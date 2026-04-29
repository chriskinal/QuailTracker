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
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public partial class ProcessingViewModel : ObservableObject
{
    private readonly IAudioFileService _audioFileService;
    private readonly IBirdNetService _birdNetService;
    private readonly ConfigService _configService;
    private readonly AppStateService _appState;
    private readonly StereoBearingService _bearingService;
    private readonly ObservableCollection<AudioFile> _audioFiles;
    private readonly ObservableCollection<Detection> _detections;
    [ObservableProperty]
    private string _statusMessage = string.Empty;

    private readonly Action<string> _setStatus;
    private CancellationTokenSource? _cts;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(StartProcessingCommand))]
    private bool _isProcessing;

    /// <summary>Projection of <see cref="AppStateService.IsModelLoaded"/> — single source of truth.</summary>
    public bool IsModelLoaded => _appState.IsModelLoaded;

    /// <summary>Projection of <see cref="AppStateService.ModelPath"/> — single source of truth.</summary>
    public string ModelPath => _appState.ModelPath;

    [ObservableProperty]
    private int _currentFile;

    [ObservableProperty]
    private int _totalFiles;

    [ObservableProperty]
    private int _currentSegment;

    [ObservableProperty]
    private int _totalSegments;

    [ObservableProperty]
    private string _currentFileName = string.Empty;

    [ObservableProperty]
    private int _detectionsFound;

    [ObservableProperty]
    private double _confidenceThreshold = 0.5;

    [ObservableProperty]
    private double _overlapSeconds = 0.0;

    [ObservableProperty]
    private double _sensitivity = 1.0;

    [ObservableProperty]
    private int _mergeCount = 1;

    [ObservableProperty]
    private int _maxThreads = Math.Max(1, (int)(Environment.ProcessorCount * 0.8));

    public int MaxAvailableThreads => Environment.ProcessorCount;

    [ObservableProperty]
    private bool _filterQuailOnly = true;

    [ObservableProperty]
    private Detection? _selectedDetection;

    [ObservableProperty]
    private string _speciesFilter = string.Empty;

    public ObservableCollection<AudioFile> AudioFiles => _audioFiles;
    public ObservableCollection<Detection> Detections => _detections;

    public ObservableCollection<Detection> FilteredDetections { get; } = [];

    public ProcessingViewModel(
        IAudioFileService audioFileService,
        IBirdNetService birdNetService,
        ConfigService configService,
        AppStateService appState,
        ObservableCollection<AudioFile> audioFiles,
        ObservableCollection<Detection> detections)
    {
        _audioFileService = audioFileService;
        _birdNetService = birdNetService;
        _configService = configService;
        _appState = appState;
        _bearingService = new StereoBearingService(audioFileService);
        _audioFiles = audioFiles;
        _detections = detections;
        _setStatus = msg => StatusMessage = msg;

        _detections.CollectionChanged += (_, _) => UpdateFilteredDetections();
        _audioFiles.CollectionChanged += (_, _) => StartProcessingCommand.NotifyCanExecuteChanged();
        _appState.PropertyChanged += OnAppStateChanged;
    }

    private void OnAppStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(AppStateService.IsModelLoaded))
        {
            OnPropertyChanged(nameof(IsModelLoaded));
            StartProcessingCommand.NotifyCanExecuteChanged();
        }
        else if (e.PropertyName == nameof(AppStateService.ModelPath))
        {
            OnPropertyChanged(nameof(ModelPath));
        }
    }

    partial void OnConfidenceThresholdChanged(double value) => UpdateFilteredDetections();
    partial void OnSpeciesFilterChanged(string value) => UpdateFilteredDetections();
    partial void OnFilterQuailOnlyChanged(bool value) => UpdateFilteredDetections();

    private void UpdateFilteredDetections()
    {
        FilteredDetections.Clear();

        var filtered = _detections
            .Where(d => d.Confidence >= ConfidenceThreshold)
            .Where(d => string.IsNullOrEmpty(SpeciesFilter) ||
                        d.CommonName.Contains(SpeciesFilter, StringComparison.OrdinalIgnoreCase) ||
                        d.ScientificName.Contains(SpeciesFilter, StringComparison.OrdinalIgnoreCase))
            .Where(d => !FilterQuailOnly || TargetSpecies.QuailSpecies.Contains(d.ScientificName))
            .OrderByDescending(d => d.Confidence);

        foreach (var detection in filtered)
        {
            FilteredDetections.Add(detection);
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

    [RelayCommand(CanExecute = nameof(CanStartProcessing))]
    private async Task StartProcessingAsync()
    {
        if (IsProcessing || !IsModelLoaded || _audioFiles.Count == 0) return;

        IsProcessing = true;
        _cts = new CancellationTokenSource();
        DetectionsFound = 0;

        try
        {
            var filesToProcess = _audioFiles.Where(f => f.IsValid).ToList();
            TotalFiles = filesToProcess.Count;

            string[]? targetSpecies = FilterQuailOnly ? TargetSpecies.QuailSpecies : null;

            var progress = new Progress<BirdNetProgress>(p =>
            {
                CurrentFile = p.CurrentFile;
                TotalFiles = p.TotalFiles;
                CurrentSegment = p.CurrentFile;
                TotalSegments = p.TotalFiles;
                CurrentFileName = p.CurrentFileName;
                DetectionsFound = p.DetectionsFound;
                _setStatus($"Processed {p.CurrentFile}/{p.TotalFiles} files ({p.DetectionsFound} detections)...");
            });

            var newDetections = await _birdNetService.AnalyzeBatchAsync(
                filesToProcess,
                _audioFileService,
                ConfidenceThreshold,
                targetSpecies,
                OverlapSeconds,
                Sensitivity,
                MergeCount,
                progress,
                MaxThreads,
                _cts.Token);

            // Compute stereo bearings for detections from stereo files
            var stereoDetections = new System.Collections.Generic.List<Detection>();
            foreach (var detection in newDetections)
            {
                _detections.Add(detection);

                // Check if source file is stereo
                var af = filesToProcess.FirstOrDefault(f => f.FilePath == detection.AudioFilePath);
                if (af?.Channels == 2)
                    stereoDetections.Add(detection);
            }

            if (stereoDetections.Count > 0)
            {
                _setStatus($"Computing stereo bearings for {stereoDetections.Count} detections...");
                await _bearingService.ComputeBearingsAsync(
                    stereoDetections,
                    ct: _cts.Token);
            }

            var withBearing = newDetections.Count(d => !double.IsNaN(d.BearingDeg));
            _setStatus(withBearing > 0
                ? $"Processing complete. {newDetections.Count} detections, {withBearing} with bearing."
                : $"Processing complete. Found {newDetections.Count} detections.");
        }
        catch (OperationCanceledException)
        {
            _setStatus("Processing cancelled");
        }
        catch (Exception ex)
        {
            _setStatus($"Processing error: {ex.Message}");
        }
        finally
        {
            IsProcessing = false;
            _cts?.Dispose();
            _cts = null;
        }
    }

    private bool CanStartProcessing() => IsModelLoaded && _audioFiles.Count > 0 && !IsProcessing;

    /// <summary>
    /// Export currently filtered detections to CSV (BirdNet format) or Raven
    /// Selection Table based on file extension. File I/O runs on a worker.
    /// </summary>
    [RelayCommand]
    private async Task ExportDetectionsAsync(string? path)
    {
        if (string.IsNullOrEmpty(path) || FilteredDetections.Count == 0) return;

        var detections = FilteredDetections.ToList();
        try
        {
            await Task.Run(() =>
            {
                if (path.EndsWith(".txt", StringComparison.OrdinalIgnoreCase))
                    DetectionExporter.WriteRavenTable(detections, path);
                else
                    DetectionExporter.WriteBirdNetCsv(detections, path);
            });
            _setStatus($"Exported {detections.Count} detections to {System.IO.Path.GetFileName(path)}");
        }
        catch (Exception ex)
        {
            _setStatus($"Export failed: {ex.Message}");
        }
    }

    [RelayCommand]
    private void CancelProcessing()
    {
        _cts?.Cancel();
    }

    [RelayCommand]
    private void ClearDetections()
    {
        _detections.Clear();
        _setStatus("Cleared all detections");
    }

    [RelayCommand]
    private void SelectAllForLocalization()
    {
        foreach (var detection in FilteredDetections)
        {
            detection.IsSelected = true;
        }
        OnPropertyChanged(nameof(FilteredDetections));
    }

    [RelayCommand]
    private void DeselectAll()
    {
        foreach (var detection in _detections)
        {
            detection.IsSelected = false;
        }
        OnPropertyChanged(nameof(FilteredDetections));
    }
}
