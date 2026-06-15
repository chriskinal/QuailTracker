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

/// <summary>Localization sweep row: percent-of-area localizable per method, by station count.</summary>
public sealed record CoverageRow(int Stations, string Tdoa, string Bearing, string Fusion);

/// <summary>Detection sweep row: percent-of-area within ≥1 mic hemisphere, by station count.</summary>
public sealed record DetectionRow(int Stations, string Coverage);

/// <summary>
/// Deployment Planner tab: draw a study-area boundary, then either
///   - Localization: auto-place a perimeter ring and score CRLB coverage (TDOA/Bearing/Fusion), or
///   - Detection: greedily spread stations to maximise ±90°-hemisphere coverage (≥1 mic hears it),
/// pick a station count from the table, and move stations to real-world spots with a live
/// "how far off optimal" readout.
/// </summary>
public partial class DeploymentPlannerViewModel : ObservableObject
{
    private const double SqMetersPerHectare = 10_000.0;
    private const double SqMetersPerAcre = 4046.8564224;

    private const int MinStations = 3, GridRes = 41;
    private const double LocalizationGoal = 0.90;   // Fusion coverage target
    private const double DetectionGoal = 0.95;      // ≥1-hemisphere coverage target

    private readonly DeploymentPlannerMapService _mapService;

    /// <summary>Latest drawn study-area ring (WGS84, closed), or null when there's no usable polygon.</summary>
    private IReadOnlyList<(double Lat, double Lon)>? _currentRing;

    /// <summary>Coverage of the current planned layout — the baseline tweaks are measured against.</summary>
    private double _optimalCoverage;

    /// <summary>Coverage by station count from the last sweep (Fusion% or detection% per mode).</summary>
    private Dictionary<int, double> _coverageByN = new();

    /// <summary>Detection-mode greedy layout (full, up to MaxStations); first N = best N-station spread.</summary>
    private List<(double Lat, double Lon, double HeadingDeg)>? _greedyLayout;

    private bool _suppressRowSelect;   // guards programmatic table selection

    [ObservableProperty]
    private string _statusMessage =
        "Click “Draw Area”, then tap the map to outline your study area. Double-tap to finish.";

    [ObservableProperty]
    private string _areaSummary = "No study area drawn yet.";

    [ObservableProperty]
    private string _planSummary = string.Empty;

    /// <summary>Live "how far off optimal" readout shown after a station is moved.</summary>
    [ObservableProperty]
    private string _tweakSummary = string.Empty;

    /// <summary>Detection mode (≥1-hemisphere) vs Localization mode (≥3-station CRLB).</summary>
    [ObservableProperty]
    private bool _optimizeForDetection;

    /// <summary>Upper bound of the station-count sweep — how many units are available to deploy.</summary>
    [ObservableProperty]
    private int _maxStations = 12;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(PlanStationsCommand))]
    private bool _hasArea;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(MoveStationsCommand))]
    private bool _hasPlan;

    /// <summary>Selected localization-table row — picking a row re-places the layout at that count.</summary>
    [ObservableProperty]
    private CoverageRow? _selectedRow;

    /// <summary>Selected detection-table row — picking a row re-places the layout at that count.</summary>
    [ObservableProperty]
    private DetectionRow? _selectedDetectionRow;

    public ObservableCollection<CoverageRow> CoverageRows { get; } = [];
    public ObservableCollection<DetectionRow> DetectionRows { get; } = [];

    public DeploymentPlannerViewModel(DeploymentPlannerMapService mapService)
    {
        _mapService = mapService;
        _mapService.AreaChanged += OnAreaChanged;
        _mapService.StationsMoved += OnStationsMoved;
        _mapService.MoveStatus += msg => StatusMessage = msg;
    }

    public async Task InitializeMapAsync(MapControl mapControl)
        => await _mapService.InitializeAsync(mapControl);

    partial void OnOptimizeForDetectionChanged(bool value)
    {
        StatusMessage = value
            ? "Detection mode: stations spread so ≥1 mic hemisphere covers the area (presence surveys)."
            : "Localization mode: ≥3 stations to pin a source (TDOA).";
        if (HasArea) _ = PlanStationsAsync();   // re-plan in the new mode
    }

    partial void OnMaxStationsChanged(int value)
    {
        if (value < MinStations) return;
        if (HasArea) _ = PlanStationsAsync();   // re-sweep up to the new cap
    }

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
        var detection = OptimizeForDetection;
        var maxN = Math.Max(MinStations, MaxStations);

        if (detection)
        {
            var (rows, recommendedN, coverageByN, greedy, summary) = await Task.Run(() => ComputeDetectionPlan(ring, maxN));
            _suppressRowSelect = true;
            CoverageRows.Clear();
            DetectionRows.Clear();
            foreach (var r in rows) DetectionRows.Add(r);
            _suppressRowSelect = false;

            _coverageByN = coverageByN;
            _greedyLayout = greedy;
            PlanSummary = $"{summary} Click a row to place that many.";

            _suppressRowSelect = true;
            SelectedDetectionRow = DetectionRows.FirstOrDefault(r => r.Stations == recommendedN);
            _suppressRowSelect = false;
            PlaceStations(recommendedN);
        }
        else
        {
            var (rows, recommendedN, coverageByN, summary) = await Task.Run(() => ComputeLocalizationPlan(ring, maxN));
            _suppressRowSelect = true;
            DetectionRows.Clear();
            CoverageRows.Clear();
            foreach (var r in rows) CoverageRows.Add(r);
            _suppressRowSelect = false;

            _coverageByN = coverageByN;
            _greedyLayout = null;
            PlanSummary = $"{summary} Click a row to place that many.";

            _suppressRowSelect = true;
            SelectedRow = CoverageRows.FirstOrDefault(r => r.Stations == recommendedN);
            _suppressRowSelect = false;
            PlaceStations(recommendedN);
        }
    }

    private bool CanPlanStations() => HasArea;

    partial void OnSelectedRowChanged(CoverageRow? value)
    {
        if (_suppressRowSelect || value is null) return;
        PlaceStations(value.Stations);
    }

    partial void OnSelectedDetectionRowChanged(DetectionRow? value)
    {
        if (_suppressRowSelect || value is null) return;
        PlaceStations(value.Stations);
    }

    /// <summary>Place an N-station layout (perimeter ring or detection greedy) and update baseline + map.</summary>
    private void PlaceStations(int n)
    {
        var ring = _currentRing;
        if (ring is null || n < 3) return;

        List<(double Lat, double Lon, double HeadingDeg)> stations;
        if (OptimizeForDetection)
        {
            if (_greedyLayout is null) return;
            stations = _greedyLayout.Take(n).ToList();
        }
        else
        {
            var proj = new GeoProjection(ring.Average(p => p.Lat), ring.Average(p => p.Lon));
            var polygon = ring.Select(p => proj.ToLocal(p.Lat, p.Lon)).ToList();
            stations = PerimeterLayout.Ring(polygon, n)
                .Select(s => { var (lat, lon) = proj.ToGeo(s.X, s.Y); return (lat, lon, s.HeadingDeg); })
                .ToList();
        }

        _mapService.ShowStations(stations);
        _mapService.FinishEditing();   // leave edit mode so stray taps don't extend the polygon
        _optimalCoverage = _coverageByN.TryGetValue(n, out var c) ? c : 0;
        HasPlan = stations.Count >= 3;

        var metric = OptimizeForDetection ? "detection" : "Fusion";
        TweakSummary = HasPlan
            ? $"Optimal {n}-station layout: {_optimalCoverage:P0} {metric} coverage. Click “Move Stations”, then tap a station and its real-world spot to see the cost."
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

    private void OnStationsMoved(IReadOnlyList<(double Lat, double Lon)> positions)
    {
        var ring = _currentRing;
        if (ring is null || positions.Count < 3)
        {
            TweakSummary = "Need ≥3 stations.";
            return;
        }

        var prm = new LocalizationParams();
        var proj = new GeoProjection(ring.Average(p => p.Lat), ring.Average(p => p.Lon));
        var polygon = ring.Select(p => proj.ToLocal(p.Lat, p.Lon)).ToList();

        // Boxes aim at the centroid in both modes (detection is omnidirectional, so heading
        // is only a box-orientation cue there; localization uses it for bearing/fusion).
        double cx = polygon.Average(p => p.X), cy = polygon.Average(p => p.Y);
        var stns = positions.Select(s =>
        {
            var (x, y) = proj.ToLocal(s.Lat, s.Lon);
            var heading = (Math.Atan2(cx - x, cy - y) * 180.0 / Math.PI + 360) % 360;
            return new ArrayStation(x, y, heading);
        }).ToArray();

        var current = OptimizeForDetection
            ? AreaModel.EvaluateDetectionPolygon(stns, prm, polygon, GridRes).Coverage
            : AreaModel.EvaluatePolygon(stns, LocalizationMethod.Fusion, prm, polygon, GridRes).Coverage;

        // Re-render with the mode-correct headings (the service draws whatever heading it's given).
        var rendered = stns.Select(s => { var (lat, lon) = proj.ToGeo(s.X, s.Y); return (lat, lon, s.HeadingDeg); }).ToList();
        _mapService.ShowStations(rendered);

        var deltaPts = (_optimalCoverage - current) * 100.0;
        var verdict = deltaPts <= 0.5 ? "as good as optimal"
            : deltaPts <= 5 ? $"{deltaPts:F0} pts below optimal"
            : $"{deltaPts:F0} pts below optimal — consider a better spot";
        var metric = OptimizeForDetection ? "detection" : "Fusion";
        TweakSummary = $"Your layout: {current:P0} {metric} coverage ({verdict}; optimal {_optimalCoverage:P0}).";
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
        AreaSummary = $"Study area: {m2 / SqMetersPerHectare:F1} ha ({m2 / SqMetersPerAcre:F1} ac) · {corners} corners";
        HasArea = true;
        ClearPlan();   // geometry changed → any existing plan is stale
    }

    private void ClearPlan()
    {
        HasPlan = false;
        TweakSummary = string.Empty;
        _optimalCoverage = 0;
        _coverageByN = new();
        _greedyLayout = null;
        if (CoverageRows.Count == 0 && DetectionRows.Count == 0 && PlanSummary.Length == 0) return;
        _suppressRowSelect = true;
        CoverageRows.Clear();
        DetectionRows.Clear();
        _suppressRowSelect = false;
        PlanSummary = string.Empty;
        _mapService.ClearStations();
    }

    /// <summary>Localization sweep: perimeter ring + CRLB coverage per method; pick smallest N meeting the Fusion goal.</summary>
    private static (List<CoverageRow> Rows, int RecommendedN, Dictionary<int, double> CoverageByN, string Summary)
        ComputeLocalizationPlan(IReadOnlyList<(double Lat, double Lon)> ring, int maxStations)
    {
        var proj = new GeoProjection(ring.Average(p => p.Lat), ring.Average(p => p.Lon));
        var polygon = ring.Select(p => proj.ToLocal(p.Lat, p.Lon)).ToList();
        var prm = new LocalizationParams();

        var rows = new List<CoverageRow>();
        var coverageByN = new Dictionary<int, double>();
        int? best = null;
        for (var n = MinStations; n <= maxStations; n++)
        {
            var stns = PerimeterLayout.Ring(polygon, n);
            var t = AreaModel.EvaluatePolygon(stns, LocalizationMethod.Tdoa, prm, polygon, GridRes);
            var b = AreaModel.EvaluatePolygon(stns, LocalizationMethod.Bearing, prm, polygon, GridRes);
            var f = AreaModel.EvaluatePolygon(stns, LocalizationMethod.Fusion, prm, polygon, GridRes);
            rows.Add(new CoverageRow(n, Cell(t), Cell(b), Cell(f)));
            coverageByN[n] = f.Coverage;
            if (best is null && f.Coverage >= LocalizationGoal) best = n;
        }

        var pickN = best ?? maxStations;
        var summary = best is null
            ? $"No layout up to {maxStations} stations reaches {LocalizationGoal:P0} (Fusion). Showing {pickN}."
            : $"Recommended: {pickN} stations — Fusion ≥ {LocalizationGoal:P0} coverage.";
        return (rows, pickN, coverageByN, summary);
    }

    /// <summary>Detection sweep: greedy hemisphere-coverage spread; pick smallest N meeting the detection goal.</summary>
    private static (List<DetectionRow> Rows, int RecommendedN, Dictionary<int, double> CoverageByN,
                    List<(double Lat, double Lon, double HeadingDeg)> Greedy, string Summary)
        ComputeDetectionPlan(IReadOnlyList<(double Lat, double Lon)> ring, int maxStations)
    {
        var proj = new GeoProjection(ring.Average(p => p.Lat), ring.Average(p => p.Lon));
        var polygon = ring.Select(p => proj.ToLocal(p.Lat, p.Lon)).ToList();
        var prm = new LocalizationParams();

        var (layout, coverageByCount) = DetectionLayout.GreedySweep(polygon, maxStations, prm, GridRes);

        var rows = new List<DetectionRow>();
        var coverageByN = new Dictionary<int, double>();
        int? best = null;
        for (var n = MinStations; n <= maxStations; n++)
        {
            var cov = n - 1 < coverageByCount.Length ? coverageByCount[n - 1]
                    : coverageByCount.Length > 0 ? coverageByCount[^1] : 0.0;
            rows.Add(new DetectionRow(n, $"{cov:P0}"));
            coverageByN[n] = cov;
            if (best is null && cov >= DetectionGoal) best = n;
        }

        var pickN = best ?? maxStations;
        var greedy = layout
            .Select(s => { var (lat, lon) = proj.ToGeo(s.X, s.Y); return (lat, lon, s.HeadingDeg); })
            .ToList();

        var summary = best is null
            ? $"No layout up to {maxStations} stations reaches {DetectionGoal:P0} detection coverage. Showing {pickN}."
            : $"Recommended: {pickN} stations — ≥{DetectionGoal:P0} detection coverage.";
        return (rows, pickN, coverageByN, greedy, summary);
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
