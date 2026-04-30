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

public sealed partial class XenoCantoDownloadViewModel : ObservableObject
{
    private const int MaxLogLines = 150;
    private const int ReachabilityPollMs = 3000;

    private readonly ITrainingService _service;
    private readonly ConfigService _configService;
    private readonly CancellationTokenSource _lifetimeCts = new();
    private readonly Queue<string> _logLines = new();

    private CancellationTokenSource? _jobCts;
    private bool _isStreaming;

    public string ContainerEndpoint => _service.BaseAddress.ToString();

    public IReadOnlyList<string> QualityOptions { get; } = ["A", "B", "C"];

    // --- Form fields ---

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartDownloadCommand))]
    private string _species = string.Empty;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartDownloadCommand))]
    private string _apiKey = string.Empty;

    [ObservableProperty]
    private string _outputDir = "/data/clips";

    [ObservableProperty]
    private int _maxRecordings = 30;

    [ObservableProperty]
    private string _qualityMin = "B";

    [ObservableProperty]
    private double _minConfidence = 0.5;

    // --- Runtime state ---

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartDownloadCommand))]
    [NotifyCanExecuteChangedFor(nameof(CancelCommand))]
    private bool _isRunning;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartDownloadCommand))]
    private bool _isContainerReachable;

    [ObservableProperty]
    private string _logTail = string.Empty;

    [ObservableProperty]
    private string _lastError = string.Empty;

    public XenoCantoDownloadViewModel(ITrainingService service, ConfigService configService)
    {
        _service = service;
        _configService = configService;
        _apiKey = configService.XenoCantoApiKey ?? string.Empty;
        _ = PollReachabilityAsync(_lifetimeCts.Token);
    }

    // Persist API key when the user edits it. Fire-and-forget save — non-critical
    // if it fails (next edit will retry).
    partial void OnApiKeyChanged(string value)
    {
        _configService.XenoCantoApiKey = value;
        _ = _configService.SaveAsync();
    }

    [RelayCommand(CanExecute = nameof(CanStart))]
    private async Task StartDownloadAsync()
    {
        LastError = string.Empty;
        var jobCt = CancellationHelpers.Replace(ref _jobCts);
        var cfg = new SpeciesDownloadConfig(
            Species: Species.Trim(),
            ApiKey: ApiKey.Trim(),
            OutputDir: OutputDir.Trim(),
            MaxRecordings: MaxRecordings,
            QualityMin: QualityMin,
            MinConf: MinConfidence);

        bool accepted;
        try
        {
            accepted = await _service.DownloadSpeciesAsync(cfg, jobCt);
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
        AppendLog($"Downloading clips for '{Species.Trim()}'...");
        _ = ConsumeProgressAsync(jobCt);
    }

    [RelayCommand(CanExecute = nameof(CanCancel))]
    private async Task CancelAsync()
    {
        try
        {
            var ok = await _service.CancelAsync(CancellationToken.None);
            AppendLog(ok ? "Cancel requested..." : "No job running.");
        }
        catch (Exception ex)
        {
            AppendLog($"Cancel error: {ex.Message}");
        }
    }

    private async Task ConsumeProgressAsync(CancellationToken ct)
    {
        if (_isStreaming) return;
        _isStreaming = true;
        var progress = new Progress<TrainingEvent>(OnEvent);
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

    private void OnEvent(TrainingEvent evt)
    {
        switch (evt)
        {
            case TrainingStatusEvent s:
                if (s.State == "error" && !string.IsNullOrEmpty(s.Error))
                {
                    LastError = s.Error;
                    AppendLog($"ERROR: {s.Error}");
                }
                else if (s.State == "cancelled") AppendLog("[cancelled]");
                else if (s.State == "done") AppendLog("[done]");
                break;
            case TrainingStageEvent st:
                AppendLog($"[{st.Stage}: {st.Status}]");
                break;
            case TrainingLogEvent l:
                AppendLog(l.Message);
                break;
            case TrainingDoneEvent:
                IsRunning = false;
                AppendLog("[stream closed]");
                break;
            // Epoch events shouldn't fire for download, but ignore if they do.
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

    private bool CanStart() =>
        !IsRunning
        && IsContainerReachable
        && !string.IsNullOrWhiteSpace(Species)
        && !string.IsNullOrWhiteSpace(ApiKey);

    private bool CanCancel() => IsRunning;
}
