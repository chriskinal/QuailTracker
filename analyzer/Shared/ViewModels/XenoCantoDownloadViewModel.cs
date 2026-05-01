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

    private readonly ITrainingService _service;
    private readonly TrainingContainerStatusService _status;
    private readonly ConfigService _configService;
    private readonly Queue<string> _logLines = new();

    private CancellationTokenSource? _jobCts;
    private bool _isStreaming;

    public string ContainerEndpoint => _service.BaseAddress.ToString();

    public IReadOnlyList<string> QualityOptions { get; } = ["A", "B", "C"];

    /// <summary>
    /// Species the user already has clips for, read from the local training
    /// data directory. Used as a picker source so users pick existing names
    /// rather than typing variants that create duplicate dirs (e.g. "Bobwhite"
    /// vs "Northern Bobwhite"). Free-text entry is still allowed.
    /// </summary>
    public ObservableCollection<string> KnownSpecies { get; } = [];

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

    /// <summary>Projection of <see cref="TrainingContainerStatusService.IsContainerReachable"/>.</summary>
    public bool IsContainerReachable => _status.IsContainerReachable;

    [ObservableProperty]
    private string _logTail = string.Empty;

    [ObservableProperty]
    private string _lastError = string.Empty;

    [ObservableProperty]
    private bool _canStartContainer;

    public XenoCantoDownloadViewModel(
        ITrainingService service,
        TrainingContainerStatusService status,
        ConfigService configService)
    {
        _service = service;
        _status = status;
        _configService = configService;
        _apiKey = configService.XenoCantoApiKey ?? string.Empty;
        _canStartContainer = TrainingDirectoryLocator.Find() is not null;
        // Initial population from local files so the dropdown isn't empty on
        // startup. The API is the canonical source and is preferred whenever
        // the container is reachable — see RefreshSpeciesAsync below.
        foreach (var name in SpeciesCatalog.LoadKnownSpecies()) KnownSpecies.Add(name);
        _status.PropertyChanged += OnStatusPropertyChanged;
        _status.CameOnline += () =>
        {
            AppendLog("[container online]");
            _ = RefreshSpeciesAsync();
        };
        _status.WentOffline += () => AppendLog("[container offline]");
    }

    private void OnStatusPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(TrainingContainerStatusService.IsContainerReachable))
        {
            OnPropertyChanged(nameof(IsContainerReachable));
            StartDownloadCommand.NotifyCanExecuteChanged();
            var serverRunning = _status.LastStatus?.Running ?? false;
            if (IsRunning && !serverRunning && !_isStreaming)
            {
                IsRunning = false;
            }
        }
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

    private void AppendLog(string msg)
    {
        _logLines.Enqueue(msg);
        while (_logLines.Count > MaxLogLines) _logLines.Dequeue();
        LogTail = string.Join("\n", _logLines);
    }

    [RelayCommand]
    private async Task RefreshSpeciesAsync()
    {
        // Prefer the container API — it's the canonical view of the data dir
        // (and works on shipped builds where the analyzer can't see the repo).
        // Fall back to local file scan when the container isn't reachable.
        if (IsContainerReachable)
        {
            try
            {
                var fromApi = await _service.GetSpeciesAsync(CancellationToken.None);
                ReplaceSpecies(fromApi);
                return;
            }
            catch { /* fall through to local */ }
        }
        ReplaceSpecies(SpeciesCatalog.LoadKnownSpecies());
    }

    private void ReplaceSpecies(IReadOnlyList<string> names)
    {
        // Preserve current selection text — avoid clobbering what the user
        // just typed if it was already valid.
        KnownSpecies.Clear();
        foreach (var name in names) KnownSpecies.Add(name);
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
            AppendLog($"Could not open browser: {ex.Message}");
        }
    }

    [RelayCommand]
    private async Task StartContainerAsync()
    {
        var dir = TrainingDirectoryLocator.Find();
        if (dir is null)
        {
            AppendLog("Cannot find training/ directory with docker-compose.yml — start the container manually.");
            return;
        }

        AppendLog($"docker compose up -d  (cwd: {dir})");
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
                AppendLog("Container start requested. Watch for [container online].");
            }
            else
            {
                var err = await p.StandardError.ReadToEndAsync(CancellationToken.None);
                AppendLog($"docker compose up -d failed (exit {p.ExitCode}): {err.Trim()}");
            }
        }
        catch (Exception ex)
        {
            AppendLog($"Could not run docker: {ex.Message}. Is Docker installed and on PATH?");
        }
    }

    private bool CanStart() =>
        !IsRunning
        && IsContainerReachable
        && !string.IsNullOrWhiteSpace(Species)
        && !string.IsNullOrWhiteSpace(ApiKey);

    private bool CanCancel() => IsRunning;
}
