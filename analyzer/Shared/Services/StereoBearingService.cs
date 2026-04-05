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
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Computes bearing angle from stereo TDOA using cross-correlation.
/// Two PDM mics spaced apart on the same ARU capture the same sound
/// with a time difference that depends on the angle of arrival.
/// </summary>
public class StereoBearingService
{
    private readonly IAudioFileService _audioService;

    /// <summary>Mic spacing in meters (default 0.12m = 12cm).</summary>
    public double MicSpacingMeters { get; set; } = 0.12;

    /// <summary>Temperature in Celsius for speed of sound calculation.</summary>
    public double TemperatureCelsius { get; set; } = 20.0;

    /// <summary>Humidity in %RH for speed of sound correction (minor effect).</summary>
    public double HumidityPercent { get; set; } = 50.0;

    /// <summary>Speed of sound in m/s, computed from temperature and humidity.</summary>
    public double SpeedOfSound => ComputeSpeedOfSound(TemperatureCelsius, HumidityPercent);

    public StereoBearingService(IAudioFileService audioService)
    {
        _audioService = audioService;
    }

    /// <summary>
    /// Compute speed of sound from atmospheric conditions.
    /// Temperature is the dominant factor (~0.6 m/s per °C).
    /// Humidity adds a small correction (~0.5% at 100% RH).
    /// </summary>
    public static double ComputeSpeedOfSound(double tempC, double humidityPct)
    {
        // Cramer (1993) simplified formula
        double c = 331.3 + 0.606 * tempC;

        // Humidity correction (approximate, valid for typical field conditions)
        // Water vapor is lighter than N2/O2, so humid air is faster
        double h = humidityPct / 100.0;
        c += 0.0124 * h * tempC; // ~0.1-0.4 m/s correction

        return c;
    }

    /// <summary>
    /// Compute bearing for a detection using stereo cross-correlation.
    /// Updates detection.BearingDeg, TdoaSamples, and TdoaConfidence.
    /// </summary>
    public async Task ComputeBearingAsync(
        Detection detection,
        int sampleRate = 48000,
        CancellationToken ct = default)
    {
        // Extract L/R channels for the detection segment
        var (left, right) = await _audioService.ExtractStereoSegmentAsync(
            detection.AudioFilePath,
            detection.OffsetSeconds,
            detection.DurationSeconds,
            ct);

        if (left.Length == 0 || right.Length == 0)
            return; // Mono file or read error

        // Cross-correlate to find TDOA
        var (lagSamples, confidence) = CrossCorrelate(left, right, sampleRate);

        detection.TdoaSamples = lagSamples;
        detection.TdoaConfidence = confidence;

        // Convert TDOA to bearing angle
        double tdoaSeconds = (double)lagSamples / sampleRate;
        double sinAngle = tdoaSeconds * SpeedOfSound / MicSpacingMeters;

        // Clamp to valid range (can exceed ±1 due to noise)
        sinAngle = Math.Clamp(sinAngle, -1.0, 1.0);

        detection.BearingDeg = Math.Asin(sinAngle) * (180.0 / Math.PI);
    }

    /// <summary>
    /// Compute bearing for all detections in a list.
    /// </summary>
    public async Task ComputeBearingsAsync(
        System.Collections.Generic.IReadOnlyList<Detection> detections,
        int sampleRate = 48000,
        IProgress<(int current, int total)>? progress = null,
        CancellationToken ct = default)
    {
        for (int i = 0; i < detections.Count; i++)
        {
            ct.ThrowIfCancellationRequested();
            await ComputeBearingAsync(detections[i], sampleRate, ct);
            progress?.Report((i + 1, detections.Count));
        }
    }

    /// <summary>
    /// Normalized cross-correlation between two signals.
    /// Returns (lagSamples, peakConfidence).
    /// Positive lag = sound arrives at right channel first (source is to the right).
    /// </summary>
    private (int lagSamples, double confidence) CrossCorrelate(
        float[] left, float[] right, int sampleRate)
    {
        int n = Math.Min(left.Length, right.Length);
        if (n == 0) return (0, 0);

        // Maximum lag based on mic spacing and speed of sound.
        // Sound can't take longer than spacing/speed to travel between mics.
        int maxLag = (int)Math.Ceiling(MicSpacingMeters / SpeedOfSound * sampleRate) + 2;
        maxLag = Math.Min(maxLag, n / 2); // Don't exceed half the signal

        // Compute mean and energy for normalization
        double meanL = 0, meanR = 0;
        for (int i = 0; i < n; i++)
        {
            meanL += left[i];
            meanR += right[i];
        }
        meanL /= n;
        meanR /= n;

        double energyL = 0, energyR = 0;
        for (int i = 0; i < n; i++)
        {
            double l = left[i] - meanL;
            double r = right[i] - meanR;
            energyL += l * l;
            energyR += r * r;
        }

        double normFactor = Math.Sqrt(energyL * energyR);
        if (normFactor < 1e-10) return (0, 0); // Silent signal

        // Cross-correlation over [-maxLag, +maxLag]
        double bestCorr = double.MinValue;
        int bestLag = 0;

        for (int lag = -maxLag; lag <= maxLag; lag++)
        {
            double sum = 0;
            int start = Math.Max(0, lag);
            int end = Math.Min(n, n + lag);

            for (int i = start; i < end; i++)
            {
                double l = left[i] - meanL;
                double r = right[i - lag] - meanR;
                sum += l * r;
            }

            double corr = sum / normFactor;
            if (corr > bestCorr)
            {
                bestCorr = corr;
                bestLag = lag;
            }
        }

        // Sub-sample interpolation using parabolic fit on the 3 points around peak
        if (bestLag > -maxLag && bestLag < maxLag)
        {
            double corrM1 = ComputeCorrelationAtLag(left, right, meanL, meanR, normFactor, n, bestLag - 1);
            double corrP1 = ComputeCorrelationAtLag(left, right, meanL, meanR, normFactor, n, bestLag + 1);

            double denom = 2.0 * (2.0 * bestCorr - corrM1 - corrP1);
            if (Math.Abs(denom) > 1e-10)
            {
                double delta = (corrM1 - corrP1) / denom;
                // Only apply sub-sample correction if it's less than 1 sample
                if (Math.Abs(delta) < 1.0)
                {
                    // For integer lag output, round the refined estimate
                    double refinedLag = bestLag + delta;
                    bestLag = (int)Math.Round(refinedLag);
                }
            }
        }

        return (bestLag, Math.Max(0, bestCorr));
    }

    private static double ComputeCorrelationAtLag(
        float[] left, float[] right,
        double meanL, double meanR, double normFactor,
        int n, int lag)
    {
        double sum = 0;
        int start = Math.Max(0, lag);
        int end = Math.Min(n, n + lag);
        for (int i = start; i < end; i++)
        {
            double l = left[i] - meanL;
            double r = right[i - lag] - meanR;
            sum += l * r;
        }
        return sum / normFactor;
    }
}
