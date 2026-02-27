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
using System.Numerics;
using MathNet.Numerics.IntegralTransforms;

namespace QuailTracker.Analyzer.Shared.Services;

public class NoiseReductionService
{
    private const int FftSize = 2048;
    private const int HopSize = 512;
    private const int NumBins = FftSize / 2 + 1;

    private const int FreqSmoothWidth = 3;

    public float Threshold { get; set; } = 2.0f;
    public float SpectralFloor { get; set; } = 0.02f;
    public float TemporalSmoothing { get; set; } = 0.7f;

    public bool HasProfile => _noiseProfile != null;

    private float[]? _noiseProfile;
    private float[]? _window;

    public void EstimateNoiseProfile(float[] samples, int sampleRate)
    {
        var window = GetWindow();
        int totalFrames = Math.Max(1, (samples.Length - FftSize) / HopSize + 1);

        // Compute RMS energy per frame and magnitude spectra
        var frameRms = new float[totalFrames];
        var frameMagnitudes = new float[totalFrames * NumBins];
        var fftBuffer = new Complex[FftSize];

        for (int f = 0; f < totalFrames; f++)
        {
            int offset = f * HopSize;
            double sumSq = 0;

            for (int i = 0; i < FftSize; i++)
            {
                float s = (offset + i < samples.Length) ? samples[offset + i] : 0f;
                float windowed = s * window[i];
                fftBuffer[i] = new Complex(windowed, 0);
                sumSq += windowed * windowed;
            }

            frameRms[f] = (float)Math.Sqrt(sumSq / FftSize);
            Fourier.Forward(fftBuffer, FourierOptions.Default);

            for (int bin = 0; bin < NumBins; bin++)
                frameMagnitudes[f * NumBins + bin] = (float)fftBuffer[bin].Magnitude;
        }

        // Sort frames by RMS to find quietest 10%
        var indices = new int[totalFrames];
        for (int i = 0; i < totalFrames; i++) indices[i] = i;
        Array.Sort(frameRms, indices);

        int quietCount = Math.Max(1, totalFrames / 10);

        // Average magnitude spectra of quietest frames
        var profile = new float[NumBins];
        for (int q = 0; q < quietCount; q++)
        {
            int f = indices[q];
            for (int bin = 0; bin < NumBins; bin++)
                profile[bin] += frameMagnitudes[f * NumBins + bin];
        }

        for (int bin = 0; bin < NumBins; bin++)
            profile[bin] /= quietCount;

        _noiseProfile = profile;
    }

    public float[] Apply(float[] samples)
    {
        if (_noiseProfile == null)
            return samples;

        var window = GetWindow();
        int totalFrames = Math.Max(1, (samples.Length - FftSize) / HopSize + 1);
        var output = new float[samples.Length];
        var windowSum = new float[samples.Length];
        var prevGain = new float[NumBins];
        var gain = new float[NumBins];
        var smoothed = new float[NumBins];
        var fftBuffer = new Complex[FftSize];

        for (int f = 0; f < totalFrames; f++)
        {
            int offset = f * HopSize;

            // Forward FFT
            for (int i = 0; i < FftSize; i++)
            {
                float s = (offset + i < samples.Length) ? samples[offset + i] : 0f;
                fftBuffer[i] = new Complex(s * window[i], 0);
            }

            Fourier.Forward(fftBuffer, FourierOptions.Default);

            // Soft Wiener-style gain: gain = max(1 - noiseThresh/SNR, floor)
            for (int bin = 0; bin < NumBins; bin++)
            {
                float mag = (float)fftBuffer[bin].Magnitude;
                float noise = _noiseProfile[bin] * Threshold;

                if (mag > 1e-10f)
                    gain[bin] = Math.Max(1.0f - noise / mag, SpectralFloor);
                else
                    gain[bin] = SpectralFloor;
            }

            // Frequency smoothing — moving average across bins
            for (int bin = 0; bin < NumBins; bin++)
            {
                float sum = 0f;
                int count = 0;
                int lo = Math.Max(0, bin - FreqSmoothWidth);
                int hi = Math.Min(NumBins - 1, bin + FreqSmoothWidth);
                for (int j = lo; j <= hi; j++)
                {
                    sum += gain[j];
                    count++;
                }
                smoothed[bin] = sum / count;
            }

            // Temporal smoothing — exponential moving average across frames
            for (int bin = 0; bin < NumBins; bin++)
            {
                float g = smoothed[bin];
                g = TemporalSmoothing * prevGain[bin] + (1.0f - TemporalSmoothing) * g;
                // Allow instant attack if current gain is higher (signal appeared)
                if (smoothed[bin] > prevGain[bin])
                    g = smoothed[bin];
                prevGain[bin] = g;

                fftBuffer[bin] *= g;

                // Mirror conjugate for inverse FFT (skip DC and Nyquist)
                if (bin > 0 && bin < NumBins - 1)
                    fftBuffer[FftSize - bin] = Complex.Conjugate(fftBuffer[bin]);
            }

            // Inverse FFT
            Fourier.Inverse(fftBuffer, FourierOptions.Default);

            // Overlap-add with synthesis window
            for (int i = 0; i < FftSize; i++)
            {
                int idx = offset + i;
                if (idx < output.Length)
                {
                    output[idx] += (float)fftBuffer[i].Real * window[i];
                    windowSum[idx] += window[i] * window[i];
                }
            }
        }

        // Normalize by window sum
        for (int i = 0; i < output.Length; i++)
        {
            if (windowSum[i] > 1e-8f)
                output[i] /= windowSum[i];
        }

        return output;
    }

    public float[] Process(float[] samples, int sampleRate)
    {
        EstimateNoiseProfile(samples, sampleRate);
        return Apply(samples);
    }

    private float[] GetWindow()
    {
        if (_window != null) return _window;

        _window = new float[FftSize];
        for (int i = 0; i < FftSize; i++)
            _window[i] = (float)(0.5 * (1.0 - Math.Cos(2.0 * Math.PI * i / (FftSize - 1))));
        return _window;
    }
}
