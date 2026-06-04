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

namespace QuailTracker.Analyzer.Shared.Models;

/// <summary>
/// Represents a QuailTracker recording station with GPS location.
/// </summary>
public class Station
{
    public string Id { get; set; } = string.Empty;
    public string Name { get; set; } = string.Empty;
    public double Latitude { get; set; }
    public double Longitude { get; set; }
    public double? Elevation { get; set; }
    public string? Description { get; set; }
    public DateTime? LastRecordingTime { get; set; }
    public int RecordingCount { get; set; }

    /// <summary>
    /// Mic-array orientation in true-north degrees (0-359). The direction the
    /// device's "forward" axis points, read from the MIC_HEADING recording
    /// metadata. Null when unknown. Required to convert a detection's relative
    /// stereo bearing into an absolute bearing for bearing-intersection localization.
    /// </summary>
    public double? MicHeadingDeg { get; set; }

    public bool HasHeading => MicHeadingDeg is >= 0 and < 360;

    public bool HasValidLocation =>
        Latitude >= -90 && Latitude <= 90 &&
        Longitude >= -180 && Longitude <= 180 &&
        (Latitude != 0 || Longitude != 0);

    public override string ToString() =>
        string.IsNullOrEmpty(Name) ? Id : $"{Name} ({Id})";
}
