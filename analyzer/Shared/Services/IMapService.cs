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
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Service for Cesium map visualization via WebView interop.
/// </summary>
public interface IMapService
{
    /// <summary>
    /// Event raised when the map is ready for interaction.
    /// </summary>
    event EventHandler? MapReady;

    /// <summary>
    /// Event raised when a station marker is clicked.
    /// </summary>
    event EventHandler<string>? StationClicked;

    /// <summary>
    /// Event raised when a detection marker is clicked.
    /// </summary>
    event EventHandler<Guid>? DetectionClicked;

    /// <summary>
    /// Event raised when a localization marker is clicked.
    /// </summary>
    event EventHandler<Guid>? LocalizationClicked;

    /// <summary>
    /// Initializes the Cesium viewer in the WebView.
    /// </summary>
    Task InitializeAsync(object webView);

    /// <summary>
    /// Adds or updates station markers on the map.
    /// </summary>
    Task SetStationsAsync(IReadOnlyList<Station> stations);

    /// <summary>
    /// Adds or updates detection markers on the map.
    /// </summary>
    Task SetDetectionsAsync(IReadOnlyList<Detection> detections, IReadOnlyList<Station> stations);

    /// <summary>
    /// Adds or updates localization markers with confidence ellipses.
    /// </summary>
    Task SetLocalizationsAsync(IReadOnlyList<Localization> localizations);

    /// <summary>
    /// Clears all markers from the map.
    /// </summary>
    Task ClearAllAsync();

    /// <summary>
    /// Sets visibility of layer types.
    /// </summary>
    Task SetLayerVisibilityAsync(bool stations, bool detections, bool localizations);

    /// <summary>
    /// Filters displayed items by time range.
    /// </summary>
    Task SetTimeFilterAsync(DateTime? startTime, DateTime? endTime);

    /// <summary>
    /// Flies the camera to a specific location.
    /// </summary>
    Task FlyToAsync(double latitude, double longitude, double altitude = 1000);

    /// <summary>
    /// Flies the camera to fit all visible entities.
    /// </summary>
    Task FlyToAllAsync();

    /// <summary>
    /// Highlights a specific entity on the map.
    /// </summary>
    Task HighlightEntityAsync(Guid? detectionId = null, Guid? localizationId = null, string? stationId = null);
}
