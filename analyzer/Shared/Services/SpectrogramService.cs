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
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using MathNet.Numerics.IntegralTransforms;
using NAudio.Flac;
using NAudio.Flac.Metadata;
using NAudio.Wave;

namespace QuailTracker.Analyzer.Shared.Services;

public class SpectrogramService
{
    private const int FftSize = 2048;
    private const int HopSize = 512;

    private static readonly int[] GridlineFrequencies =
        [1000, 2000, 4000, 6000, 8000, 12000, 16000, 20000];

    public NoiseReductionService? NoiseReduction { get; set; }

    public Task<WriteableBitmap> GenerateAsync(
        string filePath, int imageWidth, int imageHeight,
        double maxFreqHz = 0, bool noiseReduction = false,
        CancellationToken ct = default)
    {
        return Task.Run(() => Generate(filePath, imageWidth, imageHeight, maxFreqHz, noiseReduction, ct), ct);
    }

    private WriteableBitmap Generate(string filePath, int imageWidth, int imageHeight,
        double maxFreqHz, bool noiseReduction, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();

        var (samples, sampleRate) = ReadAudioMono(filePath);

        if (noiseReduction && NoiseReduction is { HasProfile: true })
            samples = NoiseReduction.Apply(samples);

        int totalFrames = Math.Max(1, (samples.Length - FftSize) / HopSize + 1);
        int numBins = FftSize / 2 + 1;
        double nyquist = sampleRate / 2.0;

        // Limit displayed frequency range
        if (maxFreqHz <= 0 || maxFreqHz > nyquist)
            maxFreqHz = nyquist;
        int maxBin = Math.Clamp((int)(maxFreqHz / nyquist * (numBins - 1)), 1, numBins - 1);

        var window = MakeHannWindow(FftSize);
        var fftBuffer = new Complex[FftSize];

        // Compute magnitudes only for frames that map to pixel columns
        var magnitudeDb = new float[imageWidth * numBins];
        float maxDb = float.MinValue;

        for (int x = 0; x < imageWidth; x++)
        {
            ct.ThrowIfCancellationRequested();

            int frame = imageWidth > 1
                ? (int)((long)x * (totalFrames - 1) / (imageWidth - 1))
                : 0;
            int offset = frame * HopSize;

            for (int i = 0; i < FftSize; i++)
            {
                float s = (offset + i < samples.Length) ? samples[offset + i] : 0f;
                fftBuffer[i] = new Complex(s * window[i], 0);
            }

            Fourier.Forward(fftBuffer, FourierOptions.Default);

            for (int bin = 0; bin < numBins; bin++)
            {
                float db = (float)(20.0 * Math.Log10(Math.Max(fftBuffer[bin].Magnitude, 1e-10)));
                magnitudeDb[x * numBins + bin] = db;
                if (db > maxDb) maxDb = db;
            }
        }

        float floor = maxDb - 80f;
        float range = maxDb - floor;
        if (range < 1f) range = 1f;

        // Build pixel buffer
        var pixels = new byte[imageWidth * imageHeight * 4];

        for (int y = 0; y < imageHeight; y++)
        {
            int bin = imageHeight > 1
                ? (int)((long)(imageHeight - 1 - y) * maxBin / (imageHeight - 1))
                : 0;
            bin = Math.Clamp(bin, 0, maxBin);

            for (int x = 0; x < imageWidth; x++)
            {
                float db = magnitudeDb[x * numBins + bin];
                float normalized = Math.Clamp((db - floor) / range, 0f, 1f);
                var (r, g, b) = MapColor(normalized);

                int idx = (y * imageWidth + x) * 4;
                pixels[idx + 0] = b; // B
                pixels[idx + 1] = g; // G
                pixels[idx + 2] = r; // R
                pixels[idx + 3] = 255; // A
            }
        }

        // Draw frequency gridlines
        foreach (int freq in GridlineFrequencies)
        {
            if (freq > maxFreqHz) break;

            double yFrac = freq / maxFreqHz;
            int y = (int)(imageHeight * (1.0 - yFrac));
            y = Math.Clamp(y, 0, imageHeight - 1);

            for (int x = 0; x < imageWidth; x++)
            {
                int idx = (y * imageWidth + x) * 4;
                pixels[idx + 0] = (byte)(pixels[idx + 0] * 0.7 + 255 * 0.3);
                pixels[idx + 1] = (byte)(pixels[idx + 1] * 0.7 + 255 * 0.3);
                pixels[idx + 2] = (byte)(pixels[idx + 2] * 0.7 + 255 * 0.3);
            }
        }

        // Draw time gridlines (vertical)
        double duration = (double)samples.Length / sampleRate;
        double timeInterval = duration <= 30 ? 5 : duration <= 120 ? 15 : duration <= 600 ? 60 : 300;

        for (double t = timeInterval; t < duration; t += timeInterval)
        {
            int x = (int)(t / duration * imageWidth);
            x = Math.Clamp(x, 0, imageWidth - 1);

            for (int y = 0; y < imageHeight; y++)
            {
                int idx = (y * imageWidth + x) * 4;
                pixels[idx + 0] = (byte)(pixels[idx + 0] * 0.85 + 255 * 0.15);
                pixels[idx + 1] = (byte)(pixels[idx + 1] * 0.85 + 255 * 0.15);
                pixels[idx + 2] = (byte)(pixels[idx + 2] * 0.85 + 255 * 0.15);
            }
        }

        // Create WriteableBitmap and copy pixels
        var bitmap = new WriteableBitmap(
            new PixelSize(imageWidth, imageHeight),
            new Avalonia.Vector(96, 96),
            PixelFormat.Bgra8888,
            AlphaFormat.Premul);

        using (var fb = bitmap.Lock())
        {
            for (int y = 0; y < imageHeight; y++)
            {
                int srcOffset = y * imageWidth * 4;
                var dstPtr = fb.Address + y * fb.RowBytes;
                Marshal.Copy(pixels, srcOffset, dstPtr, imageWidth * 4);
            }
        }

        return bitmap;
    }

    internal static (float[] samples, int sampleRate) ReadAudioMono(string filePath)
    {
        using var reader = OpenAudioReader(filePath);
        var format = reader.WaveFormat;
        var provider = reader.ToSampleProvider();

        long totalSamples = reader.Length / format.BlockAlign;
        if (totalSamples <= 0 && reader is FlacReader flac)
        {
            var si = flac.Metadata?.OfType<FlacMetadataStreamInfo>().FirstOrDefault();
            if (si != null && si.TotalSamples > 0)
                totalSamples = si.TotalSamples;
        }
        if (totalSamples <= 0)
            totalSamples = format.SampleRate * 600;

        var buffer = new float[4096];
        var allSamples = new List<float>((int)Math.Min(totalSamples, int.MaxValue / 2));

        int read;
        while ((read = provider.Read(buffer, 0, buffer.Length)) > 0)
        {
            if (format.Channels == 2)
            {
                for (int i = 0; i < read - 1; i += 2)
                    allSamples.Add((buffer[i] + buffer[i + 1]) / 2f);
            }
            else
            {
                for (int i = 0; i < read; i++)
                    allSamples.Add(buffer[i]);
            }
        }

        return (allSamples.ToArray(), format.SampleRate);
    }

    private static WaveStream OpenAudioReader(string filePath)
    {
        if (filePath.EndsWith(".flac", StringComparison.OrdinalIgnoreCase))
            return new FlacReader(filePath);
        return new WaveFileReader(filePath);
    }

    private static float[] MakeHannWindow(int size)
    {
        var window = new float[size];
        for (int i = 0; i < size; i++)
            window[i] = (float)(0.5 * (1.0 - Math.Cos(2.0 * Math.PI * i / (size - 1))));
        return window;
    }

    private static (byte r, byte g, byte b) MapColor(float value)
    {
        ReadOnlySpan<(float pos, byte r, byte g, byte b)> stops =
        [
            (0.0f, 0x1a, 0x05, 0x33),
            (0.2f, 0x5b, 0x1a, 0x8a),
            (0.4f, 0xc0, 0x20, 0x90),
            (0.6f, 0xff, 0x8c, 0x00),
            (0.8f, 0xff, 0xff, 0x00),
            (1.0f, 0xff, 0xff, 0xff),
        ];

        int seg = 0;
        for (int i = 0; i < stops.Length - 1; i++)
        {
            if (value <= stops[i + 1].pos)
            {
                seg = i;
                break;
            }
            seg = i;
        }

        float segRange = stops[seg + 1].pos - stops[seg].pos;
        float t = segRange > 0 ? (value - stops[seg].pos) / segRange : 0f;
        t = Math.Clamp(t, 0f, 1f);

        byte r = (byte)(stops[seg].r + t * (stops[seg + 1].r - stops[seg].r));
        byte g = (byte)(stops[seg].g + t * (stops[seg + 1].g - stops[seg].g));
        byte b = (byte)(stops[seg].b + t * (stops[seg + 1].b - stops[seg].b));

        return (r, g, b);
    }
}
