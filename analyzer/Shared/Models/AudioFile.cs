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
using System.IO;
using System.Text.RegularExpressions;

namespace QuailTracker.Analyzer.Shared.Models;

/// <summary>
/// Represents a WAV audio file from a QuailTracker recording station.
/// Filename format: YYYYMMDD_HHMMSS_&lt;station_id&gt;.wav
/// </summary>
public partial class AudioFile
{
    public string FilePath { get; set; } = string.Empty;
    public string FileName => Path.GetFileName(FilePath);
    public string StationId { get; set; } = string.Empty;
    public DateTime Timestamp { get; set; }
    public TimeSpan Duration { get; set; }
    public string DurationText => Duration.TotalHours >= 1
        ? Duration.ToString(@"h\:mm\:ss")
        : Duration.ToString(@"mm\:ss");
    public int SampleRate { get; set; }
    public int BitDepth { get; set; }
    public int Channels { get; set; }
    public long FileSizeBytes { get; set; }
    public bool IsValid { get; set; }
    public string? ErrorMessage { get; set; }
    public double? Latitude { get; set; }
    public double? Longitude { get; set; }
    public double? Altitude { get; set; }

    [GeneratedRegex(@"^(\d{8})_(\d{6})_(.+)\.(wav|flac)$", RegexOptions.IgnoreCase)]
    private static partial Regex FileNamePattern();

    /// <summary>
    /// Parses station ID and timestamp from a QuailTracker filename.
    /// </summary>
    public static (string? stationId, DateTime? timestamp) ParseFileName(string fileName)
    {
        var match = FileNamePattern().Match(fileName);
        if (!match.Success)
            return (null, null);

        var dateStr = match.Groups[1].Value;
        var timeStr = match.Groups[2].Value;
        var stationId = match.Groups[3].Value;

        if (DateTime.TryParseExact(
            $"{dateStr}_{timeStr}",
            "yyyyMMdd_HHmmss",
            null,
            System.Globalization.DateTimeStyles.None,
            out var timestamp))
        {
            return (stationId, timestamp);
        }

        return (stationId, null);
    }

    public override string ToString() => $"{FileName} ({Duration:mm\\:ss})";
}
