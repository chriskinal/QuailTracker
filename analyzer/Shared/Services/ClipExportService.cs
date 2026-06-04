/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

public record ClipExportProgress(int Current, int Total, string CurrentFileName);

/// <summary>
/// Saves mono PCM WAV clips for selected detections, embedding all required
/// metadata (species, station, timestamp, location, temperature, humidity)
/// in a Broadcast Wave (bext) chunk plus a coding-history block.
/// </summary>
public class ClipExportService
{
    private const int OutputSampleRate = 48000;

    private readonly IAudioFileService _audioService;
    private readonly ObservableCollection<AudioFile> _audioFiles;

    public ClipExportService(
        IAudioFileService audioService,
        ObservableCollection<AudioFile> audioFiles)
    {
        _audioService = audioService;
        _audioFiles = audioFiles;
    }

    public async Task<int> ExportClipsAsync(
        IReadOnlyList<Detection> detections,
        string outputDirectory,
        double padSeconds,
        IProgress<ClipExportProgress>? progress = null,
        CancellationToken ct = default)
    {
        if (detections.Count == 0) return 0;
        Directory.CreateDirectory(outputDirectory);

        var written = 0;
        var byFile = detections.GroupBy(d => d.AudioFilePath).ToList();
        var total = detections.Count;
        var done = 0;

        foreach (var group in byFile)
        {
            ct.ThrowIfCancellationRequested();

            var sourcePath = group.Key;
            var audioFile = _audioFiles.FirstOrDefault(f => f.FilePath == sourcePath);

            // Decode source once into mono 48 kHz. Slice each clip in memory.
            var samples = await _audioService.LoadAllSamplesAsync(sourcePath, ct);
            if (samples.Length == 0)
            {
                done += group.Count();
                progress?.Report(new ClipExportProgress(done, total, Path.GetFileName(sourcePath)));
                continue;
            }

            foreach (var detection in group)
            {
                ct.ThrowIfCancellationRequested();

                var startSec = Math.Max(0.0, detection.OffsetSeconds - padSeconds);
                var endSec = detection.OffsetSeconds + detection.DurationSeconds + padSeconds;
                var startSample = (int)Math.Round(startSec * OutputSampleRate);
                var endSample = (int)Math.Round(endSec * OutputSampleRate);
                startSample = Math.Clamp(startSample, 0, samples.Length);
                endSample = Math.Clamp(endSample, startSample, samples.Length);
                var clipLength = endSample - startSample;
                if (clipLength <= 0) { done++; continue; }

                var clip = new float[clipLength];
                Array.Copy(samples, startSample, clip, 0, clipLength);

                var outName = BuildClipFileName(detection);
                var outPath = Path.Combine(outputDirectory, outName);

                WriteBwfWav(outPath, clip, OutputSampleRate, detection, audioFile, padSeconds);

                written++;
                done++;
                progress?.Report(new ClipExportProgress(done, total, outName));
            }
        }

        return written;
    }

    private static string BuildClipFileName(Detection d)
    {
        var species = Sanitize(string.IsNullOrEmpty(d.CommonName) ? d.ScientificName : d.CommonName);
        var station = Sanitize(string.IsNullOrEmpty(d.StationId) ? "unknown" : d.StationId);
        var offsetMs = (int)Math.Round(d.OffsetSeconds * 1000);
        return $"{station}_{species}_{d.Timestamp:yyyyMMdd_HHmmss}_{offsetMs:D6}.wav";
    }

    private static string Sanitize(string s)
    {
        var sb = new StringBuilder(s.Length);
        foreach (var c in s)
        {
            if (char.IsLetterOrDigit(c)) sb.Append(c);
            else if (c == '-' || c == '_') sb.Append(c);
            else if (char.IsWhiteSpace(c)) sb.Append('_');
            // drop everything else
        }
        return sb.Length == 0 ? "unknown" : sb.ToString();
    }

    private static void WriteBwfWav(
        string path,
        float[] samples,
        int sampleRate,
        Detection detection,
        AudioFile? sourceAudio,
        double padSeconds)
    {
        const int channels = 1;
        const int bitsPerSample = 16;
        const int bytesPerSample = bitsPerSample / 8;
        var byteRate = sampleRate * channels * bytesPerSample;
        var blockAlign = channels * bytesPerSample;
        var dataSize = samples.Length * bytesPerSample;

        var bextBytes = BuildBextChunk(detection, sourceAudio, sampleRate, channels, bitsPerSample, padSeconds);

        // RIFF chunk size = 4 ("WAVE") + (8 + fmt size 16) + (8 + bextBytes.Length) + (8 + dataSize)
        var riffSize = 4 + (8 + 16) + (8 + bextBytes.Length) + (8 + dataSize);

        using var fs = new FileStream(path, FileMode.Create, FileAccess.Write);
        using var bw = new BinaryWriter(fs);

        // RIFF header
        bw.Write(Encoding.ASCII.GetBytes("RIFF"));
        bw.Write((uint)riffSize);
        bw.Write(Encoding.ASCII.GetBytes("WAVE"));

        // fmt subchunk
        bw.Write(Encoding.ASCII.GetBytes("fmt "));
        bw.Write((uint)16);                  // PCM fmt chunk size
        bw.Write((ushort)1);                 // AudioFormat = 1 (PCM)
        bw.Write((ushort)channels);
        bw.Write((uint)sampleRate);
        bw.Write((uint)byteRate);
        bw.Write((ushort)blockAlign);
        bw.Write((ushort)bitsPerSample);

        // bext subchunk
        bw.Write(Encoding.ASCII.GetBytes("bext"));
        bw.Write((uint)bextBytes.Length);
        bw.Write(bextBytes);
        if ((bextBytes.Length & 1) == 1) bw.Write((byte)0); // pad to even

        // data subchunk
        bw.Write(Encoding.ASCII.GetBytes("data"));
        bw.Write((uint)dataSize);
        var clipBytes = new byte[dataSize];
        for (var i = 0; i < samples.Length; i++)
        {
            var s = Math.Clamp(samples[i], -1f, 1f);
            var v = (short)Math.Round(s * 32767f);
            clipBytes[i * 2] = (byte)(v & 0xFF);
            clipBytes[i * 2 + 1] = (byte)((v >> 8) & 0xFF);
        }
        bw.Write(clipBytes);
    }

    /// <summary>
    /// Build a BWF v1 bext chunk (602 fixed bytes + CodingHistory). The
    /// CodingHistory block carries the full QuailTracker metadata as
    /// key=value lines after the standard A=,F=,W=,M=,T= line.
    /// </summary>
    private static byte[] BuildBextChunk(
        Detection d,
        AudioFile? audioFile,
        int sampleRate,
        int channels,
        int bitsPerSample,
        double padSeconds)
    {
        var inv = CultureInfo.InvariantCulture;

        var description = TruncateAscii(
            $"{d.CommonName} ({d.ScientificName}) | station {d.StationId} | conf {d.Confidence:P0} | " +
            $"src {Path.GetFileName(d.AudioFilePath)}",
            256);

        var originator = TruncateAscii("QuailTracker", 32);
        var originatorRef = TruncateAscii(d.StationId, 32);
        var originationDate = d.Timestamp.ToString("yyyy-MM-dd", inv);
        var originationTime = d.Timestamp.ToString("HH:mm:ss", inv);

        // TimeReference: samples since midnight on OriginationDate
        var midnight = d.Timestamp.Date;
        var samplesSinceMidnight = (ulong)Math.Round((d.Timestamp - midnight).TotalSeconds * sampleRate);

        var coding = new StringBuilder();
        coding.Append($"A=PCM,F={sampleRate},W={bitsPerSample},M=mono,T=QuailTracker\r\n");
        coding.Append($"SOURCE_FILE={Path.GetFileName(d.AudioFilePath)}\r\n");
        coding.Append($"SPECIES_COMMON={d.CommonName}\r\n");
        coding.Append($"SPECIES_SCIENTIFIC={d.ScientificName}\r\n");
        coding.Append($"STATION_ID={d.StationId}\r\n");
        coding.Append(string.Create(inv, $"DETECTION_TIME_UTC={d.Timestamp:yyyy-MM-ddTHH:mm:ss.fffZ}\r\n"));
        coding.Append(string.Create(inv, $"OFFSET_SEC={d.OffsetSeconds:F3}\r\n"));
        coding.Append(string.Create(inv, $"DURATION_SEC={d.DurationSeconds:F3}\r\n"));
        coding.Append(string.Create(inv, $"PAD_SEC={padSeconds:F3}\r\n"));
        coding.Append(string.Create(inv, $"CONFIDENCE={d.Confidence:F4}\r\n"));
        if (!double.IsNaN(d.BearingDeg))
            coding.Append(string.Create(inv, $"BEARING_DEG={d.BearingDeg:F2}\r\n"));
        if (audioFile?.MicHeadingDeg is { } micHeading)
            coding.Append(string.Create(inv, $"MIC_HEADING_DEG={micHeading:F0}\r\n"));
        if (audioFile?.Latitude is { } lat)
            coding.Append(string.Create(inv, $"LATITUDE={lat:F6}\r\n"));
        if (audioFile?.Longitude is { } lon)
            coding.Append(string.Create(inv, $"LONGITUDE={lon:F6}\r\n"));
        if (audioFile?.Altitude is { } alt)
            coding.Append(string.Create(inv, $"ALTITUDE_M={alt:F1}\r\n"));
        if (audioFile?.TemperatureCelsius is { } tempC)
            coding.Append(string.Create(inv, $"TEMPERATURE_C={tempC:F2}\r\n"));
        if (audioFile?.HumidityPercent is { } hum)
            coding.Append(string.Create(inv, $"HUMIDITY_PCT={hum:F1}\r\n"));

        var codingBytes = Encoding.ASCII.GetBytes(coding.ToString());

        // Fixed bext fields total = 602 bytes for BWF v1.
        const int fixedSize = 602;
        var buf = new byte[fixedSize + codingBytes.Length];
        var pos = 0;

        WriteFixedAscii(buf, ref pos, description, 256);
        WriteFixedAscii(buf, ref pos, originator, 32);
        WriteFixedAscii(buf, ref pos, originatorRef, 32);
        WriteFixedAscii(buf, ref pos, originationDate, 10);
        WriteFixedAscii(buf, ref pos, originationTime, 8);
        WriteUInt64LE(buf, ref pos, samplesSinceMidnight);
        WriteUInt16LE(buf, ref pos, 1);          // Version = BWF v1
        WriteZeros(buf, ref pos, 64);            // UMID (unused)
        WriteZeros(buf, ref pos, 190);           // Reserved
        Array.Copy(codingBytes, 0, buf, pos, codingBytes.Length);

        return buf;
    }

    private static string TruncateAscii(string s, int max)
    {
        if (s.Length <= max) return s;
        return s[..max];
    }

    private static void WriteFixedAscii(byte[] buf, ref int pos, string value, int width)
    {
        var bytes = Encoding.ASCII.GetBytes(value);
        var len = Math.Min(bytes.Length, width);
        Array.Copy(bytes, 0, buf, pos, len);
        // remaining bytes already zero (buf is zero-initialized)
        pos += width;
    }

    private static void WriteUInt64LE(byte[] buf, ref int pos, ulong value)
    {
        for (var i = 0; i < 8; i++) buf[pos + i] = (byte)((value >> (8 * i)) & 0xFF);
        pos += 8;
    }

    private static void WriteUInt16LE(byte[] buf, ref int pos, ushort value)
    {
        buf[pos] = (byte)(value & 0xFF);
        buf[pos + 1] = (byte)((value >> 8) & 0xFF);
        pos += 2;
    }

    private static void WriteZeros(byte[] buf, ref int pos, int count) => pos += count;
}
