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

namespace QuailTracker.Shared.Models;

/// <summary>
/// Complete device status from QuailTracker unit.
/// </summary>
public record DeviceStatus
{
    // Device Info
    public string StationId { get; init; } = "Unknown";
    public string FirmwareVersion { get; init; } = "0.0.0";

    // Battery
    public float BatteryVoltage { get; init; }
    public int BatteryPercentage { get; init; }
    public BatteryLevel BatteryLevel { get; init; }

    // GPS
    public bool GpsValid { get; init; }
    public int GpsSatellites { get; init; }
    public double Latitude { get; init; }
    public double Longitude { get; init; }
    public float Altitude { get; init; }
    public bool PpsValid { get; init; }
    public DateTime? GpsTime { get; init; }

    // Environment
    public float Temperature { get; init; }
    public float Humidity { get; init; }

    // SD Card
    public bool SdCardMounted { get; init; }
    public long SdTotalBytes { get; init; }
    public long SdUsedBytes { get; init; }
    public long SdFreeBytes { get; init; }

    // Recording
    public bool IsRecording { get; init; }
    public string? CurrentFilename { get; init; }
    public long CurrentFileSize { get; init; }
    public uint SamplesCaptured { get; init; }
    public uint BufferOverflows { get; init; }

    // Audio
    public int PeakLevel { get; init; }
    public int BufferUsed { get; init; }
    public int BufferCapacity { get; init; }

    // Timestamp
    public DateTime LastUpdated { get; init; } = DateTime.Now;
}

public enum BatteryLevel
{
    Ok,
    Low,
    Critical
}
