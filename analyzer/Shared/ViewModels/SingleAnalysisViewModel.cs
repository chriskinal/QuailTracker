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
using System.Collections.ObjectModel;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public partial class SingleAnalysisViewModel : ObservableObject
{
    private readonly IAudioFileService _audioFileService;
    private readonly IBirdNetService _birdNetService;
    private readonly SpectrogramService _spectrogramService;
    private readonly AudioPlaybackService _playbackService;
    private readonly ConfigService _configService;
    private readonly AppStateService _appState;
    private readonly Action<string> _setStatus;
    private AudioFile? _loadedFile;
    private CancellationTokenSource? _analysisCts;
    private CancellationTokenSource? _playbackCts;

    [ObservableProperty]
    private string _filePath = string.Empty;

    [ObservableProperty]
    private string _fileName = "No file loaded";

    [ObservableProperty]
    private string _fileInfo = string.Empty;

    [ObservableProperty]
    private TimeSpan _fileDuration;

    [ObservableProperty]
    private int _sampleRate;

    [ObservableProperty]
    private WriteableBitmap? _spectrogramImage;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(AnalyzeCommand))]
    private bool _isFileLoaded;

    [ObservableProperty]
    private bool _isGenerating;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(AnalyzeCommand))]
    private bool _isAnalyzing;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(AnalyzeCommand))]
    private bool _isModelLoaded;

    [ObservableProperty]
    private string _modelPath = string.Empty;

    [ObservableProperty]
    private double _confidenceThreshold = 0.5;

    [ObservableProperty]
    private double _sensitivity = 1.0;

    [ObservableProperty]
    private bool _isPlaying;

    [ObservableProperty]
    private int _analysisProgress;

    [ObservableProperty]
    private int _analysisTotalSegments;

    public ObservableCollection<Detection> Detections { get; } = [];

    public SingleAnalysisViewModel(
        IAudioFileService audioFileService,
        IBirdNetService birdNetService,
        SpectrogramService spectrogramService,
        AudioPlaybackService playbackService,
        ConfigService configService,
        AppStateService appState,
        Action<string> setStatus)
    {
        _audioFileService = audioFileService;
        _birdNetService = birdNetService;
        _spectrogramService = spectrogramService;
        _playbackService = playbackService;
        _configService = configService;
        _appState = appState;
        _setStatus = setStatus;

        // Sync from service in case model was already loaded elsewhere
        IsModelLoaded = _birdNetService.IsModelLoaded;
        if (_birdNetService.ModelPath != null)
            ModelPath = _birdNetService.ModelPath;

        // Try restoring model from saved config
        _ = TryAutoLoadModelAsync();
    }

    private async Task TryAutoLoadModelAsync()
    {
        if (_birdNetService.IsModelLoaded) return;

        var savedPath = _configService.BirdNetModelPath;
        if (string.IsNullOrEmpty(savedPath) || !File.Exists(savedPath)) return;

        try
        {
            _setStatus("Loading saved BirdNet model...");
            await _birdNetService.LoadModelAsync(savedPath);
            ModelPath = savedPath;
            IsModelLoaded = _birdNetService.IsModelLoaded;
            _appState.IsModelLoaded = IsModelLoaded;
            _appState.ModelPath = savedPath;
            _setStatus("BirdNet model loaded from saved path");
        }
        catch
        {
            _setStatus("Saved model could not be loaded — use Load Model to pick a new one");
        }
    }

    [RelayCommand]
    private async Task LoadFileAsync(string? path)
    {
        if (string.IsNullOrEmpty(path)) return;

        try
        {
            _setStatus("Loading audio file...");
            IsGenerating = true;
            Detections.Clear();

            _loadedFile = await _audioFileService.LoadFileAsync(path);

            if (!_loadedFile.IsValid)
            {
                _setStatus($"Failed to load: {_loadedFile.ErrorMessage}");
                IsGenerating = false;
                return;
            }

            FilePath = path;
            FileName = _loadedFile.FileName;
            SampleRate = _loadedFile.SampleRate;
            FileDuration = _loadedFile.Duration;
            FileInfo = $"{_loadedFile.SampleRate} Hz | {(_loadedFile.Channels == 1 ? "Mono" : "Stereo")} | {_loadedFile.DurationText}";
            IsFileLoaded = true;

            _setStatus("Generating spectrogram...");

            int imageWidth = Math.Clamp((int)(FileDuration.TotalSeconds * 20), 800, 3000);
            const int imageHeight = 500;

            SpectrogramImage = await _spectrogramService.GenerateAsync(
                path, imageWidth, imageHeight);

            _setStatus($"Loaded {FileName}");
        }
        catch (Exception ex)
        {
            _setStatus($"Error loading file: {ex.Message}");
        }
        finally
        {
            IsGenerating = false;
        }
    }

    [RelayCommand]
    private async Task LoadModelAsync(string? path)
    {
        if (string.IsNullOrEmpty(path)) return;

        try
        {
            _setStatus("Loading BirdNet model...");
            await _birdNetService.LoadModelAsync(path);
            ModelPath = path;
            IsModelLoaded = _birdNetService.IsModelLoaded;

            // Persist to config and update shared state
            _configService.BirdNetModelPath = path;
            _configService.Save();
            _appState.IsModelLoaded = IsModelLoaded;
            _appState.ModelPath = path;

            _setStatus("BirdNet model loaded");
        }
        catch (Exception ex)
        {
            _setStatus($"Failed to load model: {ex.Message}");
            IsModelLoaded = false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanAnalyze))]
    private async Task AnalyzeAsync()
    {
        if (_loadedFile == null || !IsModelLoaded) return;

        IsAnalyzing = true;
        _analysisCts = new CancellationTokenSource();
        Detections.Clear();

        try
        {
            var progress = new Progress<(int segment, int total)>(p =>
            {
                AnalysisProgress = p.segment;
                AnalysisTotalSegments = p.total;
                _setStatus($"Analyzing segment {p.segment}/{p.total}...");
            });

            var detections = await _birdNetService.AnalyzeFileAsync(
                _loadedFile,
                _audioFileService,
                ConfidenceThreshold,
                targetSpecies: null,
                overlapSeconds: 0.0,
                Sensitivity,
                mergeCount: 1,
                progress,
                _analysisCts.Token);

            foreach (var detection in detections)
                Detections.Add(detection);

            _setStatus($"Analysis complete. Found {detections.Count} detections.");
        }
        catch (OperationCanceledException)
        {
            _setStatus("Analysis cancelled");
        }
        catch (Exception ex)
        {
            _setStatus($"Analysis error: {ex.Message}");
        }
        finally
        {
            IsAnalyzing = false;
            _analysisCts?.Dispose();
            _analysisCts = null;
        }
    }

    private bool CanAnalyze() => IsFileLoaded && IsModelLoaded && !IsAnalyzing;

    [RelayCommand]
    private void CancelAnalysis()
    {
        _analysisCts?.Cancel();
    }

    public async Task PlayDetectionAsync(Detection detection)
    {
        if (string.IsNullOrEmpty(FilePath)) return;

        StopPlayback();

        IsPlaying = true;
        _playbackCts = new CancellationTokenSource();

        try
        {
            _setStatus($"Playing {detection.DisplayName} at {detection.OffsetSeconds:F1}s...");
            await _playbackService.PlaySegmentAsync(
                FilePath, detection.OffsetSeconds, detection.DurationSeconds,
                _playbackCts.Token);
            _setStatus($"Loaded {FileName}");
        }
        catch (OperationCanceledException)
        {
            // Stopped by user
        }
        catch (Exception ex)
        {
            _setStatus($"Playback error: {ex.Message}");
        }
        finally
        {
            IsPlaying = false;
            _playbackCts?.Dispose();
            _playbackCts = null;
        }
    }

    public void StopPlayback()
    {
        _playbackCts?.Cancel();
        _playbackService.Stop();
        IsPlaying = false;
    }
}
