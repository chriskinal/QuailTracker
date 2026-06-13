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
using Avalonia.Threading;
using BruTile;
using BruTile.Predefined;
using BruTile.Web;
using Mapsui;
using Mapsui.Layers;
using Mapsui.Nts;
using Mapsui.Projections;
using Mapsui.Styles;
using Mapsui.Tiling.Layers;
using Mapsui.UI.Avalonia;
using NetTopologySuite.Geometries;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Map visualization backed by a native Mapsui <see cref="MapControl"/> (2D, web-mercator).
/// Stations, detections, and localizations are rendered as point features on three
/// <see cref="MemoryLayer"/>s over a satellite base layer; localizations also draw a
/// confidence ellipse. Taps are hit-tested via <see cref="MapControl.Info"/> and surfaced
/// through the <c>*Clicked</c> events. Replaces the former CesiumJS/WebView implementation
/// — no browser, no CDN, and ready for offline (MBTiles) imagery.
/// </summary>
public class MapService : IMapService
{
    // Marker colours (match the retired Cesium SVG markers).
    private static readonly Color StationColor = new(33, 150, 243);    // #2196F3 blue
    private static readonly Color DetectionColor = new(255, 235, 59);  // #FFEB3B yellow
    private static readonly Color LocalizationColor = new(244, 67, 54); // #F44336 red

    // Default view centre (QT001/QT002 site) used until data loads.
    private const double DefaultLon = -87.18, DefaultLat = 32.5917;

    private static readonly GeometryFactory Gf = new();

    private MapControl? _mapControl;
    private MemoryLayer? _stationLayer;
    private MemoryLayer? _detectionLayer;
    private MemoryLayer? _localizationLayer;
    private bool _isInitialized;

    // Raw data caches so layers can be rebuilt when the time filter / highlight changes.
    private IReadOnlyList<Station> _stations = [];
    private IReadOnlyList<Detection> _detections = [];
    private IReadOnlyList<Station> _detectionStations = [];
    private IReadOnlyList<Localization> _localizations = [];
    private DateTime? _filterStart, _filterEnd;
    private string? _highlightId;

    public event EventHandler? MapReady;
    public event EventHandler<string>? StationClicked;
    public event EventHandler<Guid>? DetectionClicked;
    public event EventHandler<Guid>? LocalizationClicked;

    public Task InitializeAsync(MapControl mapControl)
    {
        if (_mapControl != null) return Task.CompletedTask;
        _mapControl = mapControl;

        var map = new Map { CRS = "EPSG:3857" };
        map.Layers.Add(CreateBaseLayer());

        // Draw order: ellipses/localizations below, detections, stations on top.
        _localizationLayer = new MemoryLayer("Localizations") { Style = null };
        _detectionLayer = new MemoryLayer("Detections") { Style = null };
        _stationLayer = new MemoryLayer("Stations") { Style = null };
        map.Layers.Add(_localizationLayer);
        map.Layers.Add(_detectionLayer);
        map.Layers.Add(_stationLayer);

        mapControl.Map = map;
        mapControl.Info += OnMapInfo;

        // Initial view: the default site until data loads (FlyToAll re-frames once it does).
        map.Navigator.CenterOnAndZoomTo(ToWorld(DefaultLon, DefaultLat), ZoomResolution(12));

        _isInitialized = true;
        MapReady?.Invoke(this, EventArgs.Empty);
        return Task.CompletedTask;
    }

    private static TileLayer CreateBaseLayer()
    {
        // Google hybrid (satellite + roads/labels) — same zero-config endpoint the
        // Cesium map used. Swap for an MBTiles source to go fully offline.
        var source = new HttpTileSource(
            new GlobalSphericalMercator(0, 20),
            "https://mt{s}.google.com/vt/lyrs=y&x={x}&y={y}&z={z}",
            ["0", "1", "2", "3"],
            name: "GoogleHybrid",
            attribution: new Attribution("© Google"));
        return new TileLayer(source) { Name = "Base" };
    }

    // ---------------- data setters (marshalled to the UI thread) ----------------

    public Task SetStationsAsync(IReadOnlyList<Station> stations)
        => OnUi(() => { _stations = stations; RebuildStations(); });

    public Task SetDetectionsAsync(IReadOnlyList<Detection> detections, IReadOnlyList<Station> stations)
        => OnUi(() => { _detections = detections; _detectionStations = stations; RebuildDetections(); });

    public Task SetLocalizationsAsync(IReadOnlyList<Localization> localizations)
        => OnUi(() => { _localizations = localizations; RebuildLocalizations(); });

    public Task ClearAllAsync()
        => OnUi(() =>
        {
            _stations = []; _detections = []; _localizations = [];
            RebuildStations(); RebuildDetections(); RebuildLocalizations();
        });

    public Task SetLayerVisibilityAsync(bool stations, bool detections, bool localizations)
        => OnUi(() =>
        {
            if (_stationLayer != null) _stationLayer.Enabled = stations;
            if (_detectionLayer != null) _detectionLayer.Enabled = detections;
            if (_localizationLayer != null) _localizationLayer.Enabled = localizations;
        });

    public Task SetTimeFilterAsync(DateTime? startTime, DateTime? endTime)
        => OnUi(() =>
        {
            _filterStart = startTime; _filterEnd = endTime;
            RebuildDetections(); RebuildLocalizations();
        });

    public Task FlyToAsync(double latitude, double longitude, double altitude = 1000)
        => OnUi(() => _mapControl?.Map.Navigator.CenterOnAndZoomTo(ToWorld(longitude, latitude), ZoomResolution(16)));

    public Task FlyToAllAsync()
        => OnUi(() =>
        {
            var pts = AllVisibleWorldPoints();
            if (pts.Count == 0 || _mapControl == null) return;

            double minX = pts.Min(p => p.X), maxX = pts.Max(p => p.X);
            double minY = pts.Min(p => p.Y), maxY = pts.Max(p => p.Y);

            if (maxX - minX < 1 && maxY - minY < 1)
            {
                _mapControl.Map.Navigator.CenterOnAndZoomTo(new MPoint(minX, minY), ZoomResolution(16));
                return;
            }

            // 15% padding around the extent.
            double padX = (maxX - minX) * 0.15, padY = (maxY - minY) * 0.15;
            _mapControl.Map.Navigator.ZoomToBox(new MRect(minX - padX, minY - padY, maxX + padX, maxY + padY));
        });

    public Task HighlightEntityAsync(Guid? detectionId = null, Guid? localizationId = null, string? stationId = null)
        => OnUi(() =>
        {
            _highlightId = detectionId?.ToString() ?? localizationId?.ToString() ?? stationId;
            RebuildStations(); RebuildDetections(); RebuildLocalizations();
        });

    // ---------------- layer building ----------------

    private void RebuildStations()
    {
        if (_stationLayer == null) return;
        var features = new List<IFeature>();
        foreach (var s in _stations)
        {
            var f = new PointFeature(ToWorld(s.Longitude, s.Latitude));
            f["type"] = "station";
            f["id"] = s.Id;
            f.Styles.Add(MarkerStyle(StationColor, 0.7, _highlightId == s.Id));
            if (!string.IsNullOrEmpty(s.Name)) f.Styles.Add(NameLabel(s.Name));
            features.Add(f);
        }
        _stationLayer.Features = features;
        _stationLayer.DataHasChanged();
    }

    private void RebuildDetections()
    {
        if (_detectionLayer == null) return;
        var byStation = _detectionStations.GroupBy(s => s.Id).ToDictionary(g => g.Key, g => g.First());
        var features = new List<IFeature>();
        foreach (var d in _detections)
        {
            if (!InTimeFilter(d.Timestamp)) continue;
            if (!byStation.TryGetValue(d.StationId, out var st)) continue; // detection drawn at its station
            var f = new PointFeature(ToWorld(st.Longitude, st.Latitude));
            f["type"] = "detection";
            f["id"] = d.Id.ToString();
            f.Styles.Add(MarkerStyle(DetectionColor, 0.4, _highlightId == d.Id.ToString()));
            features.Add(f);
        }
        _detectionLayer.Features = features;
        _detectionLayer.DataHasChanged();
    }

    private void RebuildLocalizations()
    {
        if (_localizationLayer == null) return;
        var features = new List<IFeature>();
        foreach (var l in _localizations)
        {
            if (!InTimeFilter(l.Timestamp)) continue;
            if (l.ErrorEllipseMajor > 0 && l.ErrorEllipseMinor > 0) features.Add(EllipseFeature(l));

            var f = new PointFeature(ToWorld(l.Longitude, l.Latitude));
            f["type"] = "localization";
            f["id"] = l.Id.ToString();
            f.Styles.Add(MarkerStyle(LocalizationColor, 0.8, _highlightId == l.Id.ToString()));
            features.Add(f);
        }
        _localizationLayer.Features = features;
        _localizationLayer.DataHasChanged();
    }

    /// <summary>Confidence ellipse as an N-gon polygon, axes (metres) scaled to world units.</summary>
    private static GeometryFeature EllipseFeature(Localization l)
    {
        var (cx, cy) = SphericalMercator.FromLonLat(l.Longitude, l.Latitude);
        // Web-mercator stretches by 1/cos(lat); convert metric axes to world units.
        double mPerWorld = Math.Cos(l.Latitude * Math.PI / 180.0);
        double a = l.ErrorEllipseMajor / mPerWorld;
        double b = l.ErrorEllipseMinor / mPerWorld;
        double rot = l.ErrorEllipseRotation * Math.PI / 180.0; // from north, clockwise
        double sin = Math.Sin(rot), cos = Math.Cos(rot);

        const int n = 48;
        var coords = new Coordinate[n + 1];
        for (var i = 0; i < n; i++)
        {
            double t = 2 * Math.PI * i / n;
            double ex = a * Math.Cos(t), ey = b * Math.Sin(t);
            coords[i] = new Coordinate(cx + ex * cos + ey * sin, cy - ex * sin + ey * cos);
        }
        coords[n] = coords[0];

        var feature = new GeometryFeature { Geometry = Gf.CreatePolygon(Gf.CreateLinearRing(coords)) };
        feature.Styles.Add(new VectorStyle
        {
            Fill = new Brush(new Color(LocalizationColor.R, LocalizationColor.G, LocalizationColor.B, 40)),
            Line = new Pen(new Color(LocalizationColor.R, LocalizationColor.G, LocalizationColor.B, 160), 1.5),
        });
        return feature;
    }

    private static SymbolStyle MarkerStyle(Color color, double scale, bool highlight) => new()
    {
        SymbolType = SymbolType.Ellipse,
        SymbolScale = highlight ? scale * 1.6 : scale,
        Fill = new Brush(color),
        Outline = new Pen(highlight ? Color.White : Color.Gray, highlight ? 3 : 1),
    };

    private static LabelStyle NameLabel(string text) => new()
    {
        Text = text,
        ForeColor = Color.White,
        BackColor = new Brush(new Color(0, 0, 0, 140)),
        Halo = new Pen(Color.Black, 1),
        HorizontalAlignment = LabelStyle.HorizontalAlignmentEnum.Center,
        VerticalAlignment = LabelStyle.VerticalAlignmentEnum.Bottom,
        Offset = new Offset(0, -14),
    };

    // ---------------- click handling ----------------

    private void OnMapInfo(object? sender, MapInfoEventArgs e)
    {
        var layers = new List<ILayer>();
        if (_stationLayer != null) layers.Add(_stationLayer);
        if (_detectionLayer != null) layers.Add(_detectionLayer);
        if (_localizationLayer != null) layers.Add(_localizationLayer);

        var feature = e.GetMapInfo(layers)?.Feature;
        if (feature is null) return;

        var id = feature["id"] as string;
        if (string.IsNullOrEmpty(id)) return;

        switch (feature["type"] as string)
        {
            case "station":
                StationClicked?.Invoke(this, id);
                break;
            case "detection":
                if (Guid.TryParse(id, out var dg)) DetectionClicked?.Invoke(this, dg);
                break;
            case "localization":
                if (Guid.TryParse(id, out var lg)) LocalizationClicked?.Invoke(this, lg);
                break;
        }
    }

    // ---------------- helpers ----------------

    private List<MPoint> AllVisibleWorldPoints()
    {
        var pts = new List<MPoint>();
        if (_stationLayer?.Enabled == true)
            foreach (var s in _stations)
                if (s.HasValidLocation) pts.Add(ToWorld(s.Longitude, s.Latitude));

        if (_localizationLayer?.Enabled == true)
            foreach (var l in _localizations)
                if (InTimeFilter(l.Timestamp)) pts.Add(ToWorld(l.Longitude, l.Latitude));

        if (_detectionLayer?.Enabled == true)
        {
            var byStation = _detectionStations.GroupBy(s => s.Id).ToDictionary(g => g.Key, g => g.First());
            foreach (var d in _detections)
                if (InTimeFilter(d.Timestamp) && byStation.TryGetValue(d.StationId, out var st))
                    pts.Add(ToWorld(st.Longitude, st.Latitude));
        }
        return pts;
    }

    private bool InTimeFilter(DateTime t)
    {
        if (_filterStart.HasValue && t < _filterStart.Value) return false;
        if (_filterEnd.HasValue && t > _filterEnd.Value) return false;
        return true;
    }

    private static MPoint ToWorld(double lon, double lat)
    {
        var (x, y) = SphericalMercator.FromLonLat(lon, lat);
        return new MPoint(x, y);
    }

    /// <summary>Web-mercator resolution (m/px at the equator) for a standard tile zoom level.</summary>
    private static double ZoomResolution(int zoom) => 156543.033928 / Math.Pow(2, zoom);

    private Task OnUi(Action action)
    {
        if (!_isInitialized) return Task.CompletedTask;
        return Dispatcher.UIThread.InvokeAsync(() =>
        {
            action();
            _mapControl?.RefreshGraphics();
        }).GetTask();
    }
}
