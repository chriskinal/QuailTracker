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
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Data.Converters;
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
    private readonly NoiseReductionService _noiseReduction;
    private readonly ConfigService _configService;
    private readonly AppStateService _appState;
    private readonly StereoBearingService _bearingService;
    [ObservableProperty]
    private string _statusMessage = string.Empty;

    private readonly Action<string> _setStatus;
    private AudioFile? _loadedFile;
    private CancellationTokenSource? _analysisCts;
    private CancellationTokenSource? _playbackCts;
    private CancellationTokenSource? _regenCts;

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

    /// <summary>Projection of <see cref="AppStateService.IsModelLoaded"/> — single source of truth.</summary>
    public bool IsModelLoaded => _appState.IsModelLoaded;

    /// <summary>Projection of <see cref="AppStateService.ModelPath"/> — single source of truth.</summary>
    public string ModelPath => _appState.ModelPath;

    [ObservableProperty]
    private double _confidenceThreshold = 0.5;

    [ObservableProperty]
    private double _sensitivity = 1.0;

    [ObservableProperty]
    private double _overlapSeconds = 0.0;

    [ObservableProperty]
    private int _mergeCount = 1;

    [ObservableProperty]
    private bool _generateSpectrogram = true;

    [ObservableProperty]
    private double _maxFrequencyHz = 24000;

    [ObservableProperty]
    private bool _noiseReductionEnabled;

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
        NoiseReductionService noiseReduction,
        ConfigService configService,
        AppStateService appState)
    {
        _audioFileService = audioFileService;
        _birdNetService = birdNetService;
        _spectrogramService = spectrogramService;
        _playbackService = playbackService;
        _noiseReduction = noiseReduction;
        _spectrogramService.NoiseReduction = noiseReduction;
        _configService = configService;
        _appState = appState;
        _bearingService = new StereoBearingService(audioFileService);
        _setStatus = msg => StatusMessage = msg;

        _appState.PropertyChanged += OnAppStateChanged;

        // Try restoring model from saved config
        _ = TryAutoLoadModelAsync();
    }

    private void OnAppStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(AppStateService.IsModelLoaded))
        {
            OnPropertyChanged(nameof(IsModelLoaded));
            AnalyzeCommand.NotifyCanExecuteChanged();
        }
        else if (e.PropertyName == nameof(AppStateService.ModelPath))
        {
            OnPropertyChanged(nameof(ModelPath));
        }
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
            _appState.ApplyModelLoaded(savedPath);
            _setStatus("BirdNet model loaded from saved path");
        }
        catch (Exception ex)
        {
            _setStatus($"Saved model could not be loaded ({ex.Message}) — use Load Model to pick a new one");
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
            MaxFrequencyHz = _loadedFile.SampleRate / 2.0;
            IsFileLoaded = true;

            if (NoiseReductionEnabled)
            {
                _setStatus("Estimating noise profile...");
                await Task.Run(() =>
                {
                    var (samples, sr) = SpectrogramService.ReadAudioMono(path);
                    _noiseReduction.EstimateNoiseProfile(samples, sr);
                });
            }

            if (GenerateSpectrogram)
            {
                _setStatus("Generating spectrogram...");

                int imageWidth = Math.Clamp((int)(FileDuration.TotalSeconds * 20), 800, 3000);
                const int imageHeight = 500;

                SpectrogramImage = await _spectrogramService.GenerateAsync(
                    path, imageWidth, imageHeight, MaxFrequencyHz, NoiseReductionEnabled);
            }
            else
            {
                SpectrogramImage = null;
            }

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

            // Single writer: AppStateService. Both ViewModels project from it.
            _appState.ApplyModelLoaded(path);
            _configService.BirdNetModelPath = path;
            await _configService.SaveAsync();

            _setStatus("BirdNet model loaded");
        }
        catch (Exception ex)
        {
            _setStatus($"Failed to load model: {ex.Message}");
            _appState.ApplyModelUnloaded();
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
                OverlapSeconds,
                Sensitivity,
                MergeCount,
                progress,
                ct: _analysisCts.Token);

            foreach (var detection in detections)
                Detections.Add(detection);

            // Compute stereo bearings if file is stereo
            if (_loadedFile.Channels == 2 && detections.Count > 0)
            {
                _setStatus($"Computing stereo bearings for {detections.Count} detections...");
                var bearingProgress = new Progress<(int current, int total)>(p =>
                    _setStatus($"Computing bearing {p.current}/{p.total}..."));

                await _bearingService.ComputeBearingsAsync(
                    detections,
                    _loadedFile.SampleRate > 0 ? _loadedFile.SampleRate : 48000,
                    bearingProgress,
                    _analysisCts!.Token);

                var withBearing = 0;
                foreach (var d in detections)
                    if (!double.IsNaN(d.BearingDeg)) withBearing++;

                _setStatus($"Analysis complete. {detections.Count} detections, {withBearing} with bearing.");
            }
            else
            {
                _setStatus($"Analysis complete. Found {detections.Count} detections.");
            }
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

    /// <summary>
    /// Export current detections to CSV (BirdNet format) or Raven Selection
    /// Table based on file extension. File I/O runs on a worker.
    /// </summary>
    [RelayCommand]
    private async Task ExportDetectionsAsync(string? path)
    {
        if (string.IsNullOrEmpty(path) || Detections.Count == 0) return;

        var detections = Detections.ToList();
        try
        {
            await Task.Run(() =>
            {
                if (path.EndsWith(".txt", StringComparison.OrdinalIgnoreCase))
                    DetectionExporter.WriteRavenTable(detections, path);
                else
                    DetectionExporter.WriteBirdNetCsv(detections, path);
            });
            _setStatus($"Exported {detections.Count} detections to {Path.GetFileName(path)}");
        }
        catch (Exception ex)
        {
            _setStatus($"Export failed: {ex.Message}");
        }
    }

    partial void OnNoiseReductionEnabledChanged(bool value)
    {
        if (!IsFileLoaded || string.IsNullOrEmpty(FilePath)) return;

        if (value && !_noiseReduction.HasProfile)
            _ = EstimateNoiseProfileThenRegenerateAsync();
        else if (GenerateSpectrogram)
            _ = FullRegenerateSpectrogramAsync();
    }

    private async Task EstimateNoiseProfileThenRegenerateAsync()
    {
        try
        {
            IsGenerating = true;
            _setStatus("Estimating noise profile...");
            await Task.Run(() =>
            {
                var (samples, sr) = SpectrogramService.ReadAudioMono(FilePath);
                _noiseReduction.EstimateNoiseProfile(samples, sr);
            });

            if (GenerateSpectrogram)
            {
                int imageWidth = Math.Clamp((int)(FileDuration.TotalSeconds * 20), 800, 3000);
                const int imageHeight = 500;
                _spectrogramService.InvalidateCache();
                SpectrogramImage = await _spectrogramService.GenerateAsync(
                    FilePath, imageWidth, imageHeight, MaxFrequencyHz, NoiseReductionEnabled);
            }

            _setStatus($"Loaded {FileName}");
        }
        catch (Exception ex)
        {
            _setStatus($"Error: {ex.Message}");
        }
        finally
        {
            IsGenerating = false;
        }
    }

    partial void OnMaxFrequencyHzChanged(double value)
    {
        if (IsFileLoaded && GenerateSpectrogram && !string.IsNullOrEmpty(FilePath))
            _ = RenderSpectrogramFromCacheAsync();
    }

    /// <summary>Re-render from cached FFT data (instant). Falls back to full regen if no cache.</summary>
    private async Task RenderSpectrogramFromCacheAsync()
    {
        _regenCts?.Cancel();
        _regenCts?.Dispose();
        var cts = new CancellationTokenSource();
        _regenCts = cts;

        try
        {
            const int imageHeight = 500;
            var bitmap = await _spectrogramService.RenderCachedAsync(imageHeight, MaxFrequencyHz, cts.Token);
            if (bitmap != null)
            {
                SpectrogramImage = bitmap;
                return;
            }

            // No cache — full regeneration
            await FullRegenerateSpectrogramAsync();
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            _setStatus($"Error regenerating spectrogram: {ex.Message}");
        }
    }

    /// <summary>Full FFT recomputation (expensive). Used for file load, noise reduction toggle.</summary>
    private async Task FullRegenerateSpectrogramAsync()
    {
        _regenCts?.Cancel();
        _regenCts?.Dispose();
        var cts = new CancellationTokenSource();
        _regenCts = cts;

        try
        {
            IsGenerating = true;
            _spectrogramService.InvalidateCache();
            int imageWidth = Math.Clamp((int)(FileDuration.TotalSeconds * 20), 800, 3000);
            const int imageHeight = 500;
            SpectrogramImage = await _spectrogramService.GenerateAsync(
                FilePath, imageWidth, imageHeight, MaxFrequencyHz, NoiseReductionEnabled, cts.Token);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            _setStatus($"Error regenerating spectrogram: {ex.Message}");
        }
        finally
        {
            if (_regenCts == cts)
                IsGenerating = false;
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

            if (NoiseReductionEnabled && _noiseReduction.HasProfile)
            {
                var samples = await _audioFileService.ExtractSegmentAsync(
                    FilePath, detection.OffsetSeconds, detection.DurationSeconds, _playbackCts.Token);
                samples = _noiseReduction.Apply(samples);
                await _playbackService.PlaySamplesAsync(samples, SampleRate, _playbackCts.Token);
            }
            else
            {
                await _playbackService.PlaySegmentAsync(
                    FilePath, detection.OffsetSeconds, detection.DurationSeconds,
                    _playbackCts.Token);
            }
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

public class HalfValueConverter : IValueConverter
{
    public static readonly HalfValueConverter Instance = new();

    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is int intVal) return intVal / 2.0;
        if (value is double dVal) return dVal / 2.0;
        return 24000.0;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is double dVal) return (int)(dVal * 2);
        return 48000;
    }
}
