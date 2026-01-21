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
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Progress information for batch BirdNet processing.
/// </summary>
public record BirdNetProgress(
    int CurrentFile,
    int TotalFiles,
    int CurrentSegment,
    int TotalSegments,
    string CurrentFileName,
    int DetectionsFound);

/// <summary>
/// Service for bird species detection using BirdNet ONNX model.
/// </summary>
public interface IBirdNetService : IDisposable
{
    /// <summary>
    /// Whether the ONNX model is loaded and ready.
    /// </summary>
    bool IsModelLoaded { get; }

    /// <summary>
    /// Loads the BirdNet ONNX model from the specified path.
    /// </summary>
    Task LoadModelAsync(string modelPath, CancellationToken ct = default);

    /// <summary>
    /// Runs inference on a single 3-second audio segment.
    /// </summary>
    Task<IReadOnlyList<Detection>> AnalyzeSegmentAsync(
        float[] audioSamples,
        AudioFile sourceFile,
        double offsetSeconds,
        double confidenceThreshold = 0.5,
        CancellationToken ct = default);

    /// <summary>
    /// Processes an entire audio file, returning all detections above the threshold.
    /// </summary>
    Task<IReadOnlyList<Detection>> AnalyzeFileAsync(
        AudioFile audioFile,
        IAudioFileService audioService,
        double confidenceThreshold = 0.5,
        string[]? targetSpecies = null,
        IProgress<(int segment, int total)>? progress = null,
        CancellationToken ct = default);

    /// <summary>
    /// Processes multiple audio files with progress reporting.
    /// </summary>
    Task<IReadOnlyList<Detection>> AnalyzeBatchAsync(
        IReadOnlyList<AudioFile> audioFiles,
        IAudioFileService audioService,
        double confidenceThreshold = 0.5,
        string[]? targetSpecies = null,
        IProgress<BirdNetProgress>? progress = null,
        CancellationToken ct = default);

    /// <summary>
    /// Gets all species labels supported by the model.
    /// </summary>
    IReadOnlyList<string> GetSpeciesLabels();
}
