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
/// Service for reading and processing WAV audio files.
/// </summary>
public interface IAudioFileService
{
    /// <summary>
    /// Loads metadata from a WAV file without reading audio data.
    /// </summary>
    Task<AudioFile> LoadFileAsync(string filePath, CancellationToken ct = default);

    /// <summary>
    /// Loads all WAV files from a directory.
    /// </summary>
    Task<IReadOnlyList<AudioFile>> LoadFolderAsync(
        string folderPath,
        IProgress<(int current, int total, string fileName)>? progress = null,
        CancellationToken ct = default);

    /// <summary>
    /// Extracts a 3-second audio segment for BirdNet processing.
    /// Returns normalized float samples at 48kHz.
    /// </summary>
    Task<float[]> ExtractSegmentAsync(
        string filePath,
        double offsetSeconds,
        double durationSeconds = 3.0,
        CancellationToken ct = default);

    /// <summary>
    /// Gets the total number of segments in an audio file, accounting for overlap.
    /// </summary>
    int GetSegmentCount(AudioFile audioFile, double segmentDuration = 3.0, double overlapSeconds = 0.0);

    /// <summary>
    /// Extracts separate L and R channel segments from a stereo audio file.
    /// Returns (left, right) float arrays at native sample rate (no resampling).
    /// Returns (empty, empty) if file is mono.
    /// </summary>
    Task<(float[] left, float[] right)> ExtractStereoSegmentAsync(
        string filePath,
        double offsetSeconds,
        double durationSeconds = 3.0,
        CancellationToken ct = default);
}
