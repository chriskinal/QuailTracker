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
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public sealed partial class ModelingEvaluationViewModel : ObservableObject
{
    private readonly ITrainingService _service;
    private readonly TrainingContainerStatusService _status;
    private readonly CancellationTokenSource _lifetimeCts = new();
    private CancellationTokenSource? _refreshCts;

    public string ContainerEndpoint => _service.BaseAddress.ToString();

    public ObservableCollection<EvaluationArtifactItem> Outputs { get; } = [];
    public ObservableCollection<EvaluationChartImage> Charts { get; } = [];

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SaveSelectedCommand))]
    private int _checkedCount;

    /// <summary>Projection of <see cref="TrainingContainerStatusService.IsContainerReachable"/>.</summary>
    public bool IsContainerReachable => _status.IsContainerReachable;

    [ObservableProperty]
    private bool _isRefreshing;

    [ObservableProperty]
    private string _statusMessage = string.Empty;

    [ObservableProperty]
    private string _lastError = string.Empty;

    [ObservableProperty]
    private bool _canStartContainer;

    public ModelingEvaluationViewModel(ITrainingService service, TrainingContainerStatusService status)
    {
        _service = service;
        _status = status;
        _canStartContainer = TrainingDirectoryLocator.Find() is not null;
        _status.PropertyChanged += OnStatusPropertyChanged;
        _status.CameOnline += () => StatusMessage = "Container online.";
        _status.WentOffline += () => StatusMessage = "Container offline.";
    }

    private void OnStatusPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(TrainingContainerStatusService.IsContainerReachable))
        {
            OnPropertyChanged(nameof(IsContainerReachable));
            RefreshCommand.NotifyCanExecuteChanged();
            SaveAllCommand.NotifyCanExecuteChanged();
            SaveSelectedCommand.NotifyCanExecuteChanged();
        }
    }

    [RelayCommand(CanExecute = nameof(CanRefresh))]
    private async Task RefreshAsync()
    {
        IsRefreshing = true;
        LastError = string.Empty;
        StatusMessage = "Refreshing artifacts...";
        var ct = CancellationHelpers.Replace(ref _refreshCts);

        try
        {
            var artifacts = await _service.ListOutputsAsync(ct);

            foreach (var old in Outputs)
                old.PropertyChanged -= OnItemPropertyChanged;
            Outputs.Clear();
            foreach (var a in artifacts.OrderBy(a => a.FileName, StringComparer.OrdinalIgnoreCase))
            {
                var item = new EvaluationArtifactItem(a);
                item.PropertyChanged += OnItemPropertyChanged;
                Outputs.Add(item);
            }
            CheckedCount = 0;

            // Pull all PNG charts into memory and rebuild the visible list.
            var newCharts = new List<EvaluationChartImage>();
            foreach (var a in artifacts.Where(a => a.FileName.EndsWith(".png", StringComparison.OrdinalIgnoreCase))
                                       .OrderBy(a => a.FileName, StringComparer.OrdinalIgnoreCase))
            {
                ct.ThrowIfCancellationRequested();
                try
                {
                    var bytes = await _service.DownloadOutputBytesAsync(a.FileName, ct);
                    using var ms = new MemoryStream(bytes);
                    var bmp = new Bitmap(ms);
                    newCharts.Add(new EvaluationChartImage(a.FileName, bmp));
                }
                catch (Exception ex)
                {
                    StatusMessage = $"Skipped {a.FileName}: {ex.Message}";
                }
            }
            Charts.Clear();
            foreach (var c in newCharts) Charts.Add(c);

            StatusMessage = $"{Outputs.Count} artifact(s); {Charts.Count} chart(s).";
        }
        catch (OperationCanceledException) { StatusMessage = "Refresh cancelled."; }
        catch (Exception ex)
        {
            LastError = ex.Message;
            StatusMessage = $"Refresh failed: {ex.Message}";
        }
        finally
        {
            IsRefreshing = false;
        }
    }

    // Save every checked artifact under a chosen folder. The view supplies
    // the folder via a Folder picker.
    [RelayCommand(CanExecute = nameof(CanSaveSelected))]
    private async Task SaveSelectedAsync(string? destFolder)
    {
        if (string.IsNullOrEmpty(destFolder)) return;
        var snapshot = Outputs.Where(o => o.IsSelected).Select(o => o.Artifact).ToList();
        if (snapshot.Count == 0) return;

        int ok = 0;
        try
        {
            Directory.CreateDirectory(destFolder);
            foreach (var a in snapshot)
            {
                StatusMessage = $"Downloading {a.FileName}... ({ok + 1}/{snapshot.Count})";
                var dest = Path.Combine(destFolder, a.FileName);
                await _service.DownloadOutputAsync(a.FileName, dest, _lifetimeCts.Token);
                ok++;
            }
            StatusMessage = $"Saved {ok}/{snapshot.Count} checked artifact(s) to {destFolder}";
        }
        catch (Exception ex)
        {
            LastError = ex.Message;
            StatusMessage = $"Saved {ok}/{snapshot.Count}; aborted: {ex.Message}";
        }
    }

    // Save every listed artifact under a chosen folder. The view supplies the
    // folder via a Folder picker.
    [RelayCommand(CanExecute = nameof(CanSaveAll))]
    private async Task SaveAllAsync(string? destFolder)
    {
        if (string.IsNullOrEmpty(destFolder)) return;
        if (Outputs.Count == 0) return;

        var snapshot = Outputs.Select(o => o.Artifact).ToList();
        int ok = 0;
        try
        {
            Directory.CreateDirectory(destFolder);
            foreach (var a in snapshot)
            {
                StatusMessage = $"Downloading {a.FileName}... ({ok + 1}/{snapshot.Count})";
                var dest = Path.Combine(destFolder, a.FileName);
                await _service.DownloadOutputAsync(a.FileName, dest, _lifetimeCts.Token);
                ok++;
            }
            StatusMessage = $"Saved {ok}/{snapshot.Count} artifact(s) to {destFolder}";
        }
        catch (Exception ex)
        {
            LastError = ex.Message;
            StatusMessage = $"Saved {ok}/{snapshot.Count}; aborted: {ex.Message}";
        }
    }

    [RelayCommand]
    private void OpenWebUi()
    {
        try
        {
            var psi = new ProcessStartInfo(_service.BaseAddress.ToString())
            {
                UseShellExecute = true,
            };
            Process.Start(psi);
        }
        catch (Exception ex)
        {
            StatusMessage = $"Could not open browser: {ex.Message}";
        }
    }

    [RelayCommand]
    private async Task StartContainerAsync()
    {
        var dir = TrainingDirectoryLocator.Find();
        if (dir is null)
        {
            StatusMessage = "Cannot find training/ directory with docker-compose.yml.";
            return;
        }

        StatusMessage = $"docker compose up -d  (cwd: {dir})";
        try
        {
            var psi = new ProcessStartInfo("docker", "compose up -d")
            {
                WorkingDirectory = dir,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
            };
            using var p = Process.Start(psi)
                ?? throw new InvalidOperationException("docker did not start");
            await p.WaitForExitAsync(CancellationToken.None);
            if (p.ExitCode == 0)
            {
                StatusMessage = "Container start requested. Watch for online indicator.";
            }
            else
            {
                var err = await p.StandardError.ReadToEndAsync(CancellationToken.None);
                StatusMessage = $"docker compose up -d failed (exit {p.ExitCode}): {err.Trim()}";
            }
        }
        catch (Exception ex)
        {
            StatusMessage = $"Could not run docker: {ex.Message}. Is Docker installed and on PATH?";
        }
    }

    private bool CanRefresh() => !IsRefreshing && IsContainerReachable;
    private bool CanSaveSelected() => CheckedCount > 0 && IsContainerReachable;
    private bool CanSaveAll() => Outputs.Count > 0 && IsContainerReachable;

    private void OnItemPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(EvaluationArtifactItem.IsSelected))
            CheckedCount = Outputs.Count(o => o.IsSelected);
    }
}

// One row in the artifacts grid. Wraps OutputArtifact so the grid can bind to
// a humanized size string without a value converter, and adds an IsSelected
// flag for the checkbox column.
public sealed partial class EvaluationArtifactItem : ObservableObject
{
    public OutputArtifact Artifact { get; }
    public string FileName => Artifact.FileName;
    public long SizeBytes => Artifact.SizeBytes;
    public string SizeDisplay { get; }

    [ObservableProperty]
    private bool _isSelected;

    public EvaluationArtifactItem(OutputArtifact a)
    {
        Artifact = a;
        SizeDisplay = FormatBytes(a.SizeBytes);
    }

    private static string FormatBytes(long b) =>
        b < 1024 ? $"{b} B"
        : b < 1024L * 1024 ? $"{b / 1024.0:F1} KB"
        : b < 1024L * 1024 * 1024 ? $"{b / (1024.0 * 1024):F1} MB"
        : $"{b / (1024.0 * 1024 * 1024):F2} GB";
}

public sealed record EvaluationChartImage(string FileName, Bitmap Image);
