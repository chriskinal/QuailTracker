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
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using NAudio.Wave;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Service for reading and processing WAV audio files using NAudio.
/// </summary>
public class AudioFileService : IAudioFileService
{
    private const int TargetSampleRate = 48000;

    public async Task<AudioFile> LoadFileAsync(string filePath, CancellationToken ct = default)
    {
        return await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            var audioFile = new AudioFile { FilePath = filePath };

            try
            {
                if (!File.Exists(filePath))
                {
                    audioFile.IsValid = false;
                    audioFile.ErrorMessage = "File not found";
                    return audioFile;
                }

                var fileInfo = new FileInfo(filePath);
                audioFile.FileSizeBytes = fileInfo.Length;

                // Parse filename for station ID and timestamp
                var (stationId, timestamp) = AudioFile.ParseFileName(Path.GetFileName(filePath));
                audioFile.StationId = stationId ?? "unknown";
                audioFile.Timestamp = timestamp ?? fileInfo.CreationTime;

                // Read WAV metadata
                using var reader = new WaveFileReader(filePath);
                audioFile.SampleRate = reader.WaveFormat.SampleRate;
                audioFile.BitDepth = reader.WaveFormat.BitsPerSample;
                audioFile.Channels = reader.WaveFormat.Channels;
                audioFile.Duration = reader.TotalTime;
                audioFile.IsValid = true;
            }
            catch (Exception ex)
            {
                audioFile.IsValid = false;
                audioFile.ErrorMessage = ex.Message;
            }

            return audioFile;
        }, ct);
    }

    public async Task<IReadOnlyList<AudioFile>> LoadFolderAsync(
        string folderPath,
        IProgress<(int current, int total, string fileName)>? progress = null,
        CancellationToken ct = default)
    {
        var wavFiles = Directory.GetFiles(folderPath, "*.wav", SearchOption.AllDirectories);
        var results = new List<AudioFile>(wavFiles.Length);
        var total = wavFiles.Length;

        for (var i = 0; i < wavFiles.Length; i++)
        {
            ct.ThrowIfCancellationRequested();

            var fileName = Path.GetFileName(wavFiles[i]);
            progress?.Report((i + 1, total, fileName));

            var audioFile = await LoadFileAsync(wavFiles[i], ct);
            results.Add(audioFile);
        }

        return results;
    }

    public async Task<float[]> ExtractSegmentAsync(
        string filePath,
        double offsetSeconds,
        double durationSeconds = 3.0,
        CancellationToken ct = default)
    {
        return await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            using var reader = new WaveFileReader(filePath);
            var waveFormat = reader.WaveFormat;

            // Calculate sample positions
            var startSample = (long)(offsetSeconds * waveFormat.SampleRate);
            var sampleCount = (int)(durationSeconds * waveFormat.SampleRate);

            // Ensure we don't read past end of file
            var totalSamples = reader.SampleCount;
            if (startSample >= totalSamples)
            {
                return Array.Empty<float>();
            }

            sampleCount = (int)Math.Min(sampleCount, totalSamples - startSample);

            // Position reader
            reader.Position = startSample * waveFormat.BlockAlign;

            // Read samples
            var buffer = new float[sampleCount];
            var provider = reader.ToSampleProvider();

            // Skip to offset
            if (startSample > 0)
            {
                var skipBuffer = new float[Math.Min(startSample, 48000)];
                var remaining = startSample;
                while (remaining > 0)
                {
                    var toRead = (int)Math.Min(remaining, skipBuffer.Length);
                    provider.Read(skipBuffer, 0, toRead);
                    remaining -= toRead;
                }
            }

            var samplesRead = provider.Read(buffer, 0, sampleCount);

            // If stereo, convert to mono by averaging channels
            if (waveFormat.Channels == 2)
            {
                var monoBuffer = new float[samplesRead / 2];
                for (var i = 0; i < monoBuffer.Length; i++)
                {
                    monoBuffer[i] = (buffer[i * 2] + buffer[i * 2 + 1]) / 2f;
                }
                buffer = monoBuffer;
                samplesRead = monoBuffer.Length;
            }

            // Resample if needed
            if (waveFormat.SampleRate != TargetSampleRate)
            {
                buffer = Resample(buffer, waveFormat.SampleRate, TargetSampleRate);
            }

            // Normalize
            var maxAbs = 0f;
            foreach (var sample in buffer)
            {
                var abs = Math.Abs(sample);
                if (abs > maxAbs) maxAbs = abs;
            }

            if (maxAbs > 0.001f)
            {
                for (var i = 0; i < buffer.Length; i++)
                {
                    buffer[i] /= maxAbs;
                }
            }

            return buffer;
        }, ct);
    }

    public int GetSegmentCount(AudioFile audioFile, double segmentDuration = 3.0)
    {
        if (!audioFile.IsValid || audioFile.Duration.TotalSeconds < segmentDuration)
            return 0;

        return (int)Math.Floor(audioFile.Duration.TotalSeconds / segmentDuration);
    }

    private static float[] Resample(float[] input, int fromRate, int toRate)
    {
        var ratio = (double)toRate / fromRate;
        var newLength = (int)(input.Length * ratio);
        var output = new float[newLength];

        for (var i = 0; i < newLength; i++)
        {
            var srcIndex = i / ratio;
            var srcIndexInt = (int)srcIndex;
            var frac = (float)(srcIndex - srcIndexInt);

            if (srcIndexInt + 1 < input.Length)
            {
                // Linear interpolation
                output[i] = input[srcIndexInt] * (1 - frac) + input[srcIndexInt + 1] * frac;
            }
            else if (srcIndexInt < input.Length)
            {
                output[i] = input[srcIndexInt];
            }
        }

        return output;
    }
}
