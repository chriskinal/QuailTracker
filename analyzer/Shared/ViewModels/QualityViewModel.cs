/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. See <https://www.gnu.org/licenses/>.
 */

using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

/// <summary>Drives the Quality report-card panel: runs the DSP
/// <see cref="QualityAnalysisService"/> on the currently-loaded recording (the
/// path is forwarded by the shell when a file is opened in Single Analysis) and
/// exposes the metrics + suggestions to the view.</summary>
public partial class QualityViewModel : ObservableObject
{
    private readonly IAudioFileService _audioFileService;
    private readonly QualityAnalysisService _qualityService;
    private CancellationTokenSource? _cts;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(AnalyzeCommand))]
    private string _filePath = string.Empty;

    [ObservableProperty] private string _fileName = string.Empty;
    [ObservableProperty] private string _statusMessage = "Load a recording in Single Analysis, then run the quality check.";

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(AnalyzeCommand))]
    private bool _isAnalyzing;

    [ObservableProperty] private bool _hasReport;
    [ObservableProperty] private int _score;
    [ObservableProperty] private string _scoreText = "--";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ScoreColor))]
    private QualityRating _overallRating = QualityRating.Good;

    public string ScoreColor => OverallRating switch
    {
        QualityRating.Good => "#4CAF50",
        QualityRating.Fair => "#FF9800",
        _                  => "#F44336",
    };

    public ObservableCollection<QualityMetric> Metrics { get; } = [];
    public ObservableCollection<QualitySuggestion> Suggestions { get; } = [];

    public QualityViewModel(IAudioFileService audioFileService, QualityAnalysisService qualityService)
    {
        _audioFileService = audioFileService;
        _qualityService = qualityService;
    }

    /// <summary>Called by the shell when the loaded recording changes. Clears any
    /// stale report so the card never shows last file's numbers against a new one.</summary>
    partial void OnFilePathChanged(string value)
    {
        FileName = string.IsNullOrEmpty(value) ? string.Empty : Path.GetFileName(value);
        HasReport = false;
        Metrics.Clear();
        Suggestions.Clear();
        ScoreText = "--";
        StatusMessage = string.IsNullOrEmpty(value)
            ? "Load a recording in Single Analysis, then run the quality check."
            : $"Ready — {FileName}. Press Run Quality Analysis.";
    }

    private bool CanAnalyze() => !IsAnalyzing && !string.IsNullOrEmpty(FilePath);

    [RelayCommand(CanExecute = nameof(CanAnalyze))]
    private async Task AnalyzeAsync()
    {
        if (string.IsNullOrEmpty(FilePath)) return;

        IsAnalyzing = true;
        _cts = new CancellationTokenSource();
        try
        {
            var progress = new Progress<string>(msg => StatusMessage = msg);
            var report = await _qualityService.AnalyzeAsync(FilePath, _audioFileService, progress, _cts.Token);

            Metrics.Clear();
            foreach (var m in report.Metrics) Metrics.Add(m);
            Suggestions.Clear();
            foreach (var sg in report.Suggestions) Suggestions.Add(sg);

            Score = report.Score;
            OverallRating = report.OverallRating;
            ScoreText = $"{report.Score} / 100";
            HasReport = true;
            StatusMessage = $"Quality: {report.Score}/100 ({report.OverallRating}) — " +
                            $"{(report.IsStereo ? "stereo" : "mono")}, {report.SampleRate} Hz, {report.DurationSeconds:F0}s.";
        }
        catch (OperationCanceledException)
        {
            StatusMessage = "Quality analysis cancelled.";
        }
        catch (Exception ex)
        {
            StatusMessage = $"Quality analysis failed: {ex.Message}";
        }
        finally
        {
            IsAnalyzing = false;
            _cts?.Dispose();
            _cts = null;
        }
    }

    [RelayCommand]
    private void Cancel() => _cts?.Cancel();
}
