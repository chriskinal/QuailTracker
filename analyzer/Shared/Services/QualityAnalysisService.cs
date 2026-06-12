/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. See <https://www.gnu.org/licenses/>.
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Threading;
using System.Threading.Tasks;
using MathNet.Numerics.IntegralTransforms;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Pure-DSP recording-quality analysis: objective signal metrics (level, clipping,
/// SNR, spectral balance, channel balance, dropouts) mapped to explainable,
/// actionable suggestions. No ML model — every result says exactly what to change
/// and why. Runs entirely off the decoded samples the analyzer already loads.
/// </summary>
public sealed class QualityAnalysisService
{
    private const int FftSize = 2048;
    private const int HopSize = 1024;
    private const int MaxFftFrames = 4000;       // cap spectral work on long files
    private const double DbFloor = -120.0;       // dBFS for silence

    public async Task<QualityReport> AnalyzeAsync(
        string filePath,
        IAudioFileService audioFileService,
        IProgress<string>? progress = null,
        CancellationToken ct = default)
    {
        progress?.Report("Decoding audio…");
        var (left, right, sampleRate) = await audioFileService.LoadAllStereoSamplesAsync(filePath, ct);

        bool stereo = left.Length > 0 && right.Length > 0;
        if (!stereo)
        {
            // Mono file (stereo loader returns empty) — fall back to the mono path.
            var mono = await audioFileService.LoadAllSamplesAsync(filePath, ct);
            sampleRate = sampleRate > 0 ? sampleRate : 48000;
            left = mono;
            right = Array.Empty<float>();
        }

        return await Task.Run(() => Analyze(left, right, stereo, sampleRate, progress, ct), ct);
    }

    private static QualityReport Analyze(
        float[] left, float[] right, bool stereo, int sampleRate,
        IProgress<string>? progress, CancellationToken ct)
    {
        progress?.Report("Measuring levels…");
        var l = ChannelStats(left);
        var r = stereo ? ChannelStats(right) : default;

        // Combined level metrics (both channels).
        double peak = stereo ? Math.Max(l.Peak, r.Peak) : l.Peak;
        long nTotal = l.N + (stereo ? r.N : 0);
        double rms = Math.Sqrt((l.SumSq + (stereo ? r.SumSq : 0)) / Math.Max(1, nTotal));
        long clipped = l.ClipCount + (stereo ? r.ClipCount : 0);
        double dcMax = stereo ? Math.Max(Math.Abs(l.Mean), Math.Abs(r.Mean)) : Math.Abs(l.Mean);

        double peakDb = Db(peak), rmsDb = Db(rms);
        double headroom = -peakDb;
        double crest = peakDb - rmsDb;
        double clippedPct = nTotal > 0 ? 100.0 * clipped / nTotal : 0;
        double dcDb = Db(dcMax);
        double imbalance = stereo ? Math.Abs(Db(l.Rms) - Db(r.Rms)) : double.NaN;

        ct.ThrowIfCancellationRequested();
        progress?.Report("Estimating noise floor…");

        // Mono mix for noise floor / activity / spectrum.
        float[] mix = stereo ? MonoMix(left, right) : left;
        var (noiseDb, signalDb, activityPct) = NoiseAndActivity(mix, sampleRate);
        double snr = signalDb - noiseDb;

        ct.ThrowIfCancellationRequested();
        progress?.Report("Analyzing spectrum…");
        var (windPct, birdPct) = SpectralBands(mix, sampleRate, ct);

        progress?.Report("Checking for dropouts…");
        int dropouts = CountDropouts(mix, sampleRate);

        // ── Ratings + report-card rows ───────────────────────────────────────
        var metrics = new List<QualityMetric>
        {
            Metric("Peak level", $"{peakDb:F1} dBFS",
                   peakDb is >= -12 and <= -2 ? QualityRating.Good
                   : peakDb is (>= -20 and < -12) or (> -2 and <= -1) ? QualityRating.Fair
                   : QualityRating.Poor,
                   Norm(peakDb, -60, 0)),
            Metric("RMS level", $"{rmsDb:F1} dBFS", QualityRating.Good, Norm(rmsDb, -70, 0)),
            Metric("Headroom", $"{headroom:F1} dB",
                   headroom is >= 2 and <= 18 ? QualityRating.Good : headroom > 18 ? QualityRating.Fair : QualityRating.Poor,
                   Norm(headroom, 0, 40)),
            Metric("Clipping", $"{clippedPct:F3} %",
                   clippedPct < 0.001 ? QualityRating.Good : clippedPct < 0.1 ? QualityRating.Fair : QualityRating.Poor,
                   Math.Min(1, clippedPct / 1.0)),
            Metric("Noise floor", $"{noiseDb:F1} dBFS", QualityRating.Good, Norm(noiseDb, -100, -20)),
            Metric("SNR", $"{snr:F1} dB",
                   snr >= 30 ? QualityRating.Good : snr >= 15 ? QualityRating.Fair : QualityRating.Poor,
                   Norm(snr, 0, 60)),
            Metric("Activity", $"{activityPct:F0} %",
                   activityPct >= 10 ? QualityRating.Good : activityPct >= 3 ? QualityRating.Fair : QualityRating.Poor,
                   activityPct / 100.0),
            Metric("Low-freq (<300 Hz)", $"{windPct:F0} %",
                   windPct < 5 ? QualityRating.Good : windPct < 15 ? QualityRating.Fair : QualityRating.Poor,
                   Math.Min(1, windPct / 50.0)),
            Metric("Quail band (1–5 kHz)", $"{birdPct:F0} %", QualityRating.Good, birdPct / 100.0),
            Metric("DC offset", $"{dcDb:F0} dBFS",
                   dcDb < -60 ? QualityRating.Good : dcDb < -40 ? QualityRating.Fair : QualityRating.Poor,
                   Norm(dcDb, -90, -20)),
        };
        if (stereo)
            metrics.Add(Metric("L/R balance", $"{imbalance:F1} dB",
                imbalance < 3 ? QualityRating.Good : imbalance < 6 ? QualityRating.Fair : QualityRating.Poor,
                Math.Min(1, imbalance / 12.0)));
        metrics.Add(Metric("Dropouts", dropouts.ToString(),
            dropouts == 0 ? QualityRating.Good : QualityRating.Poor, dropouts == 0 ? 0 : 1));

        var suggestions = BuildSuggestions(peakDb, clippedPct, snr, windPct, imbalance, dcDb, dropouts, activityPct, stereo);
        int score = Score(metrics);

        return new QualityReport
        {
            IsStereo = stereo,
            SampleRate = sampleRate,
            DurationSeconds = (double)l.N / Math.Max(1, sampleRate),
            PeakDbfs = peakDb, RmsDbfs = rmsDb, HeadroomDb = headroom, CrestFactorDb = crest,
            ClippedPercent = clippedPct, NoiseFloorDbfs = noiseDb, SnrDb = snr, DcOffsetDbfs = dcDb,
            ChannelImbalanceDb = imbalance, WindEnergyPercent = windPct, BirdBandEnergyPercent = birdPct,
            ActivityPercent = activityPct, DropoutCount = dropouts,
            Score = score, Metrics = metrics, Suggestions = suggestions,
        };
    }

    // ── Metric helpers ───────────────────────────────────────────────────────

    private struct Stats { public double Peak, SumSq, Sum, Rms, Mean; public long N, ClipCount; }

    private static Stats ChannelStats(float[] x)
    {
        double peak = 0, sumSq = 0, sum = 0; long clip = 0;
        for (int i = 0; i < x.Length; i++)
        {
            double s = x[i], a = Math.Abs(s);
            if (a > peak) peak = a;
            sumSq += s * s; sum += s;
            if (a >= 0.999) clip++;
        }
        double rms = x.Length > 0 ? Math.Sqrt(sumSq / x.Length) : 0;
        return new Stats { Peak = peak, SumSq = sumSq, Sum = sum, Rms = rms,
                           Mean = x.Length > 0 ? sum / x.Length : 0, N = x.Length, ClipCount = clip };
    }

    private static float[] MonoMix(float[] l, float[] r)
    {
        int n = Math.Min(l.Length, r.Length);
        var m = new float[n];
        for (int i = 0; i < n; i++) m[i] = 0.5f * (l[i] + r[i]);
        return m;
    }

    /// <summary>Noise floor = 10th-percentile frame RMS; signal = 90th-percentile;
    /// activity = share of frames above (noise + 6 dB). 100 ms frames.</summary>
    private static (double noiseDb, double signalDb, double activityPct) NoiseAndActivity(float[] x, int sr)
    {
        int frame = Math.Max(1, sr / 10);
        int frames = x.Length / frame;
        if (frames < 4) return (Db(Rms(x, 0, x.Length)), Db(Rms(x, 0, x.Length)), 0);

        var rmsv = new double[frames];
        for (int f = 0; f < frames; f++) rmsv[f] = Rms(x, f * frame, frame);
        var sorted = (double[])rmsv.Clone();
        Array.Sort(sorted);
        double noise = sorted[(int)(frames * 0.10)];
        double signal = sorted[(int)(frames * 0.90)];
        double thresh = noise * 1.9953; // +6 dB
        int active = rmsv.Count(v => v > thresh);
        return (Db(noise), Db(signal), 100.0 * active / frames);
    }

    /// <summary>Share of spectral energy below 300 Hz (wind/handling) and in the
    /// 1–5 kHz quail band. FFT on Hann frames, capped for long files.</summary>
    private static (double windPct, double birdPct) SpectralBands(float[] x, int sr, CancellationToken ct)
    {
        int frames = Math.Max(0, (x.Length - FftSize) / HopSize);
        if (frames <= 0) return (0, 0);
        int step = Math.Max(1, frames / MaxFftFrames);

        var hann = new double[FftSize];
        for (int i = 0; i < FftSize; i++) hann[i] = 0.5 - 0.5 * Math.Cos(2 * Math.PI * i / (FftSize - 1));
        double binHz = (double)sr / FftSize;

        double eTot = 0, eWind = 0, eBird = 0;
        var buf = new Complex[FftSize];
        for (int f = 0; f < frames; f += step)
        {
            if ((f & 0x3F) == 0) ct.ThrowIfCancellationRequested();
            int off = f * HopSize;
            for (int i = 0; i < FftSize; i++) buf[i] = new Complex(x[off + i] * hann[i], 0);
            Fourier.Forward(buf, FourierOptions.Default);
            for (int b = 1; b < FftSize / 2; b++)
            {
                double p = buf[b].Real * buf[b].Real + buf[b].Imaginary * buf[b].Imaginary;
                double hz = b * binHz;
                eTot += p;
                if (hz < 300) eWind += p;
                else if (hz is >= 1000 and <= 5000) eBird += p;
            }
        }
        if (eTot <= 0) return (0, 0);
        return (100.0 * eWind / eTot, 100.0 * eBird / eTot);
    }

    /// <summary>Count runs of ≥10 ms of exactly-zero samples — a proxy for inserted
    /// silence / SD write gaps (real audio always carries some mic noise).</summary>
    private static int CountDropouts(float[] x, int sr)
    {
        int minRun = Math.Max(16, sr / 100);
        int dropouts = 0, run = 0; bool counted = false;
        for (int i = 0; i < x.Length; i++)
        {
            if (x[i] == 0f) { if (++run >= minRun && !counted) { dropouts++; counted = true; } }
            else { run = 0; counted = false; }
        }
        return dropouts;
    }

    private static List<QualitySuggestion> BuildSuggestions(
        double peakDb, double clippedPct, double snr, double windPct,
        double imbalance, double dcDb, int dropouts, double activityPct, bool stereo)
    {
        var s = new List<QualitySuggestion>();

        if (clippedPct >= 0.01)
            s.Add(Sug(QualitySeverity.Critical, "Gain", $"Reduce gain — {clippedPct:F2}% of samples are clipped (peaks at full scale)."));
        else if (peakDb < -18)
        {
            int step = Math.Min(6, (int)(Math.Round((-12 - peakDb) / 3.0) * 3)); // toward ~-12 dBFS, 3 dB steps, capped
            if (step >= 3)
                s.Add(Sug(QualitySeverity.Warning, "Gain",
                    $"Increase gain ~+{step} dB — peaks sit {Math.Abs(peakDb):F0} dB below full scale (lots of unused headroom). Device gain is in 3 dB steps (max +24); bump and re-check."));
        }

        if (windPct >= 15)
            s.Add(Sug(QualitySeverity.Warning, "Spectrum",
                $"{windPct:F0}% of energy is below 300 Hz — likely wind or handling noise. Raise the high-pass filter or shield/reposition the mic."));

        if (snr < 15)
            s.Add(Sug(QualitySeverity.Warning, "SNR",
                $"Low SNR ({snr:F0} dB) — quiet environment or low gain. More gain (if not clipping) improves detection confidence."));

        if (stereo && imbalance >= 6)
            s.Add(Sug(QualitySeverity.Warning, "Channels",
                $"L/R level imbalance is {imbalance:F0} dB — check the mic and the board's two PDM edges."));

        if (dcDb > -40)
            s.Add(Sug(QualitySeverity.Info, "DC offset",
                $"Measurable DC offset ({dcDb:F0} dBFS) — usually a mic/filter issue; the firmware HPF should remove it."));

        if (dropouts > 0)
            s.Add(Sug(QualitySeverity.Critical, "Dropouts",
                $"{dropouts} silence gap(s) detected — possible SD write dropouts; check the card/recording chain."));

        if (activityPct < 3)
            s.Add(Sug(QualitySeverity.Info, "Activity",
                $"Very little acoustic activity ({activityPct:F0}% of frames above noise) — review mic placement or gain."));

        if (s.Count == 0)
            s.Add(Sug(QualitySeverity.Good, "Overall", "Recording quality looks good — no changes suggested."));
        return s;
    }

    private static int Score(IEnumerable<QualityMetric> metrics)
    {
        int score = 100;
        foreach (var m in metrics)
            score -= m.Rating == QualityRating.Poor ? 18 : m.Rating == QualityRating.Fair ? 7 : 0;
        return Math.Clamp(score, 0, 100);
    }

    // ── Small utilities ──────────────────────────────────────────────────────
    private static double Db(double x) => x > 1e-9 ? 20.0 * Math.Log10(x) : DbFloor;
    private static double Rms(float[] x, int off, int len)
    {
        double sum = 0; int end = Math.Min(off + len, x.Length);
        for (int i = off; i < end; i++) sum += (double)x[i] * x[i];
        int n = end - off;
        return n > 0 ? Math.Sqrt(sum / n) : 0;
    }
    private static double Norm(double v, double lo, double hi) => Math.Clamp((v - lo) / (hi - lo), 0, 1);
    private static QualityMetric Metric(string name, string val, QualityRating r, double bar) =>
        new() { Name = name, Value = val, Rating = r, BarFraction = bar };
    private static QualitySuggestion Sug(QualitySeverity sev, string cat, string msg) =>
        new() { Severity = sev, Category = cat, Message = msg };
}
