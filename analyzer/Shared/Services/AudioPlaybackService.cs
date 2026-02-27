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
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using NAudio.Flac;
using NAudio.Wave;

namespace QuailTracker.Analyzer.Shared.Services;

public class AudioPlaybackService : IDisposable
{
    private Process? _process;
    private string? _tempFile;

    public bool IsPlaying => _process is { HasExited: false };

    public async Task PlaySegmentAsync(
        string filePath, double offsetSeconds, double durationSeconds,
        CancellationToken ct = default)
    {
        Stop();

        _tempFile = Path.Combine(Path.GetTempPath(), $"qt_play_{Guid.NewGuid():N}.wav");
        await Task.Run(() => WriteSegmentToWav(filePath, offsetSeconds, durationSeconds, _tempFile), ct);

        ct.ThrowIfCancellationRequested();

        var psi = new ProcessStartInfo
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };

        if (OperatingSystem.IsMacOS())
        {
            psi.FileName = "afplay";
            psi.Arguments = $"\"{_tempFile}\"";
        }
        else if (OperatingSystem.IsWindows())
        {
            psi.FileName = "powershell";
            psi.Arguments = $"-NoProfile -c \"(New-Object Media.SoundPlayer '{_tempFile}').PlaySync()\"";
        }
        else
        {
            psi.FileName = "ffplay";
            psi.Arguments = $"-nodisp -autoexit \"{_tempFile}\"";
        }

        _process = Process.Start(psi);

        if (_process != null)
        {
            try
            {
                await _process.WaitForExitAsync(ct);
            }
            catch (OperationCanceledException)
            {
                // Stop was called or token was cancelled
            }
        }

        CleanupTemp();
    }

    public async Task PlaySamplesAsync(
        float[] samples, int sampleRate, CancellationToken ct = default)
    {
        Stop();

        _tempFile = Path.Combine(Path.GetTempPath(), $"qt_play_{Guid.NewGuid():N}.wav");
        await Task.Run(() => WriteSamplesToWav(samples, sampleRate, _tempFile), ct);

        ct.ThrowIfCancellationRequested();

        var psi = new ProcessStartInfo
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };

        if (OperatingSystem.IsMacOS())
        {
            psi.FileName = "afplay";
            psi.Arguments = $"\"{_tempFile}\"";
        }
        else if (OperatingSystem.IsWindows())
        {
            psi.FileName = "powershell";
            psi.Arguments = $"-NoProfile -c \"(New-Object Media.SoundPlayer '{_tempFile}').PlaySync()\"";
        }
        else
        {
            psi.FileName = "ffplay";
            psi.Arguments = $"-nodisp -autoexit \"{_tempFile}\"";
        }

        _process = Process.Start(psi);

        if (_process != null)
        {
            try
            {
                await _process.WaitForExitAsync(ct);
            }
            catch (OperationCanceledException)
            {
                // Stop was called or token was cancelled
            }
        }

        CleanupTemp();
    }

    public void Stop()
    {
        try
        {
            if (_process is { HasExited: false })
            {
                _process.Kill();
            }
            _process?.Dispose();
            _process = null;
        }
        catch
        {
            // Process may have already exited
        }

        CleanupTemp();
    }

    public void Dispose()
    {
        Stop();
    }

    private void CleanupTemp()
    {
        if (_tempFile == null) return;
        try
        {
            if (File.Exists(_tempFile))
                File.Delete(_tempFile);
        }
        catch
        {
            // Non-critical cleanup
        }
        _tempFile = null;
    }

    private static void WriteSamplesToWav(float[] samples, int sampleRate, string outFile)
    {
        var outFormat = new WaveFormat(sampleRate, 16, 1);
        using var writer = new WaveFileWriter(outFile, outFormat);
        for (int i = 0; i < samples.Length; i++)
            writer.WriteSample(samples[i]);
    }

    private static void WriteSegmentToWav(
        string sourceFile, double offset, double duration, string outFile)
    {
        using var reader = sourceFile.EndsWith(".flac", StringComparison.OrdinalIgnoreCase)
            ? (WaveStream)new FlacReader(sourceFile)
            : new WaveFileReader(sourceFile);

        var format = reader.WaveFormat;
        var sampleProvider = reader.ToSampleProvider();

        // Skip to offset by reading forward (FlacReader seek may not work)
        int skipSamples = (int)(offset * format.SampleRate) * format.Channels;
        var skipBuf = new float[Math.Min(4096, Math.Max(skipSamples, 1))];
        while (skipSamples > 0)
        {
            int toRead = Math.Min(skipBuf.Length, skipSamples);
            int skipped = sampleProvider.Read(skipBuf, 0, toRead);
            if (skipped == 0) break;
            skipSamples -= skipped;
        }

        // Read segment
        int segmentSamples = (int)(duration * format.SampleRate) * format.Channels;
        var segmentBuf = new float[segmentSamples];
        int totalRead = 0;
        while (totalRead < segmentSamples)
        {
            int read = sampleProvider.Read(segmentBuf, totalRead, segmentSamples - totalRead);
            if (read == 0) break;
            totalRead += read;
        }

        // Write as 16-bit WAV
        var outFormat = new WaveFormat(format.SampleRate, 16, format.Channels);
        using var writer = new WaveFileWriter(outFile, outFormat);
        for (int i = 0; i < totalRead; i++)
        {
            writer.WriteSample(segmentBuf[i]);
        }
    }
}
