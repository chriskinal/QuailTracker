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
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Mapsui.UI.Avalonia;
using QuailTracker.Acoustics;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

/// <summary>One row of the coverage sweep: percent-of-area within target per method.</summary>
public sealed record CoverageRow(int Stations, string Tdoa, string Bearing, string Fusion);

/// <summary>
/// Deployment Planner tab: draw a study-area boundary on the map, then auto-place a
/// perimeter ring of PPS stations and score coverage (CRLB) across station counts for
/// TDOA / Bearing / Fusion.
/// </summary>
public partial class DeploymentPlannerViewModel : ObservableObject
{
    private const double SqMetersPerHectare = 10_000.0;
    private const double SqMetersPerAcre = 4046.8564224;

    // Coverage sweep parameters (mirror the qt-planner CLI defaults).
    private const int MinStations = 3, MaxStations = 8, GridRes = 41;
    private const double CoverageGoal = 0.90;

    private readonly DeploymentPlannerMapService _mapService;

    /// <summary>Latest drawn study-area ring (WGS84, closed), or null when there's no usable polygon.</summary>
    private IReadOnlyList<(double Lat, double Lon)>? _currentRing;

    /// <summary>Fusion coverage of the current planned ring — the baseline tweaks are measured against.</summary>
    private double _optimalFusionCoverage;

    /// <summary>Fusion coverage per station count from the last sweep (drives the baseline when N changes).</summary>
    private Dictionary<int, double> _fusionByN = new();

    /// <summary>Guards against re-placing while we set <see cref="SelectedRow"/> programmatically.</summary>
    private bool _suppressRowSelect;

    [ObservableProperty]
    private string _statusMessage =
        "Click “Draw Area”, then tap the map to outline your study area. Double-tap to finish.";

    [ObservableProperty]
    private string _areaSummary = "No study area drawn yet.";

    [ObservableProperty]
    private string _planSummary = string.Empty;

    /// <summary>Live "how far off optimal" readout shown after a station is dragged.</summary>
    [ObservableProperty]
    private string _tweakSummary = string.Empty;

    /// <summary>True once a usable polygon (≥3 corners) exists — gates the "Plan Stations" step.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(PlanStationsCommand))]
    private bool _hasArea;

    /// <summary>True once stations are placed — gates the "Move Stations" step.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(MoveStationsCommand))]
    private bool _hasPlan;

    /// <summary>Selected coverage-table row — picking a row re-places the layout at that station count.</summary>
    [ObservableProperty]
    private CoverageRow? _selectedRow;

    /// <summary>Coverage-per-method by station count, for the results table.</summary>
    public ObservableCollection<CoverageRow> CoverageRows { get; } = [];

    public DeploymentPlannerViewModel(DeploymentPlannerMapService mapService)
    {
        _mapService = mapService;
        _mapService.AreaChanged += OnAreaChanged;
        _mapService.StationsMoved += OnStationsMoved;
        _mapService.MoveStatus += msg => StatusMessage = msg;
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

    [RelayCommand(CanExecute = nameof(CanPlanStations))]
    private async Task PlanStationsAsync()
    {
        var ring = _currentRing;
        if (ring is null) return;

        StatusMessage = "Computing station layout…";
        var (rows, recommendedN, fusionByN, summary) = await Task.Run(() => ComputePlan(ring));

        CoverageRows.Clear();
        foreach (var r in rows) CoverageRows.Add(r);
        _fusionByN = fusionByN;
        PlanSummary = $"{summary} Click a row to place that many.";

        // Select the recommended row (highlights it) without re-triggering placement, then place.
        _suppressRowSelect = true;
        SelectedRow = CoverageRows.FirstOrDefault(r => r.Stations == recommendedN);
        _suppressRowSelect = false;
        PlaceStations(recommendedN);
    }

    private bool CanPlanStations() => HasArea;

    /// <summary>Selecting a coverage-table row re-places the ring at that station count.</summary>
    partial void OnSelectedRowChanged(CoverageRow? value)
    {
        if (_suppressRowSelect || value is null) return;
        PlaceStations(value.Stations);
    }

    /// <summary>Place a perimeter ring of <paramref name="n"/> stations and update the baseline + map.</summary>
    private void PlaceStations(int n)
    {
        var ring = _currentRing;
        if (ring is null || n < 3) return;

        var proj = new GeoProjection(ring.Average(p => p.Lat), ring.Average(p => p.Lon));
        var polygon = ring.Select(p => proj.ToLocal(p.Lat, p.Lon)).ToList();
        var stations = PerimeterLayout.Ring(polygon, n)
            .Select(s => { var (lat, lon) = proj.ToGeo(s.X, s.Y); return (lat, lon, s.HeadingDeg); })
            .ToList();

        _mapService.ShowStations(stations);
        _mapService.FinishEditing();   // leave edit mode so stray taps don't extend the polygon
        _optimalFusionCoverage = _fusionByN.TryGetValue(n, out var f) ? f : 0;
        HasPlan = stations.Count >= 3;
        TweakSummary = HasPlan
            ? $"Optimal {n}-station layout: {_optimalFusionCoverage:P0} Fusion coverage. Click “Move Stations”, then tap a station and its real-world spot to see the cost."
            : string.Empty;
        StatusMessage = $"Placed {n} stations. Pick another row to change the count, or move them to real-world spots.";
    }

    [RelayCommand(CanExecute = nameof(CanMoveStations))]
    private void MoveStations()
    {
        _mapService.MoveStations();
        StatusMessage = "Tap a station to pick it up, then tap its new location — coverage vs. optimal updates live.";
    }

    private bool CanMoveStations() => HasPlan;

    private void OnStationsMoved(IReadOnlyList<(double Lat, double Lon)> stations)
    {
        var ring = _currentRing;
        if (ring is null || stations.Count < 3)
        {
            TweakSummary = "Need ≥3 stations for a fix.";
            return;
        }

        var current = EvaluateFusionCoverage(ring, stations);
        var deltaPts = (_optimalFusionCoverage - current) * 100.0;
        var verdict = deltaPts <= 0.5 ? "as good as optimal"
            : deltaPts <= 5 ? $"{deltaPts:F0} pts below optimal"
            : $"{deltaPts:F0} pts below optimal — consider a better spot";
        TweakSummary = $"Your layout: {current:P0} Fusion coverage ({verdict}; optimal {_optimalFusionCoverage:P0}).";
    }

    private void OnAreaChanged(IReadOnlyList<(double Lat, double Lon)>? ring)
    {
        // The ring is closed (last == first), so distinct corners = count - 1.
        var corners = ring is { Count: >= 4 } ? ring.Count - 1 : 0;
        if (corners < 3)
        {
            _currentRing = null;
            HasArea = false;
            AreaSummary = "No study area drawn yet.";
            ClearPlan();
            return;
        }

        _currentRing = ring;
        double m2 = PolygonAreaSqMeters(ring!);
        AreaSummary = $"Study area: {m2 / SqMetersPerHectare:F1} ha "
                    + $"({m2 / SqMetersPerAcre:F1} ac) · {corners} corners";
        HasArea = true;

        // The geometry changed, so any existing plan is stale.
        ClearPlan();
    }

    private void ClearPlan()
    {
        HasPlan = false;
        TweakSummary = string.Empty;
        _optimalFusionCoverage = 0;
        _fusionByN = new();
        if (CoverageRows.Count == 0 && PlanSummary.Length == 0) return;
        _suppressRowSelect = true;
        CoverageRows.Clear();      // clears SelectedRow → guarded handler won't re-place
        _suppressRowSelect = false;
        PlanSummary = string.Empty;
        _mapService.ClearStations();
    }

    /// <summary>Fusion coverage for an arbitrary set of station positions (headings re-aimed at the area centroid).</summary>
    private static double EvaluateFusionCoverage(
        IReadOnlyList<(double Lat, double Lon)> ring, IReadOnlyList<(double Lat, double Lon)> stationLatLon)
    {
        if (stationLatLon.Count < 3) return 0;
        var proj = new GeoProjection(ring.Average(p => p.Lat), ring.Average(p => p.Lon));
        var polygon = ring.Select(p => proj.ToLocal(p.Lat, p.Lon)).ToList();
        double cx = polygon.Average(p => p.X), cy = polygon.Average(p => p.Y);

        var stns = stationLatLon.Select(s =>
        {
            var (x, y) = proj.ToLocal(s.Lat, s.Lon);
            var heading = (Math.Atan2(cx - x, cy - y) * 180.0 / Math.PI + 360) % 360;
            return new ArrayStation(x, y, heading);
        }).ToArray();

        return AreaModel.EvaluatePolygon(stns, LocalizationMethod.Fusion, new LocalizationParams(), polygon, GridRes).Coverage;
    }

    /// <summary>
    /// Sweep station counts over the drawn polygon: place a perimeter ring, score CRLB coverage
    /// per method, and pick the smallest count meeting the Fusion goal. Returns the table, the
    /// recommended count, the per-count Fusion coverage, and a summary. Pure / off the UI thread.
    /// </summary>
    private static (List<CoverageRow> Rows, int RecommendedN, Dictionary<int, double> FusionByN, string Summary)
        ComputePlan(IReadOnlyList<(double Lat, double Lon)> ring)
    {
        var proj = new GeoProjection(ring.Average(p => p.Lat), ring.Average(p => p.Lon));
        var polygon = ring.Select(p => proj.ToLocal(p.Lat, p.Lon)).ToList();
        var prm = new LocalizationParams();

        var rows = new List<CoverageRow>();
        var fusionByN = new Dictionary<int, double>();
        int? bestFusionN = null;
        for (var n = MinStations; n <= MaxStations; n++)
        {
            var stns = PerimeterLayout.Ring(polygon, n);
            var t = AreaModel.EvaluatePolygon(stns, LocalizationMethod.Tdoa, prm, polygon, GridRes);
            var b = AreaModel.EvaluatePolygon(stns, LocalizationMethod.Bearing, prm, polygon, GridRes);
            var f = AreaModel.EvaluatePolygon(stns, LocalizationMethod.Fusion, prm, polygon, GridRes);
            rows.Add(new CoverageRow(n, Cell(t), Cell(b), Cell(f)));
            fusionByN[n] = f.Coverage;
            if (bestFusionN is null && f.Coverage >= CoverageGoal) bestFusionN = n;
        }

        var pickN = bestFusionN ?? MaxStations;

        var summary = bestFusionN is null
            ? $"No layout up to {MaxStations} stations reaches {CoverageGoal:P0} (Fusion). Showing {pickN}."
            : $"Recommended: {pickN} stations — Fusion ≥ {CoverageGoal:P0} coverage.";

        return (rows, pickN, fusionByN, summary);
    }

    private static string Cell(AreaResult r) => r.FixCount == 0 ? "—" : $"{r.Coverage:P0}";

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
