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
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using NAudio.Flac;
using NAudio.Flac.Metadata;
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

                // Read audio metadata
                using var reader = OpenAudioReader(filePath);
                audioFile.SampleRate = reader.WaveFormat.SampleRate;
                audioFile.BitDepth = reader.WaveFormat.BitsPerSample;
                audioFile.Channels = reader.WaveFormat.Channels;
                audioFile.Duration = reader.TotalTime;

                // FlacReader prescan can silently fail, leaving Length=0 and TotalTime=0.
                // Fall back to STREAMINFO metadata for reliable Duration.
                if (audioFile.Duration.TotalSeconds < 0.1 && reader is FlacReader flacReader)
                {
                    var streamInfo = flacReader.Metadata?
                        .OfType<FlacMetadataStreamInfo>()
                        .FirstOrDefault();
                    if (streamInfo != null && streamInfo.TotalSamples > 0 && streamInfo.SampleRate > 0)
                    {
                        audioFile.Duration = TimeSpan.FromSeconds(
                            (double)streamInfo.TotalSamples / streamInfo.SampleRate);
                    }
                }

                // Parse FLAC Vorbis comments for GPS location
                if (filePath.EndsWith(".flac", StringComparison.OrdinalIgnoreCase))
                {
                    var tags = ReadFlacVorbisComments(filePath);
                    if (tags.TryGetValue("LOCATION", out var location))
                    {
                        var parts = location.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                        if (parts.Length >= 2 &&
                            double.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var lat) &&
                            double.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var lon))
                        {
                            audioFile.Latitude = lat;
                            audioFile.Longitude = lon;
                            if (parts.Length >= 3 &&
                                double.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var alt))
                            {
                                audioFile.Altitude = alt;
                            }
                        }
                    }
                    if (tags.TryGetValue("STATION_ID", out var sid) && !string.IsNullOrEmpty(sid))
                        audioFile.StationId = sid;
                }

                audioFile.IsValid = true;
            }
            catch (Exception ex)
            {
                audioFile.IsValid = false;
                audioFile.ErrorMessage = $"[{ex.GetType().Name}] {ex.Message}";
            }

            return audioFile;
        }, ct);
    }

    public async Task<IReadOnlyList<AudioFile>> LoadFolderAsync(
        string folderPath,
        IProgress<(int current, int total, string fileName)>? progress = null,
        CancellationToken ct = default)
    {
        var wavFiles = Directory.GetFiles(folderPath, "*.*", SearchOption.AllDirectories)
            .Where(f => f.EndsWith(".wav", StringComparison.OrdinalIgnoreCase) ||
                        f.EndsWith(".flac", StringComparison.OrdinalIgnoreCase))
            .ToArray();
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

            using var reader = OpenAudioReader(filePath);
            var waveFormat = reader.WaveFormat;

            // Calculate sample positions
            var startSample = (long)(offsetSeconds * waveFormat.SampleRate);
            var sampleCount = (int)(durationSeconds * waveFormat.SampleRate);

            // Ensure we don't read past end of file
            var totalSamples = reader.Length / waveFormat.BlockAlign;

            // FlacReader prescan can fail, leaving Length=0. Use STREAMINFO as fallback.
            if (totalSamples <= 0 && reader is FlacReader flacReader)
            {
                var streamInfo = flacReader.Metadata?
                    .OfType<FlacMetadataStreamInfo>()
                    .FirstOrDefault();
                if (streamInfo != null && streamInfo.TotalSamples > 0)
                    totalSamples = streamInfo.TotalSamples;
            }

            if (startSample >= totalSamples)
            {
                return Array.Empty<float>();
            }

            sampleCount = (int)Math.Min(sampleCount, totalSamples - startSample);

            // For FLAC files, never use Position setter — it can crash with
            // AccessViolationException on files without seek tables (NAudio bug).
            // Always skip forward sequentially instead.
            if (reader is FlacReader)
            {
                var provider2 = reader.ToSampleProvider();
                if (offsetSeconds > 0.01)
                {
                    var skipSamples = startSample * waveFormat.Channels;
                    var skipBuf = new float[Math.Min(4096, skipSamples)];
                    while (skipSamples > 0)
                    {
                        var toRead = (int)Math.Min(skipBuf.Length, skipSamples);
                        var skipped = provider2.Read(skipBuf, 0, toRead);
                        if (skipped == 0) break;
                        skipSamples -= skipped;
                    }
                }
                var buffer2 = new float[sampleCount * waveFormat.Channels];
                var samplesRead2 = provider2.Read(buffer2, 0, buffer2.Length);
                if (waveFormat.Channels == 2)
                {
                    var mono = new float[samplesRead2 / 2];
                    for (var i = 0; i < mono.Length; i++)
                        mono[i] = (buffer2[i * 2] + buffer2[i * 2 + 1]) / 2f;
                    buffer2 = mono;
                }
                else if (samplesRead2 < buffer2.Length)
                {
                    Array.Resize(ref buffer2, samplesRead2);
                }
                if (waveFormat.SampleRate != TargetSampleRate)
                    buffer2 = Resample(buffer2, waveFormat.SampleRate, TargetSampleRate);
                return buffer2;
            }

            // WAV files: seek is safe
            reader.Position = startSample * waveFormat.BlockAlign;

            // Create SampleProvider — reads from current stream position
            var provider = reader.ToSampleProvider();

            // Read samples as float via SampleProvider
            var buffer = new float[sampleCount * waveFormat.Channels];
            var samplesRead = provider.Read(buffer, 0, buffer.Length);

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
            else
            {
                // Mono: trim buffer to actual samples read
                if (samplesRead < buffer.Length)
                    Array.Resize(ref buffer, samplesRead);
            }

            // Resample if needed
            if (waveFormat.SampleRate != TargetSampleRate)
            {
                buffer = Resample(buffer, waveFormat.SampleRate, TargetSampleRate);
            }

            return buffer;
        }, ct);
    }

    public async Task<float[]> LoadAllSamplesAsync(
        string filePath,
        CancellationToken ct = default)
    {
        return await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            using var reader = OpenAudioReader(filePath);
            var waveFormat = reader.WaveFormat;
            var channels = waveFormat.Channels;
            var srcRate = waveFormat.SampleRate;

            var totalFrames = reader.Length / waveFormat.BlockAlign;
            if (totalFrames <= 0 && reader is FlacReader flacReader)
            {
                var streamInfo = flacReader.Metadata?
                    .OfType<FlacMetadataStreamInfo>()
                    .FirstOrDefault();
                if (streamInfo != null && streamInfo.TotalSamples > 0)
                    totalFrames = streamInfo.TotalSamples;
            }

            var provider = reader.ToSampleProvider();

            // Decode the whole file once. Pre-size the mono buffer when we know
            // the frame count so we don't pay reallocation cost on long files.
            var mono = totalFrames > 0
                ? new float[totalFrames]
                : new float[srcRate * 60]; // grow as needed for unknown-length files
            var monoCount = 0;

            const int chunkFrames = 16384;
            var chunk = new float[chunkFrames * channels];

            while (true)
            {
                ct.ThrowIfCancellationRequested();

                var samplesRead = provider.Read(chunk, 0, chunk.Length);
                if (samplesRead == 0) break;

                var framesRead = samplesRead / channels;

                if (monoCount + framesRead > mono.Length)
                {
                    var newSize = Math.Max(mono.Length * 2, monoCount + framesRead);
                    Array.Resize(ref mono, newSize);
                }

                if (channels == 1)
                {
                    Array.Copy(chunk, 0, mono, monoCount, framesRead);
                }
                else
                {
                    for (var f = 0; f < framesRead; f++)
                    {
                        var sum = 0f;
                        for (var c = 0; c < channels; c++)
                            sum += chunk[f * channels + c];
                        mono[monoCount + f] = sum / channels;
                    }
                }

                monoCount += framesRead;
            }

            if (monoCount < mono.Length)
                Array.Resize(ref mono, monoCount);

            if (srcRate != TargetSampleRate)
                mono = Resample(mono, srcRate, TargetSampleRate);

            return mono;
        }, ct);
    }

    public async Task<(float[] left, float[] right)> ExtractStereoSegmentAsync(
        string filePath,
        double offsetSeconds,
        double durationSeconds = 3.0,
        CancellationToken ct = default)
    {
        return await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            using var reader = OpenAudioReader(filePath);
            var waveFormat = reader.WaveFormat;

            if (waveFormat.Channels != 2)
                return (Array.Empty<float>(), Array.Empty<float>());

            var startSample = (long)(offsetSeconds * waveFormat.SampleRate);
            var sampleCount = (int)(durationSeconds * waveFormat.SampleRate);
            var totalSamples = reader.Length / waveFormat.BlockAlign;

            if (reader is FlacReader flacReader2)
            {
                var streamInfo = flacReader2.Metadata?
                    .OfType<FlacMetadataStreamInfo>()
                    .FirstOrDefault();
                if (streamInfo != null && streamInfo.TotalSamples > 0)
                    totalSamples = streamInfo.TotalSamples;
            }

            if (startSample >= totalSamples)
                return (Array.Empty<float>(), Array.Empty<float>());

            sampleCount = (int)Math.Min(sampleCount, totalSamples - startSample);

            // Read interleaved stereo samples
            ISampleProvider provider;
            if (reader is FlacReader)
            {
                provider = reader.ToSampleProvider();
                if (offsetSeconds > 0.01)
                {
                    var skipSamples = startSample * 2; // stereo
                    var skipBuf = new float[Math.Min(4096, skipSamples)];
                    while (skipSamples > 0)
                    {
                        var toRead = (int)Math.Min(skipBuf.Length, skipSamples);
                        var skipped = provider.Read(skipBuf, 0, toRead);
                        if (skipped == 0) break;
                        skipSamples -= skipped;
                    }
                }
            }
            else
            {
                reader.Position = startSample * waveFormat.BlockAlign;
                provider = reader.ToSampleProvider();
            }

            var interleaved = new float[sampleCount * 2];
            var read = provider.Read(interleaved, 0, interleaved.Length);

            // Deinterleave into L and R
            var frames = read / 2;
            var left = new float[frames];
            var right = new float[frames];
            for (var i = 0; i < frames; i++)
            {
                left[i] = interleaved[i * 2];
                right[i] = interleaved[i * 2 + 1];
            }

            return (left, right);
        }, ct);
    }

    public async Task<(float[] left, float[] right, int sampleRate)> LoadAllStereoSamplesAsync(
        string filePath,
        CancellationToken ct = default)
    {
        return await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            using var reader = OpenAudioReader(filePath);
            var waveFormat = reader.WaveFormat;

            if (waveFormat.Channels != 2)
                return (Array.Empty<float>(), Array.Empty<float>(), waveFormat.SampleRate);

            var totalFrames = reader.Length / waveFormat.BlockAlign;
            if (totalFrames <= 0 && reader is FlacReader flacReader)
            {
                var streamInfo = flacReader.Metadata?
                    .OfType<FlacMetadataStreamInfo>()
                    .FirstOrDefault();
                if (streamInfo != null && streamInfo.TotalSamples > 0)
                    totalFrames = streamInfo.TotalSamples;
            }

            var provider = reader.ToSampleProvider();

            // Pre-size when we know the frame count; otherwise grow on demand.
            var left = totalFrames > 0 ? new float[totalFrames] : new float[waveFormat.SampleRate * 60];
            var right = totalFrames > 0 ? new float[totalFrames] : new float[waveFormat.SampleRate * 60];
            var frameCount = 0;

            const int chunkFrames = 16384;
            var chunk = new float[chunkFrames * 2];

            while (true)
            {
                ct.ThrowIfCancellationRequested();

                var samplesRead = provider.Read(chunk, 0, chunk.Length);
                if (samplesRead == 0) break;

                var framesRead = samplesRead / 2;

                if (frameCount + framesRead > left.Length)
                {
                    var newSize = Math.Max(left.Length * 2, frameCount + framesRead);
                    Array.Resize(ref left, newSize);
                    Array.Resize(ref right, newSize);
                }

                for (var f = 0; f < framesRead; f++)
                {
                    left[frameCount + f] = chunk[f * 2];
                    right[frameCount + f] = chunk[f * 2 + 1];
                }

                frameCount += framesRead;
            }

            if (frameCount < left.Length)
            {
                Array.Resize(ref left, frameCount);
                Array.Resize(ref right, frameCount);
            }

            return (left, right, waveFormat.SampleRate);
        }, ct);
    }

    public int GetSegmentCount(AudioFile audioFile, double segmentDuration = 3.0, double overlapSeconds = 0.0)
    {
        if (!audioFile.IsValid || audioFile.Duration.TotalSeconds <= 0)
            return 0;

        var step = segmentDuration - overlapSeconds;
        if (step <= 0) step = segmentDuration;
        return Math.Max(1, (int)Math.Ceiling(audioFile.Duration.TotalSeconds / step));
    }

    private static WaveStream OpenAudioReader(string filePath)
    {
        if (filePath.EndsWith(".flac", StringComparison.OrdinalIgnoreCase))
            return new FlacReader(filePath);
        return new WaveFileReader(filePath);
    }

    /// <summary>
    /// Parses FLAC metadata blocks to extract Vorbis comment key=value pairs.
    /// </summary>
    private static Dictionary<string, string> ReadFlacVorbisComments(string filePath)
    {
        var tags = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        try
        {
            using var fs = File.OpenRead(filePath);
            // Skip "fLaC" sync
            if (fs.Length < 8) return tags;
            fs.Position = 4;

            // Walk metadata blocks
            var header = new byte[4];
            while (true)
            {
                if (fs.Read(header, 0, 4) < 4) break;
                bool isLast = (header[0] & 0x80) != 0;
                int blockType = header[0] & 0x7F;
                int blockLen = (header[1] << 16) | (header[2] << 8) | header[3];

                if (blockType == 4) // VORBIS_COMMENT
                {
                    var data = new byte[blockLen];
                    if (fs.Read(data, 0, blockLen) < blockLen) break;
                    ParseVorbisCommentBlock(data, tags);
                    break;
                }

                fs.Position += blockLen;
                if (isLast) break;
            }
        }
        catch
        {
            // Non-fatal — file metadata is optional
        }
        return tags;
    }

    private static void ParseVorbisCommentBlock(byte[] data, Dictionary<string, string> tags)
    {
        if (data.Length < 8) return;
        int pos = 0;

        // Vendor string length (little-endian 32-bit)
        int vendorLen = BitConverter.ToInt32(data, pos); pos += 4;
        if (vendorLen < 0 || pos + vendorLen + 4 > data.Length) return;
        pos += vendorLen; // skip vendor string

        // Comment count
        int count = BitConverter.ToInt32(data, pos); pos += 4;
        for (int i = 0; i < count && pos + 4 <= data.Length; i++)
        {
            int commentLen = BitConverter.ToInt32(data, pos); pos += 4;
            if (commentLen < 0 || pos + commentLen > data.Length) break;

            var comment = Encoding.UTF8.GetString(data, pos, commentLen);
            pos += commentLen;

            var eq = comment.IndexOf('=');
            if (eq > 0)
                tags[comment[..eq]] = comment[(eq + 1)..];
        }
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
