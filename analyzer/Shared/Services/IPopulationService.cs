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
/// Service for population analysis: count matrix construction, simple summary, and N-mixture model fitting.
/// </summary>
public interface IPopulationService
{
    /// <summary>
    /// Builds the site x visit count matrix from detections, applying spatial deduplication.
    /// </summary>
    List<DailyCount> BuildCountMatrix(
        IReadOnlyList<Detection> detections,
        IReadOnlyList<Station> stations,
        IReadOnlyList<Localization> localizations,
        SurveyConfig config,
        ITdoaService tdoaService);

    /// <summary>
    /// Computes simple summary statistics from the count matrix.
    /// </summary>
    PopulationEstimate ComputeSimpleSummary(
        List<DailyCount> dailyCounts,
        SurveyConfig config);

    /// <summary>
    /// Fits an N-mixture model (Royle 2004) to the count matrix.
    /// Returns a PopulationEstimate with model parameters, CIs, and AIC.
    /// </summary>
    Task<PopulationEstimate> FitNMixtureModelAsync(
        List<DailyCount> dailyCounts,
        SurveyConfig config,
        bool useWeatherCovariates = false,
        CancellationToken ct = default);

    /// <summary>
    /// Exports a PopulationEstimate to JSON.
    /// </summary>
    string ExportToJson(PopulationEstimate estimate);

    /// <summary>
    /// Imports a PopulationEstimate from JSON.
    /// </summary>
    PopulationEstimate? ImportFromJson(string json);
}
