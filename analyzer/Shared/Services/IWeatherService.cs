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
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Service for fetching historical weather data from Open-Meteo.
/// </summary>
public interface IWeatherService
{
    /// <summary>
    /// Fetches hourly weather data for a location and date range.
    /// </summary>
    Task<List<WeatherData>> FetchWeatherAsync(
        double latitude,
        double longitude,
        DateTime startDate,
        DateTime endDate,
        CancellationToken ct = default);

    /// <summary>
    /// Extracts morning conditions (averaged over survey window hours) for each date.
    /// </summary>
    List<WeatherData> GetMorningConditions(
        List<WeatherData> hourlyData,
        int windowStartHour,
        int windowEndHour);
}
