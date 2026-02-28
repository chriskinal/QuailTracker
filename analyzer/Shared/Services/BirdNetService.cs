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
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.ML.OnnxRuntime;
using Microsoft.ML.OnnxRuntime.Tensors;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Service for bird species detection using BirdNet ONNX model.
/// </summary>
public class BirdNetService : IBirdNetService
{
    private const int SampleRate = 48000;
    private const double SegmentDuration = 3.0;
    private const int SegmentSamples = SampleRate * 3; // 144000 samples for 3 seconds

    private InferenceSession? _session;
    private string[]? _labels;
    private string _inputName = "input";

    public bool IsModelLoaded => _session != null;
    public string? ModelPath { get; private set; }

    public async Task LoadModelAsync(string modelPath, CancellationToken ct = default)
    {
        await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            // Dispose existing session
            _session?.Dispose();

            // Load ONNX model
            var options = new SessionOptions
            {
                GraphOptimizationLevel = GraphOptimizationLevel.ORT_ENABLE_ALL
            };

            _session = new InferenceSession(modelPath, options);
            _inputName = _session.InputMetadata.Keys.First();
            ModelPath = modelPath;

            // Search for labels file in multiple locations
            _labels = FindLabels(modelPath);
        }, ct);
    }

    private static string[]? FindLabels(string modelPath)
    {
        var modelDir = Path.GetDirectoryName(modelPath) ?? ".";

        // Try common label file locations/names
        string[] candidates =
        [
            Path.Combine(modelDir, "BirdNET_GLOBAL_6K_V2.4_Labels.txt"),
            Path.Combine(modelDir, "BirdNET_GLOBAL_6K_V2.4_Labels_en_us.txt"),
            Path.Combine(modelDir, "labels", "en_us.txt"),
            Path.Combine(modelDir, "labels", "BirdNET_GLOBAL_6K_V2.4_Labels_en_us.txt"),
            Path.ChangeExtension(modelPath, ".txt"),
            Path.Combine(modelDir, "labels.txt"),
        ];

        foreach (var candidate in candidates)
        {
            if (File.Exists(candidate))
                return File.ReadAllLines(candidate);
        }

        // Search for any en_us label file in subdirectories
        try
        {
            var found = Directory.GetFiles(modelDir, "*en_us*", SearchOption.AllDirectories);
            if (found.Length > 0)
                return File.ReadAllLines(found[0]);
        }
        catch
        {
            // Ignore search errors
        }

        return null;
    }

    public async Task<IReadOnlyList<Detection>> AnalyzeSegmentAsync(
        float[] audioSamples,
        AudioFile sourceFile,
        double offsetSeconds,
        double confidenceThreshold = 0.5,
        double sensitivity = 1.0,
        CancellationToken ct = default)
    {
        if (_session == null)
            throw new InvalidOperationException("Model not loaded. Call LoadModelAsync first.");

        return await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            // Ensure correct sample count
            var samples = audioSamples;
            if (samples.Length < SegmentSamples)
            {
                // Pad with zeros
                var padded = new float[SegmentSamples];
                Array.Copy(samples, padded, samples.Length);
                samples = padded;
            }
            else if (samples.Length > SegmentSamples)
            {
                // Trim
                samples = samples[..SegmentSamples];
            }

            // Create input tensor (batch_size=1, samples)
            var inputTensor = new DenseTensor<float>(samples, new[] { 1, SegmentSamples });

            var inputs = new List<NamedOnnxValue>
            {
                NamedOnnxValue.CreateFromTensor(_inputName, inputTensor)
            };

            // Run inference
            using var results = _session.Run(inputs);
            var output = results.First().AsEnumerable<float>().ToArray();

            // Apply sigmoid (each class scored independently, matches BirdNET-Analyzer)
            var probabilities = Sigmoid(output);

            // Apply sensitivity sigmoid (logit-scale, matches BirdNET-Analyzer)
            if (Math.Abs(sensitivity - 1.0) > 0.01)
            {
                for (var i = 0; i < probabilities.Length; i++)
                {
                    probabilities[i] = (float)(1.0 / (1.0 + Math.Exp(-sensitivity *
                        Math.Log(probabilities[i] / (1.0001 - probabilities[i])))));
                }
            }

            // Find detections above threshold
            var detections = new List<Detection>();
            var timestamp = sourceFile.Timestamp.AddSeconds(offsetSeconds);

            for (var i = 0; i < probabilities.Length; i++)
            {
                if (probabilities[i] >= confidenceThreshold)
                {
                    var (scientificName, commonName) = GetSpeciesName(i);

                    detections.Add(new Detection
                    {
                        AudioFilePath = sourceFile.FilePath,
                        StationId = sourceFile.StationId,
                        Timestamp = timestamp,
                        OffsetSeconds = offsetSeconds,
                        DurationSeconds = SegmentDuration,
                        ScientificName = scientificName,
                        CommonName = commonName,
                        Confidence = probabilities[i]
                    });
                }
            }

            return detections;
        }, ct);
    }

    public async Task<IReadOnlyList<Detection>> AnalyzeFileAsync(
        AudioFile audioFile,
        IAudioFileService audioService,
        double confidenceThreshold = 0.5,
        string[]? targetSpecies = null,
        double overlapSeconds = 0.0,
        double sensitivity = 1.0,
        int mergeCount = 1,
        IProgress<(int segment, int total)>? progress = null,
        Func<float[], float[]>? preprocessor = null,
        CancellationToken ct = default)
    {
        var allDetections = new List<Detection>();
        var segmentCount = audioService.GetSegmentCount(audioFile, SegmentDuration, overlapSeconds);
        var step = SegmentDuration - overlapSeconds;
        if (step <= 0.1) step = SegmentDuration;

        for (var i = 0; i < segmentCount; i++)
        {
            ct.ThrowIfCancellationRequested();

            progress?.Report((i + 1, segmentCount));

            var offsetSeconds = i * step;
            var samples = await audioService.ExtractSegmentAsync(
                audioFile.FilePath, offsetSeconds, SegmentDuration, ct);

            if (samples.Length == 0) continue;

            if (preprocessor != null)
                samples = preprocessor(samples);

            var detections = await AnalyzeSegmentAsync(
                samples, audioFile, offsetSeconds, confidenceThreshold, sensitivity, ct);

            // Filter by target species if specified
            if (targetSpecies != null)
            {
                detections = detections
                    .Where(d => targetSpecies.Contains(d.ScientificName))
                    .ToList();
            }

            allDetections.AddRange(detections);
        }

        return MergeDetections(allDetections, mergeCount, step);
    }

    public async Task<IReadOnlyList<Detection>> AnalyzeBatchAsync(
        IReadOnlyList<AudioFile> audioFiles,
        IAudioFileService audioService,
        double confidenceThreshold = 0.5,
        string[]? targetSpecies = null,
        double overlapSeconds = 0.0,
        double sensitivity = 1.0,
        int mergeCount = 1,
        IProgress<BirdNetProgress>? progress = null,
        int maxThreads = 0,
        CancellationToken ct = default)
    {
        var allDetections = new ConcurrentBag<IReadOnlyList<Detection>>();
        var validFiles = audioFiles.Where(f => f.IsValid).ToList();
        var totalFiles = validFiles.Count;
        var filesCompleted = 0;
        var detectionCount = 0;
        var maxConcurrency = Math.Max(1, maxThreads > 0 ? maxThreads : (int)(Environment.ProcessorCount * 0.8));

        await Parallel.ForEachAsync(validFiles,
            new ParallelOptions { MaxDegreeOfParallelism = maxConcurrency, CancellationToken = ct },
            async (audioFile, token) =>
            {
                var fileDetections = await AnalyzeFileAsync(
                    audioFile, audioService, confidenceThreshold, targetSpecies,
                    overlapSeconds, sensitivity, mergeCount, null, null, token);

                allDetections.Add(fileDetections);
                var newDetectionCount = Interlocked.Add(ref detectionCount, fileDetections.Count);
                var completed = Interlocked.Increment(ref filesCompleted);

                progress?.Report(new BirdNetProgress(
                    completed,
                    totalFiles,
                    0,
                    0,
                    audioFile.FileName,
                    newDetectionCount
                ));
            });

        return allDetections.SelectMany(d => d).ToList();
    }

    public IReadOnlyList<string> GetSpeciesLabels()
    {
        return _labels ?? Array.Empty<string>();
    }

    public void Dispose()
    {
        _session?.Dispose();
        _session = null;
    }

    private (string scientificName, string commonName) GetSpeciesName(int index)
    {
        if (_labels == null || index >= _labels.Length)
        {
            return ($"Species_{index}", $"Unknown Species {index}");
        }

        var label = _labels[index];

        // BirdNET labels use "Scientific Name_Common Name" format
        // e.g., "Colinus virginianus_Northern Bobwhite"
        var separatorIndex = label.IndexOf('_');
        if (separatorIndex > 0)
        {
            var scientificName = label[..separatorIndex];
            var commonName = label[(separatorIndex + 1)..];
            return (scientificName, commonName);
        }

        return (label, label);
    }

    private static List<Detection> MergeDetections(List<Detection> detections, int mergeCount, double step)
    {
        if (mergeCount <= 1 || detections.Count == 0)
            return detections;

        var merged = new List<Detection>();
        var groups = detections
            .GroupBy(d => (d.ScientificName, d.StationId, d.AudioFilePath));

        foreach (var group in groups)
        {
            var sorted = group.OrderBy(d => d.OffsetSeconds).ToList();
            var maxGap = step * 1.1; // consecutive if within one step (+10% tolerance)

            var runStart = 0;
            while (runStart < sorted.Count)
            {
                // Find end of this run of consecutive detections
                var runEnd = runStart;
                while (runEnd + 1 < sorted.Count &&
                       runEnd - runStart + 1 < mergeCount &&
                       sorted[runEnd + 1].OffsetSeconds - sorted[runEnd].OffsetSeconds <= maxGap)
                {
                    runEnd++;
                }

                // Keep the highest-confidence detection from the run
                var best = sorted[runStart];
                for (var i = runStart + 1; i <= runEnd; i++)
                {
                    if (sorted[i].Confidence > best.Confidence)
                        best = sorted[i];
                }
                merged.Add(best);

                runStart = runEnd + 1;
            }
        }

        return merged.OrderByDescending(d => d.Confidence).ToList();
    }

    private static float[] Sigmoid(float[] input)
    {
        var result = new float[input.Length];
        for (int i = 0; i < input.Length; i++)
            result[i] = 1.0f / (1.0f + MathF.Exp(-input[i]));
        return result;
    }
}
