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
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Mapsui.UI.Avalonia;
using QuailTracker.Acoustics;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

/// <summary>
/// Deployment Planner tab: draw a study-area boundary on the map, then (later phases)
/// auto-place PPS stations and score coverage. Phase 3 covers drawing the polygon and
/// reporting its size.
/// </summary>
public partial class DeploymentPlannerViewModel : ObservableObject
{
    private const double SqMetersPerHectare = 10_000.0;
    private const double SqMetersPerAcre = 4046.8564224;

    private readonly DeploymentPlannerMapService _mapService;

    [ObservableProperty]
    private string _statusMessage =
        "Click “Draw Area”, then tap the map to outline your study area. Double-tap to finish.";

    [ObservableProperty]
    private string _areaSummary = "No study area drawn yet.";

    /// <summary>True once a usable polygon (≥3 corners) exists — gates the later "plan" step.</summary>
    [ObservableProperty]
    private bool _hasArea;

    public DeploymentPlannerViewModel(DeploymentPlannerMapService mapService)
    {
        _mapService = mapService;
        _mapService.AreaChanged += OnAreaChanged;
    }

    public async Task InitializeMapAsync(MapControl mapControl)
        => await _mapService.InitializeAsync(mapControl);

    [RelayCommand]
    private void DrawArea()
    {
        _mapService.StartDrawArea();
        StatusMessage = "Tap the map to drop corner points; double-tap the last point to finish.";
    }

    [RelayCommand]
    private void EditArea()
    {
        _mapService.EditArea();
        StatusMessage = "Drag a corner to move it · double-tap (or long-press) a corner to delete it · tap an edge to add one.";
    }

    [RelayCommand]
    private void ClearArea()
    {
        _mapService.ClearArea();
        StatusMessage = "Cleared. Click “Draw Area” to start again.";
    }

    private void OnAreaChanged(IReadOnlyList<(double Lat, double Lon)>? ring)
    {
        // The ring is closed (last == first), so distinct corners = count - 1.
        var corners = ring is { Count: >= 4 } ? ring.Count - 1 : 0;
        if (corners < 3)
        {
            HasArea = false;
            AreaSummary = "No study area drawn yet.";
            return;
        }

        double m2 = PolygonAreaSqMeters(ring!);
        AreaSummary = $"Study area: {m2 / SqMetersPerHectare:F1} ha "
                    + $"({m2 / SqMetersPerAcre:F1} ac) · {corners} corners";
        HasArea = true;
    }

    /// <summary>
    /// Planar area of a lat/lon ring, via the shared local-ENU projection (centred on the
    /// ring centroid) + the shoelace formula. Accurate for survey-area-sized polygons.
    /// </summary>
    private static double PolygonAreaSqMeters(IReadOnlyList<(double Lat, double Lon)> ring)
    {
        var proj = new GeoProjection(ring.Average(p => p.Lat), ring.Average(p => p.Lon));
        var pts = ring.Select(p => proj.ToLocal(p.Lat, p.Lon)).ToList();

        double sum = 0;
        for (var i = 0; i < pts.Count - 1; i++)
            sum += pts[i].X * pts[i + 1].Y - pts[i + 1].X * pts[i].Y;
        return Math.Abs(sum) / 2.0;
    }
}
