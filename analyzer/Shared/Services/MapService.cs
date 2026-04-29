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
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia.Controls;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Cesium map visualization backed by Avalonia's NativeWebView.
/// Loads the embedded WebContent/cesium.html at startup and exposes a
/// thin C#-side wrapper over the JS API defined in that file. Click events
/// flow back via NativeWebView.WebMessageReceived (JS calls
/// <c>invokeCSharpAction(JSON.stringify(msg))</c>).
/// </summary>
public class MapService : IMapService
{
    private NativeWebView? _webView;
    private bool _isInitialized;

    public event EventHandler? MapReady;
    public event EventHandler<string>? StationClicked;
    public event EventHandler<Guid>? DetectionClicked;
    public event EventHandler<Guid>? LocalizationClicked;

    public Task InitializeAsync(NativeWebView webView)
    {
        if (_webView != null) return Task.CompletedTask;

        _webView = webView;
        _webView.WebMessageReceived += OnWebMessageReceived;
        _webView.NavigationStarted += (_, _) =>
            Console.WriteLine("[MapService] NavigationStarted");
        _webView.NavigationCompleted += OnNavigationCompleted;

        var html = LoadCesiumHtml();
        Console.WriteLine($"[MapService] Loaded cesium.html ({html.Length} bytes); calling NavigateToString");
        _webView.NavigateToString(html);

        // MapReady is raised when the JS sends `{"type":"mapReady"}` (preferred),
        // or — as a fallback — when NavigationCompleted fires. The fallback
        // ensures the WebView surface becomes visible even if Cesium throws
        // (e.g., bad ion token) so DevTools is reachable for debugging.
        return Task.CompletedTask;
    }

    private async void OnNavigationCompleted(object? sender, Avalonia.Controls.WebViewNavigationCompletedEventArgs e)
    {
        Console.WriteLine($"[MapService] NavigationCompleted (success={e.IsSuccess})");

        if (_webView != null)
        {
            try
            {
                var hasHelper = await _webView.InvokeScript("typeof invokeCSharpAction");
                Console.WriteLine($"[MapService] typeof invokeCSharpAction = {hasHelper}");

                var hasViewer = await _webView.InvokeScript("typeof Cesium !== 'undefined' && typeof viewer !== 'undefined'");
                Console.WriteLine($"[MapService] Cesium+viewer ready = {hasViewer}");

                var lastError = await _webView.InvokeScript("(window.__lastError||'(none)')");
                Console.WriteLine($"[MapService] window.__lastError = {lastError}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[MapService] InvokeScript probe failed: {ex.Message}");
            }
        }

        if (_isInitialized) return;
        // Fallback path — JS mapReady didn't arrive. Surface the WebView anyway
        // so the user (and DevTools) can see what's happening.
        _isInitialized = true;
        MapReady?.Invoke(this, EventArgs.Empty);
    }

    private static string LoadCesiumHtml()
    {
        var assembly = typeof(MapService).Assembly;
        const string resourceName = "QuailTracker.Analyzer.Shared.WebContent.cesium.html";
        using var stream = assembly.GetManifestResourceStream(resourceName)
            ?? throw new InvalidOperationException(
                $"Embedded resource '{resourceName}' not found. Check csproj <EmbeddedResource>.");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }

    private void OnWebMessageReceived(object? sender, Avalonia.Controls.WebMessageReceivedEventArgs e)
    {
        if (string.IsNullOrEmpty(e.Body)) return;

        Console.WriteLine($"[MapService] WebMessage: {e.Body}");

        try
        {
            using var doc = JsonDocument.Parse(e.Body);
            var root = doc.RootElement;
            if (!root.TryGetProperty("type", out var typeElement)) return;
            var type = typeElement.GetString();

            switch (type)
            {
                case "mapReady":
                    if (_isInitialized) return;
                    _isInitialized = true;
                    MapReady?.Invoke(this, EventArgs.Empty);
                    break;

                case "entitySelected":
                    DispatchEntitySelected(root);
                    break;
            }
        }
        catch (JsonException)
        {
            // Ignore malformed messages — JS is the only sender, but defensive.
        }
    }

    private void DispatchEntitySelected(JsonElement msg)
    {
        if (!msg.TryGetProperty("entityType", out var entityTypeElement)) return;
        var entityType = entityTypeElement.GetString();

        switch (entityType)
        {
            case "station":
                if (msg.TryGetProperty("stationId", out var sid))
                    StationClicked?.Invoke(this, sid.GetString() ?? string.Empty);
                break;

            case "detection":
                if (msg.TryGetProperty("detectionId", out var did) &&
                    Guid.TryParse(did.GetString(), out var detectionGuid))
                    DetectionClicked?.Invoke(this, detectionGuid);
                break;

            case "localization":
                if (msg.TryGetProperty("localizationId", out var lid) &&
                    Guid.TryParse(lid.GetString(), out var localizationGuid))
                    LocalizationClicked?.Invoke(this, localizationGuid);
                break;
        }
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

    private async Task ExecuteJsAsync(string script)
    {
        if (_webView == null || !_isInitialized) return;
        try
        {
            await _webView.InvokeScript(script);
        }
        catch (Exception ex)
        {
            // Common cause: TabControl unrealized the MapView while we were
            // away on another tab, the native WKWebView handle got torn down,
            // and InvokeScript can't run against a non-existent page. Reset
            // _isInitialized so subsequent calls early-exit cleanly; the next
            // NavigationCompleted (when the user returns) re-fires MapReady,
            // and MapViewModel's handler triggers a full RefreshMapAsync.
            Console.WriteLine($"[MapService] InvokeScript failed: {ex.Message}");
            _isInitialized = false;
        }
    }

    private static string JsonBool(bool value) => value ? "true" : "false";
    private static string JsonString(string value) => value == "null" ? "null" : $"\"{value}\"";
}
