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
/// One cell of the site x visit count matrix: a single station on a single day.
/// </summary>
public class DailyCount
{
    public string StationId { get; set; } = string.Empty;
    public DateTime Date { get; set; }

    /// <summary>Deduplicated count of individuals/coveys detected.</summary>
    public int Count { get; set; }

    /// <summary>Raw number of BirdNET detections before deduplication.</summary>
    public int RawDetectionCount { get; set; }

    // Weather covariates (populated when weather data is fetched)
    public double? Temperature { get; set; }
    public double? WindSpeed { get; set; }
    public double? CloudCover { get; set; }
    public double? Precipitation { get; set; }

    public string TemperatureDisplay => Temperature.HasValue ? $"{Temperature:F1}" : "-";
    public string WindSpeedDisplay => WindSpeed.HasValue ? $"{WindSpeed:F1}" : "-";
    public string CloudCoverDisplay => CloudCover.HasValue ? $"{CloudCover:F0}%" : "-";

    public override string ToString() =>
        $"{StationId} {Date:yyyy-MM-dd}: {Count} (raw: {RawDetectionCount})";
}
