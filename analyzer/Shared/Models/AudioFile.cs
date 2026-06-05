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
    public double? TemperatureCelsius { get; set; }
    public double? HumidityPercent { get; set; }

    /// <summary>
    /// Mic-array orientation in true-north degrees (0-359), from the MIC_HEADING
    /// metadata tag (installer points the device arrow with a compass). Null when
    /// absent or unset. Used to convert the relative stereo bearing into an
    /// absolute geographic bearing for two-station localization.
    /// </summary>
    public double? MicHeadingDeg { get; set; }

    // --- PPS / GPS timing (for cross-station TDOA) ----------------------------
    // The firmware latches a GPS 1PPS edge against the audio sample counter and
    // writes the anchor into the recording metadata (FLAC Vorbis comments:
    // PPS_SYNC_UTC / PPS_SYNC_SAMPLE / PPS_EDGES / PPS_SAMPLE_RATE). The PPS
    // sample rate is the *measured* rate — the header's nominal ~48 kHz is wrong
    // by ~1% (MSI clock), so all timing MUST use PpsSampleRate, never SampleRate.

    /// <summary>UTC instant of the GPS 1PPS edge latched during recording (PPS_SYNC_UTC). Null if absent.</summary>
    public DateTime? PpsSyncUtc { get; set; }

    /// <summary>Native sample index (from recording start) at which <see cref="PpsSyncUtc"/> was latched (PPS_SYNC_SAMPLE).</summary>
    public long? PpsSyncSample { get; set; }

    /// <summary>Number of PPS edges captured during the recording (PPS_EDGES).</summary>
    public int? PpsEdges { get; set; }

    /// <summary>Audio sample rate measured against PPS in Hz (PPS_SAMPLE_RATE) — the true rate, e.g. 48050.166.</summary>
    public double? PpsSampleRate { get; set; }

    /// <summary>True when this file carries a usable absolute time anchor (UTC + sample + measured rate).</summary>
    public bool HasPpsTiming => PpsSyncUtc.HasValue && PpsSyncSample.HasValue && PpsSampleRate is > 0;

    /// <summary>
    /// Effective (true) sample rate: the PPS-measured rate when available, else the
    /// nominal header rate. Native sample indices advance at this rate in real time.
    /// </summary>
    public double EffectiveSampleRate => PpsSampleRate is > 0 ? PpsSampleRate.Value : SampleRate;

    /// <summary>UTC of native sample 0 (recording start), back-derived from the PPS anchor. Requires <see cref="HasPpsTiming"/>.</summary>
    public DateTime RecordingStartUtc =>
        PpsSyncUtc!.Value - TimeSpan.FromSeconds(PpsSyncSample!.Value / PpsSampleRate!.Value);

    /// <summary>Maps a native sample index to its absolute UTC instant via the PPS anchor + measured rate. Requires <see cref="HasPpsTiming"/>.</summary>
    public DateTime NativeSampleToUtc(double nativeSample) =>
        PpsSyncUtc!.Value + TimeSpan.FromSeconds((nativeSample - PpsSyncSample!.Value) / PpsSampleRate!.Value);

    /// <summary>Maps an absolute UTC instant to a (fractional) native sample index. Requires <see cref="HasPpsTiming"/>.</summary>
    public double UtcToNativeSample(DateTime utc) =>
        PpsSyncSample!.Value + (utc - PpsSyncUtc!.Value).TotalSeconds * PpsSampleRate!.Value;

    /// <summary>
    /// Compact PPS-timing indicator for the UI: a check + measured rate when a full
    /// anchor is present (TDOA-grade), a "~rate" when only the rate is known, else "—".
    /// </summary>
    public string PpsStatus => HasPpsTiming
        ? $"✓ {PpsSampleRate!.Value:F1} Hz"
        : (PpsSampleRate is > 0 ? $"~ {PpsSampleRate.Value:F1} Hz" : "—");

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
