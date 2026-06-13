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
/// </summary>
public sealed class DeploymentPlannerMapService
{
    private const double DefaultLon = -87.18, DefaultLat = 32.5917; // QT001/QT002 site

    private static readonly Color AreaColor = new(33, 150, 243); // #2196F3 blue

    private MapControl? _mapControl;
    private WritableLayer? _editLayer;
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

        _editLayer = new WritableLayer { Name = "StudyArea", Style = AreaStyle() };
        map.Layers.Add(_editLayer);
        // DataChanged fires as the user adds/moves vertices — report the live ring.
        _editLayer.DataChanged += (_, _) =>
        {
            var ring = CurrentRingLatLon();
            var handler = AreaChanged;
            if (handler != null) Dispatcher.UIThread.Post(() => handler(ring));
        };

        _editManager = new EditManager { Layer = _editLayer };
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

    /// <summary>Enter modify mode — drag vertices to adjust, shift/double/long-tap a vertex to delete.</summary>
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

    private void ResetArea()
    {
        if (_editManager != null) _editManager.EditMode = EditMode.None;
        _editLayer?.Clear();
        _editLayer?.DataHasChanged();
    }

    /// <summary>Current polygon exterior ring as WGS84 (lat, lon), or null if none / too few points.</summary>
    private IReadOnlyList<(double Lat, double Lon)>? CurrentRingLatLon()
    {
        var polygon = _editLayer?.GetFeatures()
            .OfType<GeometryFeature>()
            .Select(f => f.Geometry)
            .OfType<Polygon>()
            .LastOrDefault();
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

    private static IStyle AreaStyle() => new VectorStyle
    {
        Fill = new Brush(new Color(AreaColor.R, AreaColor.G, AreaColor.B, 48)),
        Line = new Pen(new Color(AreaColor.R, AreaColor.G, AreaColor.B, 220), 2),
    };

    private static MPoint ToWorld(double lon, double lat)
    {
        var (x, y) = SphericalMercator.FromLonLat(lon, lat);
        return new MPoint(x, y);
    }

    private static double ZoomResolution(int zoom) => 156543.033928 / Math.Pow(2, zoom);
}
