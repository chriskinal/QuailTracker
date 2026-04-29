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
using System.Globalization;
using Avalonia.Data.Converters;

namespace QuailTracker.Analyzer.Shared.Converters;

/// <summary>
/// Converts a sample-rate value to half (Nyquist) for the spectrogram
/// max-frequency slider. Used by SingleAnalysisView.
/// </summary>
public class HalfValueConverter : IValueConverter
{
    public static readonly HalfValueConverter Instance = new();

    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is int intVal) return intVal / 2.0;
        if (value is double dVal) return dVal / 2.0;
        return 24000.0;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is double dVal) return (int)(dVal * 2);
        return 48000;
    }
}
