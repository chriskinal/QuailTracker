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

namespace QuailTracker.Analyzer.Shared.Models;

/// <summary>
/// Hourly weather observation from Open-Meteo, used as model covariates.
/// </summary>
public class WeatherData
{
    public DateTime Timestamp { get; set; }
    public double TemperatureCelsius { get; set; }
    public double RelativeHumidityPercent { get; set; }
    public double WindSpeedKmh { get; set; }
    public double CloudCoverPercent { get; set; }
    public double PrecipitationMm { get; set; }
    public double SurfacePressureHpa { get; set; }

    /// <summary>
    /// Beaufort wind scale (0-12) derived from wind speed.
    /// </summary>
    public int BeaufortScale => WindSpeedKmh switch
    {
        < 1 => 0,
        < 6 => 1,
        < 12 => 2,
        < 20 => 3,
        < 29 => 4,
        < 39 => 5,
        < 50 => 6,
        < 62 => 7,
        < 75 => 8,
        < 89 => 9,
        < 103 => 10,
        < 118 => 11,
        _ => 12
    };

    public override string ToString() =>
        $"{Timestamp:yyyy-MM-dd HH:mm} {TemperatureCelsius:F1}C wind:{WindSpeedKmh:F1}km/h cloud:{CloudCoverPercent:F0}%";
}
