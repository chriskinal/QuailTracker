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

using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Options for KML export.
/// </summary>
public class KmlExportOptions
{
    public bool IncludeStations { get; set; } = true;
    public bool IncludeDetections { get; set; } = true;
    public bool IncludeLocalizations { get; set; } = true;
    public bool IncludeConfidenceEllipses { get; set; } = true;
    public string DocumentName { get; set; } = "QuailTracker Export";
    public string? DocumentDescription { get; set; }
}

/// <summary>
/// Service for exporting data to KML format for Google Earth.
/// </summary>
public interface IKmlExportService
{
    /// <summary>
    /// Exports all data to a KML file.
    /// </summary>
    Task ExportAsync(
        string filePath,
        IReadOnlyList<Station> stations,
        IReadOnlyList<Detection> detections,
        IReadOnlyList<Localization> localizations,
        KmlExportOptions? options = null,
        CancellationToken ct = default);

    /// <summary>
    /// Exports only stations to a KML file.
    /// </summary>
    Task ExportStationsAsync(
        string filePath,
        IReadOnlyList<Station> stations,
        CancellationToken ct = default);

    /// <summary>
    /// Exports only localizations to a KML file.
    /// </summary>
    Task ExportLocalizationsAsync(
        string filePath,
        IReadOnlyList<Localization> localizations,
        bool includeEllipses = true,
        CancellationToken ct = default);
}
