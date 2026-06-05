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
using System.Threading;
using System.Threading.Tasks;
using MathNet.Numerics.IntegralTransforms;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>Result of a cross-station TDOA estimate.</summary>
/// <param name="TdoaSeconds">arrival(other) − arrival(reference), in seconds. Positive = the source reached the other station later (it is farther).</param>
/// <param name="Confidence">0–1 peak-to-sidelobe quality of the GCC-PHAT correlation.</param>
/// <param name="LagSamples">Refined sub-sample lag on the common grid (for diagnostics).</param>
public sealed record CrossTdoaResult(double TdoaSeconds, double Confidence, double LagSamples);

/// <summary>
/// Estimates the time-difference-of-arrival of the same acoustic event between two
/// stations to sub-sample precision, using GCC-PHAT cross-correlation on windows
/// that are aligned to a common <b>absolute UTC</b> span via each file's PPS anchor.
///
/// Because both windows are extracted to represent the identical real-time interval
/// (using each recording's measured PPS sample rate, not the nominal header rate),
/// the correlation lag IS the inter-station TDOA — no separate coarse offset term.
/// Requires both files to carry a precise PPS time anchor (<see cref="AudioFile.HasPpsTiming"/>).
/// </summary>
public sealed class CrossStationTdoaService
{
    private readonly IAudioFileService _audio;

    /// <summary>Common resample grid for correlation (Hz). Both windows are placed on this grid.</summary>
    public const int CommonRate = 48000;

    public CrossStationTdoaService(IAudioFileService audio) => _audio = audio;

    /// <summary>
    /// Estimates TDOA(other − reference) for a call detected in <paramref name="refFile"/>
    /// at <paramref name="refOffsetSeconds"/>. Returns null if either file lacks PPS timing.
    /// </summary>
    public async Task<CrossTdoaResult?> EstimateAsync(
        AudioFile refFile, double refOffsetSeconds, double segmentDuration,
        AudioFile otherFile, double baselineMeters, double speedOfSound,
        CancellationToken ct = default)
    {
        if (!refFile.HasPpsTiming || !otherFile.HasPpsTiming)
            return null;
        if (speedOfSound <= 0) speedOfSound = 343.0;

        // Centre the search on the reference detection's segment centre, converted
        // to absolute UTC. Offset → native sample uses the header rate (that's the
        // grid the analyzer's segmentation indexed against); native → UTC then uses
        // the PPS anchor + measured rate.
        var refHeaderRate = refFile.SampleRate > 0 ? refFile.SampleRate : CommonRate;
        var refCentreNative = (refOffsetSeconds + segmentDuration / 2.0) * refHeaderRate;
        var centreUtc = refFile.NativeSampleToUtc(refCentreNative);

        // Window half-width: must contain the call in BOTH stations even after the
        // worst-case propagation shift (baseline / c) plus the call's own spread.
        var maxLagSec = baselineMeters / speedOfSound + 0.05;
        var halfWidthSec = maxLagSec + segmentDuration / 2.0 + 0.5;
        var spanSec = 2.0 * halfWidthSec;
        var winStartUtc = centreUtc.AddSeconds(-halfWidthSec);

        var outLen = (int)Math.Round(spanSec * CommonRate);
        if (outLen < 16) return null;

        var refWin = await ExtractAlignedAsync(refFile, winStartUtc, spanSec, outLen, ct);
        var otherWin = await ExtractAlignedAsync(otherFile, winStartUtc, spanSec, outLen, ct);
        if (refWin == null || otherWin == null) return null;

        var maxLag = (int)Math.Ceiling(maxLagSec * CommonRate);
        maxLag = Math.Min(maxLag, outLen / 2 - 1);
        if (maxLag < 1) return null;

        var (peakLag, conf) = GccPhat(refWin, otherWin, maxLag);

        // GCC peak at τ corresponds to arrival(other) − arrival(ref) = −τ (see GccPhat).
        var tdoaSeconds = -peakLag / CommonRate;
        return new CrossTdoaResult(tdoaSeconds, conf, peakLag);
    }

    /// <summary>
    /// Extracts a mono window covering the absolute UTC interval
    /// [<paramref name="winStartUtc"/>, +<paramref name="spanSec"/>] from a file and
    /// resamples it onto the common grid (<paramref name="outLen"/> samples at
    /// <see cref="CommonRate"/>), folding the file's fractional start sample into the
    /// resample phase so both stations land on the identical real-time grid.
    /// </summary>
    private async Task<float[]?> ExtractAlignedAsync(
        AudioFile file, DateTime winStartUtc, double spanSec, int outLen, CancellationToken ct)
    {
        var startExact = file.UtcToNativeSample(winStartUtc);
        var startFloor = (long)Math.Floor(startExact);
        var phase0 = startExact - startFloor; // sub-sample remainder, native samples

        // step = native input samples per common-grid output sample
        var step = file.PpsSampleRate!.Value / CommonRate;
        var nativeNeeded = (int)Math.Ceiling(spanSec * file.PpsSampleRate.Value) + 4;

        var (native, _) = await _audio.ExtractMonoNativeAsync(file.FilePath, startFloor, nativeNeeded, ct);
        if (native.Length == 0) return null;

        return ResampleLinear(native, phase0, step, outLen);
    }

    /// <summary>Linear resample starting at fractional input index <paramref name="phase0"/>, advancing <paramref name="step"/> input samples per output sample.</summary>
    private static float[] ResampleLinear(float[] x, double phase0, double step, int outLen)
    {
        var y = new float[outLen];
        for (var i = 0; i < outLen; i++)
        {
            var pos = phase0 + i * step;
            var i0 = (int)pos;
            var frac = pos - i0;
            if (i0 + 1 < x.Length) y[i] = (float)(x[i0] * (1.0 - frac) + x[i0 + 1] * frac);
            else if (i0 >= 0 && i0 < x.Length) y[i] = x[i0];
            // else leave zero (past end / before start)
        }
        return y;
    }

    /// <summary>
    /// Generalized cross-correlation with phase transform (GCC-PHAT) between equal-length
    /// signals <paramref name="a"/> and <paramref name="b"/>. Returns the sub-sample lag τ
    /// (samples) of the correlation peak within ±<paramref name="maxLag"/>, and a 0–1
    /// peak-to-sidelobe confidence.
    ///
    /// Convention: for b[k] = a[k − D] (the event arrives D samples later in b), the peak
    /// lands at τ = −D. So arrival(b) − arrival(a) = −τ.
    /// </summary>
    internal static (double lag, double confidence) GccPhat(float[] a, float[] b, int maxLag)
    {
        var n = Math.Min(a.Length, b.Length);
        if (n < 4) return (0, 0);

        var fftSize = 1;
        while (fftSize < 2 * n) fftSize <<= 1; // zero-pad to avoid circular wrap

        var A = new Complex[fftSize];
        var B = new Complex[fftSize];
        for (var i = 0; i < n; i++)
        {
            A[i] = new Complex(a[i], 0);
            B[i] = new Complex(b[i], 0);
        }

        Fourier.Forward(A, FourierOptions.NoScaling);
        Fourier.Forward(B, FourierOptions.NoScaling);

        var R = new Complex[fftSize];
        for (var i = 0; i < fftSize; i++)
        {
            var cross = A[i] * Complex.Conjugate(B[i]);
            var mag = cross.Magnitude;
            R[i] = mag > 1e-12 ? cross / mag : Complex.Zero; // PHAT whitening
        }

        Fourier.Inverse(R, FourierOptions.NoScaling);

        // Search ±maxLag (negative lags wrap to the top of the array).
        double peak = double.MinValue;
        var peakLag = 0;
        double sumSq = 0;
        var count = 0;
        for (var lag = -maxLag; lag <= maxLag; lag++)
        {
            var v = R[LagIndex(lag, fftSize)].Real;
            sumSq += v * v;
            count++;
            if (v > peak) { peak = v; peakLag = lag; }
        }

        // Parabolic sub-sample interpolation on the three points around the peak.
        var rm = R[LagIndex(peakLag - 1, fftSize)].Real;
        var rp = R[LagIndex(peakLag + 1, fftSize)].Real;
        var denom = rm - 2 * peak + rp;
        var delta = Math.Abs(denom) > 1e-12 ? 0.5 * (rm - rp) / denom : 0.0;
        if (Math.Abs(delta) >= 1.0) delta = 0.0;
        var refinedLag = peakLag + delta;

        // Confidence: peak vs RMS over the search range (peak-to-sidelobe ratio).
        var rms = count > 0 ? Math.Sqrt(sumSq / count) : 0.0;
        var psr = rms > 1e-12 ? peak / rms : 0.0;
        var confidence = Math.Clamp(1.0 - 1.0 / Math.Max(psr, 1e-6), 0.0, 1.0);

        return (refinedLag, confidence);
    }

    private static int LagIndex(int lag, int fftSize) => lag >= 0 ? lag : fftSize + lag;
}
