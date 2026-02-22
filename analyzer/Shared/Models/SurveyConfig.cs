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

public enum SurveyType
{
    SpringWhistleCount,
    FallCoveyCount,
    Continuous
}

/// <summary>
/// Configuration for a population survey analysis period.
/// </summary>
public class SurveyConfig
{
    public SurveyType SurveyType { get; set; } = SurveyType.FallCoveyCount;
    public DateTime StartDate { get; set; } = DateTime.Today.AddMonths(-3);
    public DateTime EndDate { get; set; } = DateTime.Today;

    /// <summary>Survey window start relative to sunrise (e.g., -30 min).</summary>
    public TimeSpan WindowStartOffset { get; set; } = TimeSpan.FromMinutes(-30);

    /// <summary>Survey window end relative to sunrise (e.g., +120 min).</summary>
    public TimeSpan WindowEndOffset { get; set; } = TimeSpan.FromMinutes(120);

    /// <summary>Target species scientific name for filtering detections.</summary>
    public string TargetSpecies { get; set; } = Models.TargetSpecies.NorthernBobwhite;

    /// <summary>Radius in meters for deduplicating nearby detections into one individual/covey.</summary>
    public double DeduplicationRadiusMeters { get; set; } = 200;

    /// <summary>Minimum BirdNET confidence to include a detection.</summary>
    public double MinConfidence { get; set; } = 0.5;

    /// <summary>Default covey size for fall population estimation.</summary>
    public int DefaultCoveySize { get; set; } = 12;

    /// <summary>Maximum distance from any station to include a detection (CIP protocol, 0 = disabled).</summary>
    public double TruncationRadiusMeters { get; set; } = 500;

    public string SurveyTypeDisplay => SurveyType switch
    {
        SurveyType.SpringWhistleCount => "Spring Whistle Count",
        SurveyType.FallCoveyCount => "Fall Covey Count",
        SurveyType.Continuous => "Continuous Monitoring",
        _ => SurveyType.ToString()
    };

    public override string ToString() =>
        $"{SurveyTypeDisplay} ({StartDate:yyyy-MM-dd} to {EndDate:yyyy-MM-dd})";
}
