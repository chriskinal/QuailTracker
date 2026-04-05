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
/// Represents a BirdNet species detection result from audio analysis.
/// </summary>
public class Detection
{
    public Guid Id { get; set; } = Guid.NewGuid();
    public string AudioFilePath { get; set; } = string.Empty;
    public string StationId { get; set; } = string.Empty;

    /// <summary>GPS-synchronized timestamp of the detection.</summary>
    public DateTime Timestamp { get; set; }

    /// <summary>Offset from the start of the audio file in seconds.</summary>
    public double OffsetSeconds { get; set; }

    /// <summary>Duration of the detection segment in seconds (typically 3s for BirdNet).</summary>
    public double DurationSeconds { get; set; } = 3.0;

    /// <summary>Scientific name of the detected species.</summary>
    public string ScientificName { get; set; } = string.Empty;

    /// <summary>Common name of the detected species.</summary>
    public string CommonName { get; set; } = string.Empty;

    /// <summary>BirdNet confidence score (0.0 to 1.0).</summary>
    public double Confidence { get; set; }

    /// <summary>Stereo TDOA bearing in degrees (0=forward, 90=right, -90=left). NaN if not computed.</summary>
    public double BearingDeg { get; set; } = double.NaN;

    /// <summary>TDOA in samples between L and R channels. Positive = sound arrives at R first.</summary>
    public int TdoaSamples { get; set; }

    /// <summary>Cross-correlation peak value (0-1). Higher = more confident bearing.</summary>
    public double TdoaConfidence { get; set; }

    /// <summary>Whether this detection has been selected for TDOA localization.</summary>
    public bool IsSelected { get; set; }

    /// <summary>Link to a localization result if this detection was localized.</summary>
    public Guid? LocalizationId { get; set; }

    public string DisplayName => string.IsNullOrEmpty(CommonName) ? ScientificName : CommonName;
    public string ConfidencePercent => $"{Confidence:P0}";

    public override string ToString() =>
        $"{DisplayName} ({ConfidencePercent}) at {Timestamp:HH:mm:ss}";
}

/// <summary>
/// Common bird species constants for filtering.
/// </summary>
public static class TargetSpecies
{
    public const string NorthernBobwhite = "Colinus virginianus";
    public const string ScaledQuail = "Callipepla squamata";
    public const string GambelQuail = "Callipepla gambelii";
    public const string CaliforniaQuail = "Callipepla californica";
    public const string MontezumaQuail = "Cyrtonyx montezumae";

    public static readonly string[] QuailSpecies =
    [
        NorthernBobwhite,
        ScaledQuail,
        GambelQuail,
        CaliforniaQuail,
        MontezumaQuail
    ];
}
