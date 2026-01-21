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

    public bool IsModelLoaded => _session != null;

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

            // Load labels if available (assume labels file next to model)
            var labelsPath = Path.ChangeExtension(modelPath, ".txt");
            if (File.Exists(labelsPath))
            {
                _labels = File.ReadAllLines(labelsPath);
            }
            else
            {
                // Use default label indices as placeholders
                _labels = null;
            }
        }, ct);
    }

    public async Task<IReadOnlyList<Detection>> AnalyzeSegmentAsync(
        float[] audioSamples,
        AudioFile sourceFile,
        double offsetSeconds,
        double confidenceThreshold = 0.5,
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
                NamedOnnxValue.CreateFromTensor(_session.InputMetadata.Keys.First(), inputTensor)
            };

            // Run inference
            using var results = _session.Run(inputs);
            var output = results.First().AsEnumerable<float>().ToArray();

            // Apply softmax
            var probabilities = Softmax(output);

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
        IProgress<(int segment, int total)>? progress = null,
        CancellationToken ct = default)
    {
        var allDetections = new List<Detection>();
        var segmentCount = audioService.GetSegmentCount(audioFile, SegmentDuration);

        for (var i = 0; i < segmentCount; i++)
        {
            ct.ThrowIfCancellationRequested();

            progress?.Report((i + 1, segmentCount));

            var offsetSeconds = i * SegmentDuration;
            var samples = await audioService.ExtractSegmentAsync(
                audioFile.FilePath, offsetSeconds, SegmentDuration, ct);

            if (samples.Length == 0) continue;

            var detections = await AnalyzeSegmentAsync(
                samples, audioFile, offsetSeconds, confidenceThreshold, ct);

            // Filter by target species if specified
            if (targetSpecies != null)
            {
                detections = detections
                    .Where(d => targetSpecies.Contains(d.ScientificName))
                    .ToList();
            }

            allDetections.AddRange(detections);
        }

        return allDetections;
    }

    public async Task<IReadOnlyList<Detection>> AnalyzeBatchAsync(
        IReadOnlyList<AudioFile> audioFiles,
        IAudioFileService audioService,
        double confidenceThreshold = 0.5,
        string[]? targetSpecies = null,
        IProgress<BirdNetProgress>? progress = null,
        CancellationToken ct = default)
    {
        var allDetections = new List<Detection>();
        var totalFiles = audioFiles.Count;

        for (var fileIndex = 0; fileIndex < totalFiles; fileIndex++)
        {
            ct.ThrowIfCancellationRequested();

            var audioFile = audioFiles[fileIndex];
            if (!audioFile.IsValid) continue;

            var fileProgress = new Progress<(int segment, int total)>(p =>
            {
                progress?.Report(new BirdNetProgress(
                    fileIndex + 1,
                    totalFiles,
                    p.segment,
                    p.total,
                    audioFile.FileName,
                    allDetections.Count
                ));
            });

            var fileDetections = await AnalyzeFileAsync(
                audioFile, audioService, confidenceThreshold, targetSpecies, fileProgress, ct);

            allDetections.AddRange(fileDetections);
        }

        return allDetections;
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

        // BirdNet labels are typically in format "Scientific_Name_Common Name"
        // or "Scientific Name_Common Name"
        var parts = label.Split('_');
        if (parts.Length >= 2)
        {
            // Try to reconstruct scientific name (usually first 2 parts)
            var scientificName = string.Join(" ", parts.Take(2));
            var commonName = string.Join(" ", parts.Skip(2));
            return (scientificName, commonName);
        }

        return (label, label);
    }

    private static float[] Softmax(float[] input)
    {
        var maxVal = input.Max();
        var exp = input.Select(x => MathF.Exp(x - maxVal)).ToArray();
        var sum = exp.Sum();
        return exp.Select(x => x / sum).ToArray();
    }
}
