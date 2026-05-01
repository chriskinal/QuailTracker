/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

using System;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Single-writer status of the training container, shared across every tab
/// that talks to it (Modeling Training, Modeling Evaluation, Modeling Data
/// xeno-canto). One poll loop instead of N — fixes the bug where each tab's
/// VM had its own independent reachability state and would show stale
/// "offline" until the user round-tripped through another tab.
///
/// Per Plans/threading_model.md, IsContainerReachable / LastStatus are written
/// only from this service, on the UI thread (Dispatcher.UIThread.Post).
/// </summary>
public partial class TrainingContainerStatusService : ObservableObject, IDisposable
{
    private const int PollIntervalMs = 3000;

    private readonly ITrainingService _service;
    private readonly CancellationTokenSource _cts = new();

    [ObservableProperty]
    private bool _isContainerReachable;

    [ObservableProperty]
    private TrainingStatus? _lastStatus;

    public event Action? CameOnline;
    public event Action? WentOffline;

    public TrainingContainerStatusService(ITrainingService service)
    {
        _service = service;
        _ = Task.Run(() => PollLoopAsync(_cts.Token));
    }

    private async Task PollLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                var status = await _service.GetStatusAsync(ct).ConfigureAwait(false);
                Dispatcher.UIThread.Post(() => ApplyOnline(status));
            }
            catch (OperationCanceledException) { break; }
            catch
            {
                Dispatcher.UIThread.Post(ApplyOffline);
            }
            try { await Task.Delay(PollIntervalMs, ct).ConfigureAwait(false); }
            catch (OperationCanceledException) { break; }
        }
    }

    private void ApplyOnline(TrainingStatus status)
    {
        var was = IsContainerReachable;
        LastStatus = status;
        IsContainerReachable = true;
        if (!was) CameOnline?.Invoke();
    }

    private void ApplyOffline()
    {
        var was = IsContainerReachable;
        IsContainerReachable = false;
        if (was) WentOffline?.Invoke();
    }

    public void Dispose()
    {
        _cts.Cancel();
        _cts.Dispose();
    }
}
