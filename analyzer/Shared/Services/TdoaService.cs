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
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Service for TDOA (Time Difference of Arrival) localization using hyperbolic multilateration.
/// </summary>
public class TdoaService : ITdoaService
{
    private const double EarthRadiusMeters = 6371000;
    private const double DegToRad = Math.PI / 180.0;
    private const double RadToDeg = 180.0 / Math.PI;

    public double SpeedOfSound { get; set; } = 343.0; // m/s at ~20°C
    public double MaxTimeDifferenceMs { get; set; } = 3000; // 3 seconds

    public IReadOnlyList<DetectionMatch> MatchDetections(
        IReadOnlyList<Detection> detections,
        IReadOnlyList<Station> stations)
    {
        var matches = new List<DetectionMatch>();
        var stationDict = stations.ToDictionary(s => s.Id);

        // Group detections by species
        var bySpecies = detections.GroupBy(d => d.ScientificName);

        foreach (var speciesGroup in bySpecies)
        {
            var speciesDetections = speciesGroup.OrderBy(d => d.Timestamp).ToList();

            // Find clusters of detections within time window
            var used = new HashSet<Guid>();

            foreach (var detection in speciesDetections)
            {
                if (used.Contains(detection.Id)) continue;
                if (!stationDict.TryGetValue(detection.StationId, out var station)) continue;
                if (!station.HasValidLocation) continue;

                // Find all detections within time window from different stations
                var cluster = new List<(Detection d, Station s)> { (detection, station) };

                foreach (var other in speciesDetections)
                {
                    if (other.Id == detection.Id) continue;
                    if (used.Contains(other.Id)) continue;
                    if (other.StationId == detection.StationId) continue;
                    if (!stationDict.TryGetValue(other.StationId, out var otherStation)) continue;
                    if (!otherStation.HasValidLocation) continue;

                    var timeDiffMs = Math.Abs((other.Timestamp - detection.Timestamp).TotalMilliseconds);
                    if (timeDiffMs <= MaxTimeDifferenceMs)
                    {
                        cluster.Add((other, otherStation));
                    }
                }

                // Only create match if we have at least 3 stations
                if (cluster.Count >= 3)
                {
                    var referenceTime = cluster.Min(c => c.d.Timestamp);

                    var match = new DetectionMatch
                    {
                        ReferenceTime = referenceTime,
                        Species = speciesGroup.Key,
                        Detections = cluster.Select(c =>
                            (c.d, c.s, (c.d.Timestamp - referenceTime).TotalMilliseconds)
                        ).ToList()
                    };

                    matches.Add(match);

                    foreach (var (d, _) in cluster)
                    {
                        used.Add(d.Id);
                    }
                }
            }
        }

        return matches;
    }

    public async Task<Localization?> LocalizeAsync(DetectionMatch match, CancellationToken ct = default)
    {
        if (!match.IsValidForLocalization)
            return null;

        return await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            // Use least squares hyperbolic multilateration
            var stations = match.Detections.Select(d => d.Station).ToList();
            var timeDiffs = match.Detections.Select(d => d.TimeDifferenceMs / 1000.0).ToList();

            // Initial guess: centroid of stations
            var centerLat = stations.Average(s => s.Latitude);
            var centerLon = stations.Average(s => s.Longitude);

            // Iterative least squares optimization
            var (lat, lon, residual) = OptimizeLocation(
                centerLat, centerLon,
                stations.ToArray(),
                timeDiffs.ToArray());

            // Calculate confidence ellipse
            var (majorAxis, minorAxis, rotation) = CalculateErrorEllipse(
                lat, lon, stations.ToArray(), timeDiffs.ToArray());

            // Quality score based on geometry and residual
            var qualityScore = CalculateQualityScore(
                stations.ToArray(), lat, lon, residual);

            return new Localization
            {
                Latitude = lat,
                Longitude = lon,
                Timestamp = match.ReferenceTime,
                Species = match.Species,
                AverageConfidence = match.Detections.Average(d => d.Detection.Confidence),
                DetectionIds = match.Detections.Select(d => d.Detection.Id).ToList(),
                StationIds = match.Detections.Select(d => d.Station.Id).ToList(),
                ErrorEllipseMajor = majorAxis,
                ErrorEllipseMinor = minorAxis,
                ErrorEllipseRotation = rotation,
                ResidualError = residual,
                QualityScore = qualityScore
            };
        }, ct);
    }

    public async Task<IReadOnlyList<Localization>> LocalizeAllAsync(
        IReadOnlyList<DetectionMatch> matches,
        IProgress<(int current, int total)>? progress = null,
        CancellationToken ct = default)
    {
        var results = new List<Localization>();

        for (var i = 0; i < matches.Count; i++)
        {
            ct.ThrowIfCancellationRequested();
            progress?.Report((i + 1, matches.Count));

            var localization = await LocalizeAsync(matches[i], ct);
            if (localization != null)
            {
                results.Add(localization);
            }
        }

        return results;
    }

    public double CalculateDistance(double lat1, double lon1, double lat2, double lon2)
    {
        // Haversine formula
        var dLat = (lat2 - lat1) * DegToRad;
        var dLon = (lon2 - lon1) * DegToRad;

        var a = Math.Sin(dLat / 2) * Math.Sin(dLat / 2) +
                Math.Cos(lat1 * DegToRad) * Math.Cos(lat2 * DegToRad) *
                Math.Sin(dLon / 2) * Math.Sin(dLon / 2);

        var c = 2 * Math.Atan2(Math.Sqrt(a), Math.Sqrt(1 - a));

        return EarthRadiusMeters * c;
    }

    private (double lat, double lon, double residual) OptimizeLocation(
        double initialLat, double initialLon,
        Station[] stations, double[] timeDiffs)
    {
        var lat = initialLat;
        var lon = initialLon;
        var residual = double.MaxValue;

        // Gradient descent with decreasing step size
        var stepSize = 0.001; // degrees

        for (var iteration = 0; iteration < 1000 && stepSize > 1e-8; iteration++)
        {
            var currentResidual = CalculateResidual(lat, lon, stations, timeDiffs);

            if (currentResidual < residual)
            {
                residual = currentResidual;
            }

            // Calculate gradient numerically
            var gradLat = (CalculateResidual(lat + stepSize, lon, stations, timeDiffs) -
                          CalculateResidual(lat - stepSize, lon, stations, timeDiffs)) / (2 * stepSize);
            var gradLon = (CalculateResidual(lat, lon + stepSize, stations, timeDiffs) -
                          CalculateResidual(lat, lon - stepSize, stations, timeDiffs)) / (2 * stepSize);

            // Update position
            var newLat = lat - stepSize * gradLat;
            var newLon = lon - stepSize * gradLon;

            var newResidual = CalculateResidual(newLat, newLon, stations, timeDiffs);

            if (newResidual < residual)
            {
                lat = newLat;
                lon = newLon;
                residual = newResidual;
            }
            else
            {
                stepSize *= 0.5;
            }
        }

        return (lat, lon, residual);
    }

    private double CalculateResidual(double lat, double lon, Station[] stations, double[] timeDiffs)
    {
        // Reference is the first station (time diff = 0)
        var distances = stations.Select(s => CalculateDistance(lat, lon, s.Latitude, s.Longitude)).ToArray();

        // Expected time differences based on distances
        var expectedTimeDiffs = distances.Select(d => d / SpeedOfSound).ToArray();

        // Subtract reference distance to get relative time differences
        var refDist = expectedTimeDiffs[0];
        for (var i = 0; i < expectedTimeDiffs.Length; i++)
        {
            expectedTimeDiffs[i] -= refDist;
        }

        // Calculate sum of squared errors
        var error = 0.0;
        for (var i = 0; i < timeDiffs.Length; i++)
        {
            var diff = expectedTimeDiffs[i] - timeDiffs[i];
            error += diff * diff;
        }

        return error;
    }

    private (double major, double minor, double rotation) CalculateErrorEllipse(
        double lat, double lon, Station[] stations, double[] timeDiffs)
    {
        // Simplified error estimation based on station geometry
        var distances = stations.Select(s => CalculateDistance(lat, lon, s.Latitude, s.Longitude)).ToArray();
        var avgDistance = distances.Average();

        // GDOP-like calculation
        var bearings = stations.Select(s =>
            Math.Atan2(s.Longitude - lon, s.Latitude - lat) * RadToDeg).ToArray();

        var bearingSpread = CalculateBearingSpread(bearings);

        // Scale error based on time sync accuracy (~1ms = ~0.343m)
        var baseError = 0.343 * 1000; // 1ms timing error
        var geometryFactor = 1.0 / Math.Max(bearingSpread / 180.0, 0.1);

        var majorAxis = baseError * geometryFactor;
        var minorAxis = baseError * geometryFactor * 0.5;

        // Rotation based on dominant bearing direction
        var rotation = bearings.Average();

        return (majorAxis, minorAxis, rotation);
    }

    private double CalculateBearingSpread(double[] bearings)
    {
        if (bearings.Length < 2) return 0;

        var sorted = bearings.OrderBy(b => b).ToArray();
        var maxGap = 0.0;

        for (var i = 0; i < sorted.Length; i++)
        {
            var next = (i + 1) % sorted.Length;
            var gap = sorted[next] - sorted[i];
            if (next == 0) gap += 360;
            maxGap = Math.Max(maxGap, gap);
        }

        return 360 - maxGap;
    }

    private double CalculateQualityScore(Station[] stations, double lat, double lon, double residual)
    {
        // Quality based on:
        // 1. Number of stations (more is better)
        // 2. Station geometry (spread is better)
        // 3. Residual error (lower is better)

        var stationScore = Math.Min(stations.Length / 6.0, 1.0);

        var bearings = stations.Select(s =>
            Math.Atan2(s.Longitude - lon, s.Latitude - lat) * RadToDeg).ToArray();
        var geometryScore = Math.Min(CalculateBearingSpread(bearings) / 270.0, 1.0);

        var residualScore = Math.Max(0, 1.0 - residual / 10.0);

        return (stationScore + geometryScore + residualScore) / 3.0;
    }
}
