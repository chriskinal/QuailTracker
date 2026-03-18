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
    public string StationId { get; set; } = "Unknown";
    public string FirmwareVersion { get; set; } = "0.0.0";

    // Battery
    public float BatteryVoltage { get; set; }
    public int BatteryPercentage { get; set; }
    public BatteryLevel BatteryLevel { get; set; }

    // GPS
    public bool GpsValid { get; set; }
    public int GpsFixType { get; set; }
    public int GpsSatellites { get; set; }
    public double Latitude { get; set; }
    public double Longitude { get; set; }
    public float Altitude { get; set; }
    public string GpsDate { get; set; } = "";
    public bool PpsValid { get; set; }
    public long PpsCount { get; set; }
    public long PpsAgeMs { get; set; }
    public DateTime? GpsTime { get; set; }

    // Environment
    public float Temperature { get; set; }
    public float Humidity { get; set; }

    // SD Card
    public bool SdCardMounted { get; set; }
    public long SdTotalBytes { get; set; }
    public long SdUsedBytes { get; set; }
    public long SdFreeBytes { get; set; }

    // Recording
    public bool IsRecording { get; set; }
    public string? CurrentFilename { get; set; }
    public long CurrentFileSize { get; set; }
    public uint SamplesCaptured { get; set; }
    public uint BufferOverflows { get; set; }

    // Audio
    public int PeakLevel { get; set; }
    public int BufferUsed { get; set; }
    public int BufferCapacity { get; set; }
    public uint LimiterClipCount { get; set; }
    public int ActivityRatio { get; set; }

    // BLE Module
    public bool BleModuleReady { get; set; }
    public string BleModuleName { get; set; } = "";
    public string BleModuleAddr { get; set; } = "";
    public bool BleConnected { get; set; }

    // Survey-In
    public double SurveyLatitude { get; set; }
    public double SurveyLongitude { get; set; }
    public float SurveyAltitude { get; set; }
    public int SurveyCount { get; set; }
    public bool SurveyActive { get; set; }
    public int SurveySecondsLeft { get; set; }

    // Detection / Inference
    public bool DetectionActive { get; set; }
    public long DetectionWindows { get; set; }
    public long DetectionHits { get; set; }
    public string DetectionLastSpecies { get; set; } = "";
    public bool ModelLoaded { get; set; }
    public long ModelSize { get; set; }
    public int ModelLabels { get; set; }

    // Timestamp
    public DateTime LastUpdated { get; set; } = DateTime.Now;
}

public enum BatteryLevel
{
    Ok,
    Low,
    Critical
}
