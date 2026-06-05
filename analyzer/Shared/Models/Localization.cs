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

namespace QuailTracker.Analyzer.Shared.Models;

/// <summary>
/// Represents a TDOA (Time Difference of Arrival) localization result.
/// </summary>
public class Localization
{
    public Guid Id { get; set; } = Guid.NewGuid();

    /// <summary>Estimated source latitude.</summary>
    public double Latitude { get; set; }

    /// <summary>Estimated source longitude.</summary>
    public double Longitude { get; set; }

    /// <summary>Estimated source elevation in meters (if available).</summary>
    public double? Elevation { get; set; }

    /// <summary>Timestamp of the vocalization event.</summary>
    public DateTime Timestamp { get; set; }

    /// <summary>Species detected at this location.</summary>
    public string Species { get; set; } = string.Empty;

    /// <summary>Average confidence across all contributing detections.</summary>
    public double AverageConfidence { get; set; }

    /// <summary>IDs of detections that contributed to this localization.</summary>
    public List<Guid> DetectionIds { get; set; } = [];

    /// <summary>IDs of stations that detected this call.</summary>
    public List<string> StationIds { get; set; } = [];

    /// <summary>Semi-major axis of the confidence ellipse in meters.</summary>
    public double ErrorEllipseMajor { get; set; }

    /// <summary>Semi-minor axis of the confidence ellipse in meters.</summary>
    public double ErrorEllipseMinor { get; set; }

    /// <summary>Rotation of the confidence ellipse in degrees from north.</summary>
    public double ErrorEllipseRotation { get; set; }

    /// <summary>Residual error from the multilateration algorithm.</summary>
    public double ResidualError { get; set; }

    /// <summary>Quality indicator (0-1) based on geometry and signal quality.</summary>
    public double QualityScore { get; set; }

    /// <summary>
    /// True when the time differences were measured by PPS-anchored waveform
    /// cross-correlation (sub-sample). False = coarse detection-timestamp fallback
    /// (one or more recordings lacked a PPS anchor).
    /// </summary>
    public bool PpsRefined { get; set; }

    /// <summary>Compact timing-source label for the UI ("PPS" vs "Coarse").</summary>
    public string TimingSource => PpsRefined ? "PPS" : "Coarse";

    public string QualityLabel => QualityScore switch
    {
        >= 0.8 => "Excellent",
        >= 0.6 => "Good",
        >= 0.4 => "Fair",
        _ => "Poor"
    };

    public string CoordinateString => $"{Latitude:F6}, {Longitude:F6}";

    public override string ToString() =>
        $"{Species} at {CoordinateString} ({QualityLabel})";
}

/// <summary>
/// Represents a matched detection across multiple stations for TDOA processing.
/// </summary>
public class DetectionMatch
{
    public DateTime ReferenceTime { get; set; }
    public string Species { get; set; } = string.Empty;
    public List<(Detection Detection, Station Station, double TimeDifferenceMs)> Detections { get; set; } = [];

    public int StationCount => Detections.Count;
    public bool IsValidForLocalization => StationCount >= 3;
}
