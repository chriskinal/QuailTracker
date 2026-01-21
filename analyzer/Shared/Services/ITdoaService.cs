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
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Service for TDOA (Time Difference of Arrival) localization.
/// </summary>
public interface ITdoaService
{
    /// <summary>
    /// Speed of sound in meters per second (at ~20°C in air).
    /// </summary>
    double SpeedOfSound { get; set; }

    /// <summary>
    /// Maximum time difference (ms) to consider detections as the same call event.
    /// </summary>
    double MaxTimeDifferenceMs { get; set; }

    /// <summary>
    /// Matches detections from multiple stations that likely represent the same call.
    /// </summary>
    IReadOnlyList<DetectionMatch> MatchDetections(
        IReadOnlyList<Detection> detections,
        IReadOnlyList<Station> stations);

    /// <summary>
    /// Performs multilateration to estimate source location from a detection match.
    /// Requires at least 3 stations.
    /// </summary>
    Task<Localization?> LocalizeAsync(
        DetectionMatch match,
        CancellationToken ct = default);

    /// <summary>
    /// Processes all matches and returns localization results.
    /// </summary>
    Task<IReadOnlyList<Localization>> LocalizeAllAsync(
        IReadOnlyList<DetectionMatch> matches,
        IProgress<(int current, int total)>? progress = null,
        CancellationToken ct = default);

    /// <summary>
    /// Calculates the distance between two geographic points in meters.
    /// </summary>
    double CalculateDistance(double lat1, double lon1, double lat2, double lon2);
}
