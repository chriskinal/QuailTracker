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

using BruTile;
using BruTile.Predefined;
using BruTile.Web;
using Mapsui.Tiling.Layers;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>Shared base imagery for the map-based tabs (results map and deployment planner).</summary>
internal static class MapImagery
{
    /// <summary>
    /// Google hybrid (satellite + roads/labels) tile layer in web-mercator. Zero-config
    /// (no key). Swap the source for an MBTiles file to take the maps fully offline.
    /// </summary>
    public static TileLayer CreateSatelliteBaseLayer()
    {
        var source = new HttpTileSource(
            new GlobalSphericalMercator(0, 20),
            "https://mt{s}.google.com/vt/lyrs=y&x={x}&y={y}&z={z}",
            ["0", "1", "2", "3"],
            name: "GoogleHybrid",
            attribution: new Attribution("© Google"));
        return new TileLayer(source) { Name = "Base" };
    }
}
