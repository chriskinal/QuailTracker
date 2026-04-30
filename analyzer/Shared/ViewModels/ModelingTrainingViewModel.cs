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
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public sealed partial class ModelingTrainingViewModel : ObservableObject
{
    private const int MaxLogLines = 300;
    private const int ReachabilityPollMs = 3000;

    private readonly ITrainingService _service;
    private readonly CancellationTokenSource _lifetimeCts = new();
    private readonly Queue<string> _logLines = new();

    private CancellationTokenSource? _jobCts;
    private bool _isStreaming;

    public string ContainerEndpoint => _service.BaseAddress.ToString();

    // --- Hyperparameters ---

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartTrainingCommand))]
    private string _dataDir = "/data/clips";

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartTrainingCommand))]
    private string _outputDir = "/output";

    [ObservableProperty]
    private int _epochs = 100;

    [ObservableProperty]
    private int _batchSize = 32;

    [ObservableProperty]
    private bool _augment = true;

    public IReadOnlyList<int> BatchSizeOptions { get; } = [16, 32, 64];

    // --- Runtime state ---

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartTrainingCommand))]
    [NotifyCanExecuteChangedFor(nameof(CancelCommand))]
    private bool _isRunning;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartTrainingCommand))]
    private bool _isContainerReachable;

    [ObservableProperty]
    private string _stage = string.Empty;

    [ObservableProperty]
    private int _currentEpoch;

    [ObservableProperty]
    private int _totalEpochs;

    [ObservableProperty]
    private double? _latestValAuc;

    [ObservableProperty]
    private string _logTail = string.Empty;

    [ObservableProperty]
    private string _lastError = string.Empty;

    public ModelingTrainingViewModel(ITrainingService service)
    {
        _service = service;
        _ = PollReachabilityAsync(_lifetimeCts.Token);
    }

    [RelayCommand(CanExecute = nameof(CanStart))]
    private async Task StartTrainingAsync()
    {
        ResetProgressState();
        var jobCt = CancellationHelpers.Replace(ref _jobCts);
        var cfg = new QuickTrainConfig(DataDir, OutputDir, Epochs, BatchSize, Augment);

        bool accepted;
        try
        {
            accepted = await _service.StartTrainingAsync(cfg, jobCt);
        }
        catch (Exception ex)
        {
            LastError = ex.Message;
            AppendLog($"ERROR: {ex.Message}");
            return;
        }

        if (!accepted)
        {
            AppendLog("Server rejected start: a job is already running.");
            return;
        }

        IsRunning = true;
        AppendLog("Training started.");
        _ = ConsumeProgressAsync(jobCt);
    }

    [RelayCommand(CanExecute = nameof(CanCancel))]
    private async Task CancelAsync()
    {
        try
        {
            var accepted = await _service.CancelAsync(CancellationToken.None);
            AppendLog(accepted ? "Cancel requested..." : "No job running.");
        }
        catch (Exception ex)
        {
            AppendLog($"Cancel error: {ex.Message}");
        }
    }

    // SSE consumer. Returns when the server emits Done or the token cancels.
    // Progress<T> marshals OnTrainingEvent back to the UI thread (Progress<T>
    // captures the construction sync context, which is the UI thread here).
    private async Task ConsumeProgressAsync(CancellationToken ct)
    {
        if (_isStreaming) return;
        _isStreaming = true;
        var progress = new Progress<TrainingEvent>(OnTrainingEvent);
        try
        {
            await _service.StreamProgressAsync(progress, ct);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            AppendLog($"[stream error: {ex.Message}]");
        }
        finally
        {
            _isStreaming = false;
        }
    }

    private void OnTrainingEvent(TrainingEvent evt)
    {
        switch (evt)
        {
            case TrainingStatusEvent s:
                if (!string.IsNullOrEmpty(s.Stage)) Stage = s.Stage;
                if (s.State == "error" && !string.IsNullOrEmpty(s.Error))
                {
                    LastError = s.Error;
                    AppendLog($"ERROR: {s.Error}");
                }
                else if (s.State == "cancelled")
                {
                    AppendLog("[cancelled]");
                }
                else if (s.State == "done")
                {
                    AppendLog("[done]");
                }
                break;

            case TrainingEpochEvent e:
                Stage = e.Stage;
                CurrentEpoch = e.Epoch;
                TotalEpochs = e.Total;
                LatestValAuc = e.ValAuc;
                AppendLog(e.ValAuc.HasValue
                    ? $"Epoch {e.Epoch}/{e.Total}  val_auc={e.ValAuc.Value:F4}"
                    : $"Epoch {e.Epoch}/{e.Total}");
                break;

            case TrainingStageEvent st:
                Stage = st.Stage;
                AppendLog($"[stage {st.Stage}: {st.Status}]");
                break;

            case TrainingLogEvent l:
                AppendLog(l.Message);
                break;

            case TrainingDoneEvent:
                IsRunning = false;
                AppendLog("[stream closed]");
                break;
        }
    }

    private async Task PollReachabilityAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                var status = await _service.GetStatusAsync(ct);
                IsContainerReachable = true;
                // Reconcile if server says no job is running but we still think one is.
                // Don't touch IsRunning while the SSE stream is still alive — it owns that flag.
                if (IsRunning && !status.Running && !_isStreaming)
                {
                    IsRunning = false;
                }
            }
            catch (OperationCanceledException) { break; }
            catch
            {
                IsContainerReachable = false;
            }
            try { await Task.Delay(ReachabilityPollMs, ct); }
            catch (OperationCanceledException) { break; }
        }
    }

    private void AppendLog(string msg)
    {
        _logLines.Enqueue(msg);
        while (_logLines.Count > MaxLogLines) _logLines.Dequeue();
        LogTail = string.Join("\n", _logLines);
    }

    private void ResetProgressState()
    {
        CurrentEpoch = 0;
        TotalEpochs = 0;
        LatestValAuc = null;
        Stage = string.Empty;
        LastError = string.Empty;
    }

    private bool CanStart() =>
        !IsRunning
        && IsContainerReachable
        && !string.IsNullOrWhiteSpace(DataDir)
        && !string.IsNullOrWhiteSpace(OutputDir);

    private bool CanCancel() => IsRunning;
}
