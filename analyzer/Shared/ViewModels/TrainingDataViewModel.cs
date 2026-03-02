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
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using NAudio.Wave;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public partial class TrainingDataViewModel : ObservableObject
{
    private const int ExportSampleRate = 22050;

    private readonly IAudioFileService _audioFileService;
    private readonly ObservableCollection<Detection> _detections;
    private readonly ObservableCollection<AudioFile> _audioFiles;
    [ObservableProperty]
    private string _statusMessage = string.Empty;

    private readonly Action<string> _setStatus;
    private CancellationTokenSource? _cts;

    public ObservableCollection<TrainingClip> Clips { get; } = [];

    [ObservableProperty]
    private TrainingClip? _selectedClip;

    [ObservableProperty]
    private int _confirmedCount;

    [ObservableProperty]
    private int _rejectedCount;

    [ObservableProperty]
    private int _pendingCount;

    [ObservableProperty]
    private string _exportPath = string.Empty;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ExportCommand))]
    private bool _isExporting;

    [ObservableProperty]
    private int _exportProgress;

    [ObservableProperty]
    private int _exportTotal;

    [ObservableProperty]
    private double _minConfidence = 0.5;

    [ObservableProperty]
    private bool _includeNegatives = true;

    [ObservableProperty]
    private int _negativesPerFile = 2;

    public TrainingDataViewModel(
        IAudioFileService audioFileService,
        ObservableCollection<AudioFile> audioFiles,
        ObservableCollection<Detection> detections)
    {
        _audioFileService = audioFileService;
        _audioFiles = audioFiles;
        _detections = detections;
        _setStatus = msg => StatusMessage = msg;
    }

    [RelayCommand]
    private void Populate()
    {
        Clips.Clear();

        var filtered = _detections
            .Where(d => d.Confidence >= MinConfidence)
            .OrderByDescending(d => d.Confidence);

        foreach (var detection in filtered)
        {
            var clip = new TrainingClip { Detection = detection };
            clip.PropertyChanged += (_, _) => UpdateCounts();
            Clips.Add(clip);
        }

        UpdateCounts();
        _setStatus($"Populated {Clips.Count} clips from {_detections.Count} detections (>= {MinConfidence:P0} confidence)");
    }

    [RelayCommand]
    private void Confirm()
    {
        if (SelectedClip == null) return;
        SelectedClip.Status = TrainingClipStatus.Confirmed;
        AdvanceToNextPending();
    }

    [RelayCommand]
    private void Reject()
    {
        if (SelectedClip == null) return;
        SelectedClip.Status = TrainingClipStatus.Rejected;
        AdvanceToNextPending();
    }

    [RelayCommand]
    private void Skip()
    {
        AdvanceToNextPending();
    }

    [RelayCommand]
    private void ConfirmAllFiltered()
    {
        foreach (var clip in Clips.Where(c => c.Status == TrainingClipStatus.Pending))
        {
            clip.Status = TrainingClipStatus.Confirmed;
        }
        UpdateCounts();
        _setStatus($"Confirmed all {ConfirmedCount} pending clips");
    }

    [RelayCommand(CanExecute = nameof(CanExport))]
    private async Task ExportAsync()
    {
        if (string.IsNullOrEmpty(ExportPath)) return;

        IsExporting = true;
        _cts = new CancellationTokenSource();

        try
        {
            var confirmed = Clips.Where(c => c.Status == TrainingClipStatus.Confirmed).ToList();
            var rejected = Clips.Where(c => c.Status == TrainingClipStatus.Rejected).ToList();

            ExportTotal = confirmed.Count + (IncludeNegatives ? rejected.Count : 0);
            ExportProgress = 0;

            // Count random negatives if enabled
            var randomNegativeSegments = new List<(string filePath, double offset)>();
            if (IncludeNegatives && NegativesPerFile > 0)
            {
                randomNegativeSegments = GetRandomNegativeSegments();
                ExportTotal += randomNegativeSegments.Count;
            }

            // Export confirmed clips by species
            foreach (var clip in confirmed)
            {
                _cts.Token.ThrowIfCancellationRequested();

                var speciesDir = SanitizeDirectoryName($"{clip.ScientificName}_{clip.CommonName}");
                var dir = Path.Combine(ExportPath, speciesDir);
                Directory.CreateDirectory(dir);

                var fileName = $"{clip.Timestamp:yyyyMMdd_HHmmss}_{clip.OffsetSeconds:F1}s.wav";
                var outPath = Path.Combine(dir, fileName);

                await ExportClipAsync(clip.AudioFilePath, clip.OffsetSeconds, clip.DurationSeconds, outPath, _cts.Token);

                ExportProgress++;
                _setStatus($"Exporting... {ExportProgress}/{ExportTotal}");
            }

            // Export rejected clips as noise
            if (IncludeNegatives)
            {
                var noiseDir = Path.Combine(ExportPath, "noise");
                Directory.CreateDirectory(noiseDir);

                foreach (var clip in rejected)
                {
                    _cts.Token.ThrowIfCancellationRequested();

                    var fileName = $"rejected_{clip.Timestamp:yyyyMMdd_HHmmss}_{clip.OffsetSeconds:F1}s.wav";
                    var outPath = Path.Combine(noiseDir, fileName);

                    await ExportClipAsync(clip.AudioFilePath, clip.OffsetSeconds, clip.DurationSeconds, outPath, _cts.Token);

                    ExportProgress++;
                    _setStatus($"Exporting noise... {ExportProgress}/{ExportTotal}");
                }

                // Export random negative segments
                foreach (var (filePath, offset) in randomNegativeSegments)
                {
                    _cts.Token.ThrowIfCancellationRequested();

                    var ts = Path.GetFileNameWithoutExtension(filePath);
                    var fileName = $"random_{ts}_{offset:F1}s.wav";
                    var outPath = Path.Combine(noiseDir, fileName);

                    await ExportClipAsync(filePath, offset, 3.0, outPath, _cts.Token);

                    ExportProgress++;
                    _setStatus($"Exporting random negatives... {ExportProgress}/{ExportTotal}");
                }
            }

            // Write labels.txt
            var speciesNames = confirmed
                .Select(c => $"{c.ScientificName}_{c.CommonName}")
                .Distinct()
                .OrderBy(s => s)
                .ToList();

            var labelsPath = Path.Combine(ExportPath, "labels.txt");
            await File.WriteAllLinesAsync(labelsPath, speciesNames, _cts.Token);

            // Write export_summary.json
            var summary = new
            {
                exportDate = DateTime.Now.ToString("o"),
                targetSampleRate = ExportSampleRate,
                confirmedClips = confirmed.Count,
                rejectedClips = rejected.Count,
                randomNegatives = randomNegativeSegments.Count,
                speciesCounts = confirmed
                    .GroupBy(c => $"{c.ScientificName}_{c.CommonName}")
                    .ToDictionary(g => g.Key, g => g.Count())
            };

            var summaryPath = Path.Combine(ExportPath, "export_summary.json");
            var json = JsonSerializer.Serialize(summary, new JsonSerializerOptions { WriteIndented = true });
            await File.WriteAllTextAsync(summaryPath, json, _cts.Token);

            _setStatus($"Export complete: {confirmed.Count} confirmed, {rejected.Count + randomNegativeSegments.Count} noise clips to {ExportPath}");
        }
        catch (OperationCanceledException)
        {
            _setStatus("Export cancelled");
        }
        catch (Exception ex)
        {
            _setStatus($"Export error: {ex.Message}");
        }
        finally
        {
            IsExporting = false;
            _cts?.Dispose();
            _cts = null;
        }
    }

    private bool CanExport() => !IsExporting && !string.IsNullOrEmpty(ExportPath) && Clips.Any(c => c.Status == TrainingClipStatus.Confirmed);

    [RelayCommand]
    private void CancelExport()
    {
        _cts?.Cancel();
    }

    private async Task ExportClipAsync(string sourceFile, double offset, double duration, string outPath, CancellationToken ct)
    {
        // Extract at native 48kHz
        var samples = await _audioFileService.ExtractSegmentAsync(sourceFile, offset, duration, ct);
        if (samples.Length == 0) return;

        // Resample from 48kHz to 22050Hz
        var resampled = Resample(samples, 48000, ExportSampleRate);

        // Write as 16-bit mono WAV
        await Task.Run(() =>
        {
            var format = new WaveFormat(ExportSampleRate, 16, 1);
            using var writer = new WaveFileWriter(outPath, format);
            for (int i = 0; i < resampled.Length; i++)
                writer.WriteSample(resampled[i]);
        }, ct);
    }

    private List<(string filePath, double offset)> GetRandomNegativeSegments()
    {
        var result = new List<(string, double)>();
        var rng = new Random();

        // Find audio files that have detections to pick random non-detection segments from
        var detectionsByFile = _detections
            .GroupBy(d => d.AudioFilePath)
            .ToDictionary(g => g.Key, g => g.Select(d => d.OffsetSeconds).ToHashSet());

        foreach (var audioFile in _audioFiles.Where(f => f.IsValid))
        {
            var fileDuration = audioFile.Duration.TotalSeconds;
            if (fileDuration < 3.0) continue;

            detectionsByFile.TryGetValue(audioFile.FilePath, out var detectedOffsets);
            detectedOffsets ??= new HashSet<double>();

            var added = 0;
            var attempts = 0;
            while (added < NegativesPerFile && attempts < NegativesPerFile * 10)
            {
                attempts++;
                var offset = Math.Floor(rng.NextDouble() * (fileDuration - 3.0));

                // Skip if this offset overlaps with any detection
                bool overlaps = detectedOffsets.Any(d => Math.Abs(d - offset) < 3.0);
                if (overlaps) continue;

                result.Add((audioFile.FilePath, offset));
                added++;
            }
        }

        return result;
    }

    private void AdvanceToNextPending()
    {
        var currentIndex = SelectedClip != null ? Clips.IndexOf(SelectedClip) : -1;

        // Search forward from current position
        for (int i = currentIndex + 1; i < Clips.Count; i++)
        {
            if (Clips[i].Status == TrainingClipStatus.Pending)
            {
                SelectedClip = Clips[i];
                return;
            }
        }

        // Wrap around from beginning
        for (int i = 0; i <= currentIndex && i < Clips.Count; i++)
        {
            if (Clips[i].Status == TrainingClipStatus.Pending)
            {
                SelectedClip = Clips[i];
                return;
            }
        }

        SelectedClip = null;
        _setStatus("All clips reviewed");
    }

    private void UpdateCounts()
    {
        ConfirmedCount = Clips.Count(c => c.Status == TrainingClipStatus.Confirmed);
        RejectedCount = Clips.Count(c => c.Status == TrainingClipStatus.Rejected);
        PendingCount = Clips.Count(c => c.Status == TrainingClipStatus.Pending);
        ExportCommand.NotifyCanExecuteChanged();
    }

    partial void OnExportPathChanged(string value)
    {
        ExportCommand.NotifyCanExecuteChanged();
    }

    private static float[] Resample(float[] input, int fromRate, int toRate)
    {
        var ratio = (double)toRate / fromRate;
        var newLength = (int)(input.Length * ratio);
        var output = new float[newLength];

        for (var i = 0; i < newLength; i++)
        {
            var srcIndex = i / ratio;
            var srcIndexInt = (int)srcIndex;
            var frac = (float)(srcIndex - srcIndexInt);

            if (srcIndexInt + 1 < input.Length)
                output[i] = input[srcIndexInt] * (1 - frac) + input[srcIndexInt + 1] * frac;
            else if (srcIndexInt < input.Length)
                output[i] = input[srcIndexInt];
        }

        return output;
    }

    private static string SanitizeDirectoryName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        return new string(name.Select(c => invalid.Contains(c) ? '_' : c).ToArray());
    }
}
