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

using System.Collections.Generic;

namespace QuailTracker.Analyzer.Shared.Models;

// Snapshot from GET /api/status. Running is independent of State because the
// Flask side reports State == "done"/"cancelled"/"error" after the worker thread
// has exited — Running tracks the thread itself.
public sealed record TrainingStatus(
    string State,
    string Stage,
    string Error,
    bool Running);

// Quick Train form (POST /api/train).
public sealed record QuickTrainConfig(
    string DataDir,
    string OutputDir,
    int Epochs,
    int BatchSize,
    bool Augment);

// Full pipeline (POST /api/full-pipeline).
public sealed record FullPipelineConfig(
    IReadOnlyList<string> SpeciesList,
    string ApiKey,
    bool SkipDownload,
    string OutputDir,
    string NoiseDir,
    int MaxRecordings,
    string QualityMin,
    double MinConf,
    int Epochs,
    int BatchSize,
    bool Augment);

// Species-only download (POST /api/download-species).
public sealed record SpeciesDownloadConfig(
    string Species,
    string ApiKey,
    string OutputDir,
    int MaxRecordings,
    string QualityMin,
    double MinConf);

// One file from GET /api/outputs.
public sealed record OutputArtifact(string FileName, long SizeBytes);

// Discriminated union of events parsed from the SSE /api/progress stream.
// Construction context: the IProgress<TrainingEvent> the caller passes in
// determines marshaling — VMs should use Progress<T> created on the UI thread.
public abstract record TrainingEvent;

public sealed record TrainingStatusEvent(
    string State,
    string Stage,
    string Error) : TrainingEvent;

public sealed record TrainingEpochEvent(
    string Stage,
    int Epoch,
    int Total,
    double? ValAuc) : TrainingEvent;

public sealed record TrainingStageEvent(
    string Stage,
    string Status) : TrainingEvent;

public sealed record TrainingLogEvent(string Message) : TrainingEvent;

public sealed record TrainingDoneEvent : TrainingEvent;
