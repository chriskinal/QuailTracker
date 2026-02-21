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
/// Device configuration settings.
/// </summary>
public record DeviceConfig
{
    // Station Identity
    public string StationId { get; init; } = "QT001";

    // Audio Settings
    public GainLevel Gain { get; init; } = GainLevel.Medium;
    public HighPassFilter HighPassFilter { get; init; } = HighPassFilter.Hz8;
    public int SampleRate { get; init; } = 48000;
    public RecordingFormat Format { get; init; } = RecordingFormat.FLAC;

    // Sunrise/Sunset Schedule
    public bool SunriseEnabled { get; init; } = true;
    public int SunriseBeforeMinutes { get; init; } = 30;
    public int SunriseAfterMinutes { get; init; } = 60;
    public bool SunsetEnabled { get; init; } = true;
    public int SunsetBeforeMinutes { get; init; } = 30;
    public int SunsetAfterMinutes { get; init; } = 30;

    // Freeform Recording Windows
    public TimeWindow[] FreeformWindows { get; init; } = [];

    // Triggering
    public bool AmplitudeTriggerEnabled { get; init; } = false;
    public int AmplitudeThresholdDb { get; init; } = -40;
    public int PreTriggerSeconds { get; init; } = 2;
    public int PostTriggerSeconds { get; init; } = 5;

    // Power
    public int LowBatteryThresholdPercent { get; init; } = 10;
    public bool AutoStopOnLowBattery { get; init; } = true;

    // Surveyed Position
    public double SurveyLatitude { get; init; }
    public double SurveyLongitude { get; init; }
    public float SurveyAltitude { get; init; }
    public int SurveyCount { get; init; }
}

public enum GainLevel
{
    Low,
    LowMedium,
    Medium,
    MediumHigh,
    High
}

public enum HighPassFilter
{
    Disabled,
    Hz8,
    Hz48
}

public enum RecordingFormat
{
    FLAC,
    WAV
}

public record TimeWindow(TimeOnly Start, TimeOnly End);
