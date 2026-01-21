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

    // Recording Schedule
    public ScheduleMode ScheduleMode { get; init; } = ScheduleMode.Manual;
    public int SunriseOffsetMinutes { get; init; } = -30;  // Before sunrise
    public int SunsetOffsetMinutes { get; init; } = 30;    // After sunset
    public TimeOnly ManualStartTime { get; init; } = new(6, 0);
    public TimeOnly ManualEndTime { get; init; } = new(9, 0);

    // Triggering
    public bool AmplitudeTriggerEnabled { get; init; } = false;
    public int AmplitudeThresholdDb { get; init; } = -40;
    public int PreTriggerSeconds { get; init; } = 2;
    public int PostTriggerSeconds { get; init; } = 5;

    // Power
    public int LowBatteryThresholdPercent { get; init; } = 10;
    public bool AutoStopOnLowBattery { get; init; } = true;
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

public enum ScheduleMode
{
    Manual,
    SunriseSunset,
    Continuous
}
