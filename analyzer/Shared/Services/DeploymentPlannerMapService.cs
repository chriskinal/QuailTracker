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
using Mapsui;
using Mapsui.Layers;
using Mapsui.Nts;
using Mapsui.Nts.Editing;
using Mapsui.Nts.Widgets;
using Mapsui.Projections;
using Mapsui.Styles;
using Mapsui.UI.Avalonia;
using NetTopologySuite.Geometries;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Map + interactive polygon-editing backend for the Deployment Planner tab. Hosts a
/// native Mapsui <see cref="MapControl"/> over satellite imagery and lets the user draw /
/// edit a single study-area polygon (Mapsui.Nts <see cref="EditManager"/> driven by an
/// <see cref="EditingWidget"/>). Reports the drawn ring back as WGS84 lat/lon via
/// <see cref="AreaChanged"/> so the view-model can compute area / station layouts.
///
/// The edit layer is styled with a <see cref="StyleCollection"/> whose <see cref="SymbolStyle"/>
/// renders a dot on every vertex. That is not just decoration: the editor decides "did you
/// tap a vertex?" by hit-testing the edit layer for a SymbolStyle, which is what makes
/// vertex insert/delete behave correctly (a separate decorative layer does NOT work).
/// </summary>
public sealed class DeploymentPlannerMapService
{
    private const double DefaultLon = -87.18, DefaultLat = 32.5917; // QT001/QT002 site

    private static readonly Color AreaColor = new(33, 150, 243);    // #2196F3 blue
    private static readonly Color StationColor = new(255, 152, 0);  // #FF9800 orange

    private static readonly GeometryFactory Gf = new();

    private MapControl? _mapControl;
    private WritableLayer? _editLayer;
    private MemoryLayer? _stationLayer;
    private EditManager? _editManager;
    private bool _isInitialized;

    /// <summary>
    /// Fires with the current study-area exterior ring as WGS84 (lat, lon) points
    /// (closed: last == first), or null when there is no polygon.
    /// </summary>
    public event Action<IReadOnlyList<(double Lat, double Lon)>?>? AreaChanged;

    public Task InitializeAsync(MapControl mapControl)
    {
        if (_mapControl != null) return Task.CompletedTask;
        _mapControl = mapControl;

        var map = new Map { CRS = "EPSG:3857" };
        map.Layers.Add(MapImagery.CreateSatelliteBaseLayer());

        _editLayer = new WritableLayer { Name = "StudyArea", Style = EditLayerStyle() };
        map.Layers.Add(_editLayer);
        // DataChanged fires as the user adds/moves vertices — report the live ring.
        _editLayer.DataChanged += (_, _) =>
        {
            var ring = ToLatLonRing(CurrentPolygon());
            var handler = AreaChanged;
            if (handler != null) Dispatcher.UIThread.Post(() => handler(ring));
        };

        // Planned stations draw on top of the study area.
        _stationLayer = new MemoryLayer("PlannedStations") { Style = null };
        map.Layers.Add(_stationLayer);

        _editManager = new EditManager { Layer = _editLayer, VertexRadius = 16 };
        map.Widgets.Enqueue(new EditingWidget(_editManager));

        mapControl.Map = map;
        map.Navigator.CenterOnAndZoomTo(ToWorld(DefaultLon, DefaultLat), ZoomResolution(13));

        _isInitialized = true;
        return Task.CompletedTask;
    }

    /// <summary>Clear any existing area and start drawing a new polygon (tap to add, double-tap to finish).</summary>
    public void StartDrawArea()
    {
        if (!_isInitialized || _editManager == null) return;
        ResetArea();
        _editManager.EditMode = EditMode.AddPolygon;
        _mapControl?.RefreshGraphics();
    }

    /// <summary>Enter modify mode — drag vertices to move, double-tap/long-press a vertex to delete, tap an edge to add.</summary>
    public void EditArea()
    {
        if (!_isInitialized || _editManager == null) return;
        _editManager.EditMode = EditMode.Modify;
    }

    /// <summary>Stop adding/editing without clearing the drawn area.</summary>
    public void FinishEditing()
    {
        if (!_isInitialized || _editManager == null) return;
        _editManager.EditMode = EditMode.None;
    }

    /// <summary>Remove the drawn area entirely.</summary>
    public void ClearArea()
    {
        if (!_isInitialized) return;
        ResetArea();
        _mapControl?.RefreshGraphics();
        AreaChanged?.Invoke(null);
    }

    /// <summary>Draw the planned stations: an orange dot per station with a number label and an inward heading tick.</summary>
    public void ShowStations(IReadOnlyList<(double Lat, double Lon, double HeadingDeg)> stations)
    {
        if (_stationLayer == null) return;

        var features = new List<IFeature>();
        var index = 1;
        foreach (var s in stations)
        {
            var (wx, wy) = SphericalMercator.FromLonLat(s.Lon, s.Lat);

            // ~70 m heading tick toward the centre (world units = metres / cos(lat)).
            var len = 70.0 / Math.Cos(s.Lat * Math.PI / 180.0);
            var hx = wx + len * Math.Sin(s.HeadingDeg * Math.PI / 180.0);
            var hy = wy + len * Math.Cos(s.HeadingDeg * Math.PI / 180.0);
            var tick = new GeometryFeature
            {
                Geometry = Gf.CreateLineString([new Coordinate(wx, wy), new Coordinate(hx, hy)]),
            };
            tick.Styles.Add(new VectorStyle { Line = new Pen(new Color(StationColor.R, StationColor.G, StationColor.B, 255), 3) });
            features.Add(tick);

            var dot = new PointFeature(new MPoint(wx, wy));
            dot.Styles.Add(StationStyle());
            dot.Styles.Add(NumberLabel(index.ToString()));
            features.Add(dot);
            index++;
        }

        _stationLayer.Features = features;
        _stationLayer.DataHasChanged();
        _mapControl?.RefreshGraphics();
    }

    /// <summary>Remove any planned stations.</summary>
    public void ClearStations()
    {
        if (_stationLayer == null) return;
        _stationLayer.Features = [];
        _stationLayer.DataHasChanged();
        _mapControl?.RefreshGraphics();
    }

    private void ResetArea()
    {
        if (_editManager != null) _editManager.EditMode = EditMode.None;
        _editLayer?.Clear();
        _editLayer?.DataHasChanged();
        ClearStations();
    }

    private Polygon? CurrentPolygon()
        => _editLayer?.GetFeatures()
            .OfType<GeometryFeature>()
            .Select(f => f.Geometry)
            .OfType<Polygon>()
            .LastOrDefault();

    /// <summary>Polygon exterior ring as WGS84 (lat, lon), or null if none / too few points.</summary>
    private static IReadOnlyList<(double Lat, double Lon)>? ToLatLonRing(Polygon? polygon)
    {
        if (polygon is null) return null;
        var coords = polygon.ExteriorRing.Coordinates;
        if (coords.Length < 3) return null;

        var ring = new List<(double Lat, double Lon)>(coords.Length);
        foreach (var c in coords)
        {
            var (lon, lat) = SphericalMercator.ToLonLat(c.X, c.Y);
            ring.Add((lat, lon));
        }
        return ring;
    }

    /// <summary>
    /// Polygon fill/outline + a dot on every vertex. The vertex <see cref="SymbolStyle"/> is
    /// required for correct insert/delete hit-testing (see class remarks), not just looks.
    /// </summary>
    private static StyleCollection EditLayerStyle() => new()
    {
        Styles =
        {
            new VectorStyle
            {
                Fill = new Brush(new Color(AreaColor.R, AreaColor.G, AreaColor.B, 48)),
                Line = new Pen(new Color(AreaColor.R, AreaColor.G, AreaColor.B, 220), 2),
                Outline = new Pen(new Color(AreaColor.R, AreaColor.G, AreaColor.B, 220), 2),
            },
            new SymbolStyle
            {
                SymbolType = SymbolType.Ellipse,
                SymbolScale = 0.5,
                Fill = new Brush(Color.White),
                Outline = new Pen(new Color(AreaColor.R, AreaColor.G, AreaColor.B, 255), 2),
            },
        },
    };

    private static SymbolStyle StationStyle() => new()
    {
        SymbolType = SymbolType.Ellipse,
        SymbolScale = 0.7,
        Fill = new Brush(new Color(StationColor.R, StationColor.G, StationColor.B, 255)),
        Outline = new Pen(Color.White, 2),
    };

    private static LabelStyle NumberLabel(string text) => new()
    {
        Text = text,
        ForeColor = Color.White,
        BackColor = new Brush(new Color(0, 0, 0, 140)),
        Halo = new Pen(Color.Black, 1),
        HorizontalAlignment = LabelStyle.HorizontalAlignmentEnum.Center,
        VerticalAlignment = LabelStyle.VerticalAlignmentEnum.Bottom,
        Offset = new Offset(0, -14),
    };

    private static MPoint ToWorld(double lon, double lat)
    {
        var (x, y) = SphericalMercator.FromLonLat(lon, lat);
        return new MPoint(x, y);
    }

    private static double ZoomResolution(int zoom) => 156543.033928 / Math.Pow(2, zoom);
}
