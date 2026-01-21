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
using System.Text.Json;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Service for Cesium map visualization via WebView interop.
/// This is a stub implementation - actual WebView integration depends on platform.
/// </summary>
public class MapService : IMapService
{
    private object? _webView;
    private bool _isInitialized;

    public event EventHandler? MapReady;
    public event EventHandler<string>? StationClicked;
    public event EventHandler<Guid>? DetectionClicked;
    public event EventHandler<Guid>? LocalizationClicked;

    public async Task InitializeAsync(object webView)
    {
        _webView = webView;

        // In actual implementation, this would:
        // 1. Load Cesium HTML/JS into WebView
        // 2. Set up message passing for events
        // 3. Wait for Cesium to initialize

        await Task.Delay(100); // Simulate initialization

        _isInitialized = true;
        MapReady?.Invoke(this, EventArgs.Empty);
    }

    public async Task SetStationsAsync(IReadOnlyList<Station> stations)
    {
        if (!_isInitialized) return;

        var stationData = stations.Select(s => new
        {
            id = s.Id,
            name = s.Name,
            lat = s.Latitude,
            lon = s.Longitude,
            elevation = s.Elevation ?? 0
        });

        var json = JsonSerializer.Serialize(stationData);
        await ExecuteJsAsync($"setStations({json})");
    }

    public async Task SetDetectionsAsync(IReadOnlyList<Detection> detections, IReadOnlyList<Station> stations)
    {
        if (!_isInitialized) return;

        var stationDict = stations.ToDictionary(s => s.Id);

        var detectionData = detections
            .Where(d => stationDict.ContainsKey(d.StationId))
            .Select(d =>
            {
                var station = stationDict[d.StationId];
                return new
                {
                    id = d.Id,
                    stationId = d.StationId,
                    lat = station.Latitude,
                    lon = station.Longitude,
                    species = d.CommonName,
                    confidence = d.Confidence,
                    timestamp = d.Timestamp.ToString("o")
                };
            });

        var json = JsonSerializer.Serialize(detectionData);
        await ExecuteJsAsync($"setDetections({json})");
    }

    public async Task SetLocalizationsAsync(IReadOnlyList<Localization> localizations)
    {
        if (!_isInitialized) return;

        var locData = localizations.Select(l => new
        {
            id = l.Id,
            lat = l.Latitude,
            lon = l.Longitude,
            species = l.Species,
            timestamp = l.Timestamp.ToString("o"),
            quality = l.QualityScore,
            ellipseMajor = l.ErrorEllipseMajor,
            ellipseMinor = l.ErrorEllipseMinor,
            ellipseRotation = l.ErrorEllipseRotation
        });

        var json = JsonSerializer.Serialize(locData);
        await ExecuteJsAsync($"setLocalizations({json})");
    }

    public async Task ClearAllAsync()
    {
        if (!_isInitialized) return;
        await ExecuteJsAsync("clearAll()");
    }

    public async Task SetLayerVisibilityAsync(bool stations, bool detections, bool localizations)
    {
        if (!_isInitialized) return;
        await ExecuteJsAsync($"setLayerVisibility({JsonBool(stations)}, {JsonBool(detections)}, {JsonBool(localizations)})");
    }

    public async Task SetTimeFilterAsync(DateTime? startTime, DateTime? endTime)
    {
        if (!_isInitialized) return;

        var start = startTime?.ToString("o") ?? "null";
        var end = endTime?.ToString("o") ?? "null";
        await ExecuteJsAsync($"setTimeFilter({JsonString(start)}, {JsonString(end)})");
    }

    public async Task FlyToAsync(double latitude, double longitude, double altitude = 1000)
    {
        if (!_isInitialized) return;
        await ExecuteJsAsync($"flyTo({latitude}, {longitude}, {altitude})");
    }

    public async Task FlyToAllAsync()
    {
        if (!_isInitialized) return;
        await ExecuteJsAsync("flyToAll()");
    }

    public async Task HighlightEntityAsync(Guid? detectionId = null, Guid? localizationId = null, string? stationId = null)
    {
        if (!_isInitialized) return;

        var id = detectionId?.ToString() ?? localizationId?.ToString() ?? stationId ?? "null";
        await ExecuteJsAsync($"highlightEntity({JsonString(id)})");
    }

    private Task ExecuteJsAsync(string script)
    {
        // Stub - actual implementation would execute JS in WebView
        // Example for different platforms:
        // - Avalonia.HtmlRenderer: webView.EvaluateJavaScript(script)
        // - WebView2: webView.ExecuteScriptAsync(script)
        return Task.CompletedTask;
    }

    private static string JsonBool(bool value) => value ? "true" : "false";
    private static string JsonString(string value) => value == "null" ? "null" : $"\"{value}\"";

    /// <summary>
    /// Gets the embedded Cesium HTML content for initialization.
    /// </summary>
    public static string GetCesiumHtml()
    {
        return """
            <!DOCTYPE html>
            <html>
            <head>
                <meta charset="utf-8">
                <script src="https://cesium.com/downloads/cesiumjs/releases/1.119/Build/Cesium/Cesium.js"></script>
                <link href="https://cesium.com/downloads/cesiumjs/releases/1.119/Build/Cesium/Widgets/widgets.css" rel="stylesheet">
                <style>
                    html, body, #cesiumContainer { width: 100%; height: 100%; margin: 0; padding: 0; overflow: hidden; }
                </style>
            </head>
            <body>
                <div id="cesiumContainer"></div>
                <script>
                    // Initialize Cesium with default ion token (user should provide their own)
                    Cesium.Ion.defaultAccessToken = 'YOUR_CESIUM_ION_TOKEN';

                    const viewer = new Cesium.Viewer('cesiumContainer', {
                        terrain: Cesium.Terrain.fromWorldTerrain(),
                        baseLayerPicker: false,
                        geocoder: false,
                        homeButton: false,
                        sceneModePicker: false,
                        navigationHelpButton: false,
                        animation: false,
                        timeline: false,
                        fullscreenButton: false
                    });

                    const stationEntities = new Cesium.EntityCollection();
                    const detectionEntities = new Cesium.EntityCollection();
                    const localizationEntities = new Cesium.EntityCollection();

                    function setStations(stations) {
                        stationEntities.removeAll();
                        stations.forEach(s => {
                            stationEntities.add({
                                id: 'station_' + s.id,
                                position: Cesium.Cartesian3.fromDegrees(s.lon, s.lat, s.elevation),
                                point: { pixelSize: 12, color: Cesium.Color.BLUE },
                                label: { text: s.name || s.id, font: '12px sans-serif', verticalOrigin: Cesium.VerticalOrigin.BOTTOM, pixelOffset: new Cesium.Cartesian2(0, -15) }
                            });
                        });
                        viewer.entities.add(stationEntities);
                    }

                    function setDetections(detections) {
                        detectionEntities.removeAll();
                        detections.forEach(d => {
                            detectionEntities.add({
                                id: 'detection_' + d.id,
                                position: Cesium.Cartesian3.fromDegrees(d.lon, d.lat),
                                point: { pixelSize: 8, color: Cesium.Color.YELLOW.withAlpha(d.confidence) }
                            });
                        });
                        viewer.entities.add(detectionEntities);
                    }

                    function setLocalizations(localizations) {
                        localizationEntities.removeAll();
                        localizations.forEach(l => {
                            localizationEntities.add({
                                id: 'localization_' + l.id,
                                position: Cesium.Cartesian3.fromDegrees(l.lon, l.lat),
                                point: { pixelSize: 14, color: Cesium.Color.RED },
                                ellipse: {
                                    semiMajorAxis: l.ellipseMajor,
                                    semiMinorAxis: l.ellipseMinor,
                                    rotation: Cesium.Math.toRadians(l.ellipseRotation),
                                    material: Cesium.Color.RED.withAlpha(0.3),
                                    outline: true,
                                    outlineColor: Cesium.Color.RED
                                }
                            });
                        });
                        viewer.entities.add(localizationEntities);
                    }

                    function clearAll() {
                        stationEntities.removeAll();
                        detectionEntities.removeAll();
                        localizationEntities.removeAll();
                    }

                    function setLayerVisibility(stations, detections, localizations) {
                        stationEntities.show = stations;
                        detectionEntities.show = detections;
                        localizationEntities.show = localizations;
                    }

                    function flyTo(lat, lon, alt) {
                        viewer.camera.flyTo({
                            destination: Cesium.Cartesian3.fromDegrees(lon, lat, alt)
                        });
                    }

                    function flyToAll() {
                        viewer.zoomTo(viewer.entities);
                    }

                    function highlightEntity(id) {
                        viewer.selectedEntity = viewer.entities.getById(id);
                    }

                    // Notify parent that map is ready
                    window.postMessage({ type: 'mapReady' }, '*');
                </script>
            </body>
            </html>
            """;
    }
}
