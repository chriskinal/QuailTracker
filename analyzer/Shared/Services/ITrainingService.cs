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

public interface ITrainingService
{
    Uri BaseAddress { get; }

    Task<TrainingStatus> GetStatusAsync(CancellationToken ct = default);

    // Returns true if the request was accepted and a job started, false if a
    // job was already running (HTTP 409). Throws on transport / 5xx.
    Task<bool> StartTrainingAsync(QuickTrainConfig config, CancellationToken ct = default);
    Task<bool> StartFullPipelineAsync(FullPipelineConfig config, CancellationToken ct = default);
    Task<bool> DownloadSpeciesAsync(SpeciesDownloadConfig config, CancellationToken ct = default);

    // Returns true if a cancel was requested, false if no job was running.
    Task<bool> CancelAsync(CancellationToken ct = default);

    Task<IReadOnlyList<OutputArtifact>> ListOutputsAsync(CancellationToken ct = default);

    // Authoritative list of species the container's data directory currently
    // has clips for. Used to populate the species picker so users pick from
    // existing names instead of typing variants. Falls back to local file
    // scan in callers if the API is unreachable.
    Task<IReadOnlyList<string>> GetSpeciesAsync(CancellationToken ct = default);

    // Streams the artifact directly to disk — does not buffer the whole file
    // in memory. destPath is overwritten if it exists.
    Task DownloadOutputAsync(string filename, string destPath, CancellationToken ct = default);

    // Pulls an artifact into memory. Use only for small files that the UI
    // needs to read directly (chart PNGs, metrics.json). Multi-MB binaries
    // (.tflite, .onnx) should go through DownloadOutputAsync to disk.
    Task<byte[]> DownloadOutputBytesAsync(string filename, CancellationToken ct = default);

    // Opens GET /api/progress as a Server-Sent Events stream and dispatches
    // parsed events through `sink` until the stream ends or `ct` cancels.
    // The Flask server runs at most one job at a time, so a single subscriber
    // per VM is sufficient.
    Task StreamProgressAsync(IProgress<TrainingEvent> sink, CancellationToken ct = default);
}
