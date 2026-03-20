/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

using System;

namespace QuailTracker.Shared.Models;

public record HealthReport
{
    // Recording
    public uint FilesWritten { get; init; }
    public ulong TotalBytes { get; init; }
    public uint RecordingSecs { get; init; }
    public string LastFilename { get; init; } = "";
    public uint LastFileBytes { get; init; }
    public uint LastFileSecs { get; init; }
    public uint WriteErrors { get; init; }

    // Detection
    public uint Detections { get; init; }
    public string LastSpecies { get; init; } = "";
    public uint LastConfidence { get; init; }
    public string LastDetTime { get; init; } = "";

    // System
    public uint BatteryMinMv { get; init; }
    public uint BatteryMaxMv { get; init; }
    public int TempMinC100 { get; init; }
    public int TempMaxC100 { get; init; }
    public uint BootCount { get; init; }
    public uint SdErrors { get; init; }
    public uint GpsFixLosses { get; init; }
    public uint UptimeSecs { get; init; }

    public DateTime ReceivedAt { get; init; } = DateTime.Now;
}
