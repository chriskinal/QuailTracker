/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. See <https://www.gnu.org/licenses/>.
 */

using System;
using System.Collections.Generic;

namespace QuailTracker.Analyzer.Shared.Models;

/// <summary>How a single quality metric scores, for the report-card colour.</summary>
public enum QualityRating { Good, Fair, Poor }

/// <summary>Severity of an improvement suggestion.</summary>
public enum QualitySeverity { Good, Info, Warning, Critical }

/// <summary>One row in the quality report card: a named metric, its formatted
/// value, a Good/Fair/Poor rating, and a 0..1 fraction for a bar.</summary>
public sealed class QualityMetric
{
    public string Name { get; init; } = "";
    public string Value { get; init; } = "";
    public QualityRating Rating { get; init; }
    public double BarFraction { get; init; }   // 0..1, for the report-card bar

    /// <summary>Hex colour for the rating pill/bar (bound directly in XAML).</summary>
    public string RatingColor => Rating switch
    {
        QualityRating.Good => "#4CAF50",
        QualityRating.Fair => "#FF9800",
        _                  => "#F44336",
    };
    public double BarPercent => BarFraction * 100.0;
}

/// <summary>An actionable suggestion derived from the metrics.</summary>
public sealed class QualitySuggestion
{
    public QualitySeverity Severity { get; init; }
    public string Category { get; init; } = "";
    public string Message { get; init; } = "";
    public string Icon => Severity switch
    {
        QualitySeverity.Critical => "⛔",
        QualitySeverity.Warning  => "⚠",
        QualitySeverity.Info      => "ℹ",
        _                          => "✓",
    };
    public string Color => Severity switch
    {
        QualitySeverity.Critical => "#F44336",
        QualitySeverity.Warning  => "#FF9800",
        QualitySeverity.Info      => "#2196F3",
        _                          => "#4CAF50",
    };
}

/// <summary>Result of a DSP quality analysis of one recording: the raw metrics,
/// the report-card rows, an overall 0..100 score, and the suggestions.</summary>
public sealed class QualityReport
{
    public bool IsStereo { get; init; }
    public int SampleRate { get; init; }
    public double DurationSeconds { get; init; }

    // Raw metrics (kept for callers/exports; the UI binds the Metrics rows).
    public double PeakDbfs { get; init; }
    public double RmsDbfs { get; init; }
    public double HeadroomDb { get; init; }
    public double CrestFactorDb { get; init; }
    public double ClippedPercent { get; init; }
    public double NoiseFloorDbfs { get; init; }
    public double SnrDb { get; init; }
    public double DcOffsetDbfs { get; init; }
    public double ChannelImbalanceDb { get; init; }   // NaN if mono
    public double WindEnergyPercent { get; init; }     // < 300 Hz share of energy
    public double BirdBandEnergyPercent { get; init; } // 1–5 kHz share
    public double ActivityPercent { get; init; }       // share of frames above noise
    public int DropoutCount { get; init; }

    public int Score { get; init; }                    // 0..100 composite

    public IReadOnlyList<QualityMetric> Metrics { get; init; } = Array.Empty<QualityMetric>();
    public IReadOnlyList<QualitySuggestion> Suggestions { get; init; } = Array.Empty<QualitySuggestion>();

    public QualityRating OverallRating =>
        Score >= 75 ? QualityRating.Good : Score >= 50 ? QualityRating.Fair : QualityRating.Poor;
}
