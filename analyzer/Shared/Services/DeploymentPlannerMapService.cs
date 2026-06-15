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
/// Map + interactive editing backend for the Deployment Planner tab. Hosts a native Mapsui
/// <see cref="MapControl"/> over satellite imagery and supports two interactions:
///
///  - Study-area polygon: drawn/edited via the Mapsui.Nts <see cref="EditManager"/> + an
///    <see cref="EditingWidget"/>. The edit layer's vertex <see cref="SymbolStyle"/> is what
///    makes vertex insert/delete hit-testing work (a separate decorative layer does NOT).
///  - Planned stations: moved by TAP-to-pick / TAP-to-place via the control's
///    <see cref="MapControl.Info"/> event. (EditManager Modify is NOT used for stations: its
///    insert/delete paths call DeleteCoordinate/InsertCoordinate, which throw
///    NotSupportedException on Point geometries.)
/// </summary>
public sealed class DeploymentPlannerMapService
{
    private const double DefaultLon = -87.18, DefaultLat = 32.5917; // QT001/QT002 site

    private static readonly Color AreaColor = new(33, 150, 243);    // #2196F3 blue
    private static readonly Color StationColor = new(255, 152, 0);  // #FF9800 orange

    private static readonly GeometryFactory Gf = new();

    private MapControl? _mapControl;
    private WritableLayer? _editLayer;
    private WritableLayer? _stationLayer;     // station points (tap-to-move target)
    private MemoryLayer? _stationDecoLayer;   // heading ticks (decorative, redrawn on move)
    private EditManager? _editManager;
    private bool _isInitialized;

    private bool _moveMode;                    // true while "Move Stations" is active
    private GeometryFeature? _picked;          // station picked up, awaiting a destination tap
    private IReadOnlyList<(double Lat, double Lon)>? _lastReportedRing; // dedupe hover-induced fires

    /// <summary>
    /// Fires with the current study-area exterior ring as WGS84 (lat, lon) points
    /// (closed: last == first), or null when there is no polygon.
    /// </summary>
    public event Action<IReadOnlyList<(double Lat, double Lon)>?>? AreaChanged;

    /// <summary>Fires after a station is moved, with the current station positions (WGS84).</summary>
    public event Action<IReadOnlyList<(double Lat, double Lon)>>? StationsMoved;

    /// <summary>Fires with a prompt as the user picks/places stations (for the status line).</summary>
    public event Action<string>? MoveStatus;

    public Task InitializeAsync(MapControl mapControl)
    {
        if (_mapControl != null) return Task.CompletedTask;
        _mapControl = mapControl;

        var map = new Map { CRS = "EPSG:3857" };
        map.Layers.Add(MapImagery.CreateSatelliteBaseLayer());

        _editLayer = new WritableLayer { Name = "StudyArea", Style = EditLayerStyle() };
        map.Layers.Add(_editLayer);
        _editLayer.DataChanged += (_, _) =>
        {
            // The EditingWidget pokes DataHasChanged on every pointer move (incl. hover),
            // so only propagate when the polygon's geometry actually changed — otherwise a
            // mere mouse-move would re-fire AreaChanged and wipe the current plan.
            var ring = ToLatLonRing(CurrentPolygon());
            if (RingsEqual(ring, _lastReportedRing)) return;
            _lastReportedRing = ring;
            var handler = AreaChanged;
            if (handler != null) Dispatcher.UIThread.Post(() => handler(ring));
        };

        // Heading ticks (decorative) below the station dots.
        _stationDecoLayer = new MemoryLayer("StationHeadings") { Style = null };
        map.Layers.Add(_stationDecoLayer);

        _stationLayer = new WritableLayer { Name = "PlannedStations", Style = null };
        map.Layers.Add(_stationLayer);

        _editManager = new EditManager { Layer = _editLayer, VertexRadius = 16 };
        map.Widgets.Enqueue(new EditingWidget(_editManager));

        mapControl.Map = map;
        mapControl.Info += OnMapInfo;   // tap handling for station move
        map.Navigator.CenterOnAndZoomTo(ToWorld(DefaultLon, DefaultLat), ZoomResolution(13));

        _isInitialized = true;
        return Task.CompletedTask;
    }

    /// <summary>Clear any existing area and start drawing a new polygon (tap to add, double-tap to finish).</summary>
    public void StartDrawArea()
    {
        if (!_isInitialized || _editManager == null) return;
        ExitMoveMode();
        ResetArea();
        _editManager.Layer = _editLayer;
        _editManager.EditMode = EditMode.AddPolygon;
        _mapControl?.RefreshGraphics();
    }

    /// <summary>Enter modify mode — drag vertices to move, double-tap/long-press a vertex to delete, tap an edge to add.</summary>
    public void EditArea()
    {
        if (!_isInitialized || _editManager == null) return;
        ExitMoveMode();
        _editManager.Layer = _editLayer;
        _editManager.EditMode = EditMode.Modify;
    }

    /// <summary>Enter station-move mode: tap a station to pick it up, tap again to drop it.</summary>
    public void MoveStations()
    {
        if (!_isInitialized || _editManager == null) return;
        _editManager.EditMode = EditMode.None;   // disable polygon editing while moving stations
        _moveMode = true;
        _picked = null;
    }

    private void ExitMoveMode()
    {
        _moveMode = false;
        _picked = null;
    }

    /// <summary>Leave any active edit/move mode (e.g. after planning) without clearing anything.</summary>
    public void FinishEditing()
    {
        if (!_isInitialized || _editManager == null) return;
        ExitMoveMode();
        _editManager.EditMode = EditMode.None;
    }

    /// <summary>Remove the drawn area entirely.</summary>
    public void ClearArea()
    {
        if (!_isInitialized) return;
        ExitMoveMode();
        ResetArea();
        _lastReportedRing = null;
        _mapControl?.RefreshGraphics();
        AreaChanged?.Invoke(null);
    }

    /// <summary>
    /// Draw the planned stations as orange dots (numbered) with an inward heading tick each.
    /// </summary>
    public void ShowStations(IReadOnlyList<(double Lat, double Lon, double HeadingDeg)> stations)
    {
        if (_stationLayer == null) return;

        _stationLayer.Clear();
        var index = 1;
        foreach (var s in stations)
        {
            var (wx, wy) = SphericalMercator.FromLonLat(s.Lon, s.Lat);
            var dot = new GeometryFeature { Geometry = Gf.CreatePoint(new Coordinate(wx, wy)) };
            dot["num"] = index.ToString();   // LabelStyle.Text is write-only; keep the number in feature data
            dot.Styles.Add(StationStyle());
            dot.Styles.Add(NumberLabel(index.ToString()));
            _stationLayer.Add(dot);
            index++;
        }
        _stationLayer.DataHasChanged();
        RedrawHeadingTicks();
        _mapControl?.RefreshGraphics();
    }

    /// <summary>Remove any planned stations + heading ticks.</summary>
    public void ClearStations()
    {
        _picked = null;
        _stationLayer?.Clear();
        _stationLayer?.DataHasChanged();
        if (_stationDecoLayer != null)
        {
            _stationDecoLayer.Features = [];
            _stationDecoLayer.DataHasChanged();
        }
        _mapControl?.RefreshGraphics();
    }

    // ---- station tap-to-move ----

    private void OnMapInfo(object? sender, MapInfoEventArgs e)
    {
        if (!_moveMode || _stationLayer == null) return;
        var info = e.GetMapInfo([_stationLayer]);

        if (_picked == null)
        {
            // First tap: pick up the tapped station.
            if (info?.Feature is GeometryFeature gf)
            {
                _picked = gf;
                SetHighlighted(gf, true);
                _mapControl?.RefreshGraphics();
                MoveStatus?.Invoke($"Station {LabelOf(gf)} picked up — tap its new location (or another station to switch).");
            }
        }
        else
        {
            // Second tap: drop it. If they tapped another station, switch to that one instead.
            if (info?.Feature is GeometryFeature other && !ReferenceEquals(other, _picked))
            {
                SetHighlighted(_picked, false);
                _picked = other;
                SetHighlighted(other, true);
                _mapControl?.RefreshGraphics();
                MoveStatus?.Invoke($"Station {LabelOf(other)} picked up — tap its new location.");
                return;
            }

            var wp = info?.WorldPosition;
            if (wp == null) return;

            _picked.Geometry = Gf.CreatePoint(new Coordinate(wp.X, wp.Y));
            SetHighlighted(_picked, false);
            _picked = null;
            _stationLayer.DataHasChanged();
            RedrawHeadingTicks();
            _mapControl?.RefreshGraphics();

            var positions = CurrentStationLatLon();
            var handler = StationsMoved;
            if (handler != null && positions.Count > 0)
                Dispatcher.UIThread.Post(() => handler(positions));
        }
    }

    private static string LabelOf(GeometryFeature f) => f["num"] as string ?? "?";

    private void SetHighlighted(GeometryFeature f, bool on)
    {
        var label = LabelOf(f);   // capture before clearing the styles
        f.Styles.Clear();
        f.Styles.Add(on ? StationStyleHighlighted() : StationStyle());
        f.Styles.Add(NumberLabel(label));
    }

    /// <summary>Current station positions (WGS84), in layer order.</summary>
    private IReadOnlyList<(double Lat, double Lon)> CurrentStationLatLon()
    {
        var list = new List<(double Lat, double Lon)>();
        if (_stationLayer == null) return list;
        foreach (var p in _stationLayer.GetFeatures().OfType<GeometryFeature>()
                     .Select(f => f.Geometry).OfType<Point>())
        {
            var (lon, lat) = SphericalMercator.ToLonLat(p.X, p.Y);
            list.Add((lat, lon));
        }
        return list;
    }

    /// <summary>
    /// Redraw each station's box footprint + aim tick + heading label. The box is a schematic,
    /// fixed-screen-size rectangle (the real 130×100 mm enclosure is sub-pixel) oriented so its
    /// LONG (130 mm, mic-axis) side runs perpendicular to the aim and the broad face points at
    /// the study-area centroid — i.e. how to physically lay the unit. Recomputed on every
    /// move/zoom-driven redraw so it stays fixed-size and correctly aimed.
    /// </summary>
    private void RedrawHeadingTicks()
    {
        if (_stationDecoLayer == null || _stationLayer == null) return;

        var points = _stationLayer.GetFeatures().OfType<GeometryFeature>()
            .Select(f => f.Geometry).OfType<Point>().ToList();

        var deco = new List<IFeature>();
        var centroid = CurrentPolygon()?.Centroid;
        if (centroid != null)
        {
            var res = _mapControl?.Map.Navigator.Viewport.Resolution ?? 1.0;
            var boxPen = new Pen(new Color(StationColor.R, StationColor.G, StationColor.B, 255), 2);
            var boxFill = new Brush(new Color(StationColor.R, StationColor.G, StationColor.B, 60));
            var tickPen = new Pen(new Color(StationColor.R, StationColor.G, StationColor.B, 220), 2);

            // Fixed screen sizes (px) → world units at current zoom; aspect 130:100.
            var longHalf = 17.0 * res;   // half the 130 mm (mic-axis) side
            var shortHalf = 13.0 * res;  // half the 100 mm side
            var tickLen = 22.0 * res;

            foreach (var p in points)
            {
                var dx = centroid.X - p.X;
                var dy = centroid.Y - p.Y;
                var mag = Math.Sqrt(dx * dx + dy * dy);
                if (mag < 1e-6) continue;

                double ux = dx / mag, uy = dy / mag;        // aim (boresight, toward centroid)
                double rx = -uy, ry = ux;                    // mic axis (perpendicular to aim)

                // Box rectangle: ± longHalf along the mic axis, ± shortHalf along the aim.
                Coordinate Corner(double a, double b) =>
                    new(p.X + a * longHalf * rx + b * shortHalf * ux,
                        p.Y + a * longHalf * ry + b * shortHalf * uy);
                var box = new GeometryFeature
                {
                    Geometry = Gf.CreatePolygon(Gf.CreateLinearRing(
                    [
                        Corner(+1, +1), Corner(+1, -1), Corner(-1, -1), Corner(-1, +1), Corner(+1, +1),
                    ])),
                };
                box.Styles.Add(new VectorStyle { Fill = boxFill, Line = boxPen, Outline = boxPen });
                deco.Add(box);

                // Aim tick off the broad (target-facing) side + heading label at its tip.
                var bx = p.X + ux * shortHalf;
                var by = p.Y + uy * shortHalf;
                var ex = bx + ux * tickLen;
                var ey = by + uy * tickLen;
                var tick = new GeometryFeature
                {
                    Geometry = Gf.CreateLineString([new Coordinate(bx, by), new Coordinate(ex, ey)]),
                };
                tick.Styles.Add(new VectorStyle { Line = tickPen });
                deco.Add(tick);

                var headingDeg = (Math.Atan2(dx, dy) * 180.0 / Math.PI + 360) % 360;
                var label = new PointFeature(new MPoint(ex, ey));
                label.Styles.Add(HeadingLabel($"{headingDeg:F0}°"));
                deco.Add(label);
            }
        }
        _stationDecoLayer.Features = deco;
        _stationDecoLayer.DataHasChanged();
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

    private static bool RingsEqual(IReadOnlyList<(double Lat, double Lon)>? a, IReadOnlyList<(double Lat, double Lon)>? b)
    {
        if (a is null && b is null) return true;
        if (a is null || b is null) return false;
        if (a.Count != b.Count) return false;
        for (var i = 0; i < a.Count; i++)
            if (Math.Abs(a[i].Lat - b[i].Lat) > 1e-9 || Math.Abs(a[i].Lon - b[i].Lon) > 1e-9) return false;
        return true;
    }

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
        SymbolScale = 0.45,   // small centre anchor inside the box rectangle (+ tap target)
        Fill = new Brush(Color.White),
        Outline = new Pen(new Color(StationColor.R, StationColor.G, StationColor.B, 255), 2),
    };

    /// <summary>Picked-up station: larger, white-filled so it's clearly "in hand".</summary>
    private static SymbolStyle StationStyleHighlighted() => new()
    {
        SymbolType = SymbolType.Ellipse,
        SymbolScale = 1.0,
        Fill = new Brush(Color.White),
        Outline = new Pen(new Color(StationColor.R, StationColor.G, StationColor.B, 255), 3),
    };

    /// <summary>Small label showing the boresight compass heading at the tick tip.</summary>
    private static LabelStyle HeadingLabel(string text) => new()
    {
        Text = text,
        ForeColor = Color.White,
        BackColor = new Brush(new Color(0, 0, 0, 140)),
        Halo = new Pen(Color.Black, 1),
        Font = new Font { Size = 11 },
        HorizontalAlignment = LabelStyle.HorizontalAlignmentEnum.Center,
        VerticalAlignment = LabelStyle.VerticalAlignmentEnum.Center,
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
