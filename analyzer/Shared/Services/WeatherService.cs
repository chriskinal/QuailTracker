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
using System.Globalization;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

public class WeatherService : IWeatherService
{
    private static readonly HttpClient HttpClient = new()
    {
        Timeout = TimeSpan.FromSeconds(30)
    };

    public async Task<List<WeatherData>> FetchWeatherAsync(
        double latitude,
        double longitude,
        DateTime startDate,
        DateTime endDate,
        CancellationToken ct = default)
    {
        var url = $"https://archive-api.open-meteo.com/v1/archive" +
                  $"?latitude={latitude.ToString(CultureInfo.InvariantCulture)}" +
                  $"&longitude={longitude.ToString(CultureInfo.InvariantCulture)}" +
                  $"&start_date={startDate:yyyy-MM-dd}" +
                  $"&end_date={endDate:yyyy-MM-dd}" +
                  $"&hourly=temperature_2m,relative_humidity_2m,wind_speed_10m,cloud_cover,precipitation,surface_pressure";

        var response = await HttpClient.GetAsync(url, ct);
        response.EnsureSuccessStatusCode();

        var json = await response.Content.ReadAsStringAsync(ct);
        return ParseHourlyData(json);
    }

    public List<WeatherData> GetMorningConditions(
        List<WeatherData> hourlyData,
        int windowStartHour,
        int windowEndHour)
    {
        return hourlyData
            .GroupBy(w => w.Timestamp.Date)
            .Select(dayGroup =>
            {
                var morningHours = dayGroup
                    .Where(w => w.Timestamp.Hour >= windowStartHour && w.Timestamp.Hour < windowEndHour)
                    .ToList();

                if (morningHours.Count == 0)
                    return null;

                return new WeatherData
                {
                    Timestamp = dayGroup.Key,
                    TemperatureCelsius = morningHours.Average(w => w.TemperatureCelsius),
                    RelativeHumidityPercent = morningHours.Average(w => w.RelativeHumidityPercent),
                    WindSpeedKmh = morningHours.Average(w => w.WindSpeedKmh),
                    CloudCoverPercent = morningHours.Average(w => w.CloudCoverPercent),
                    PrecipitationMm = morningHours.Sum(w => w.PrecipitationMm),
                    SurfacePressureHpa = morningHours.Average(w => w.SurfacePressureHpa)
                };
            })
            .Where(w => w != null)
            .ToList()!;
    }

    private static List<WeatherData> ParseHourlyData(string json)
    {
        var result = new List<WeatherData>();

        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        if (!root.TryGetProperty("hourly", out var hourly))
            return result;

        var times = hourly.GetProperty("time");
        var temps = hourly.GetProperty("temperature_2m");
        var humidity = hourly.GetProperty("relative_humidity_2m");
        var wind = hourly.GetProperty("wind_speed_10m");
        var cloud = hourly.GetProperty("cloud_cover");
        var precip = hourly.GetProperty("precipitation");
        var pressure = hourly.GetProperty("surface_pressure");

        int count = times.GetArrayLength();
        for (int i = 0; i < count; i++)
        {
            var timeStr = times[i].GetString();
            if (timeStr == null) continue;

            result.Add(new WeatherData
            {
                Timestamp = DateTime.Parse(timeStr, CultureInfo.InvariantCulture),
                TemperatureCelsius = GetDoubleOrDefault(temps[i]),
                RelativeHumidityPercent = GetDoubleOrDefault(humidity[i]),
                WindSpeedKmh = GetDoubleOrDefault(wind[i]),
                CloudCoverPercent = GetDoubleOrDefault(cloud[i]),
                PrecipitationMm = GetDoubleOrDefault(precip[i]),
                SurfacePressureHpa = GetDoubleOrDefault(pressure[i])
            });
        }

        return result;
    }

    private static double GetDoubleOrDefault(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Null)
            return 0;
        return element.TryGetDouble(out var value) ? value : 0;
    }
}
