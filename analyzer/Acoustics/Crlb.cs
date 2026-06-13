/*
 * QuailTracker - Localization engine: Cramér-Rao position-error model
 * Copyright (C) 2026 QuailTracker Project
 *
 * 1σ position error (DRMS, metres) for a candidate station layout, from the
 * Cramér-Rao lower bound. Best-case (perfect call-matching, stated noise); real
 * fixes are worse, so treat results as relative comparisons between layouts/methods.
 *
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Acoustics;

/// <summary>Cramér-Rao lower-bound position-error model for an acoustic station array.</summary>
public static class Crlb
{
    const double RadToDeg = 180.0 / Math.PI;

    /// <summary>
    /// 1σ position error (DRMS, metres) at survey point (<paramref name="px"/>,<paramref name="py"/>)
    /// for the given layout and method, or <c>null</c> if the point is not localizable
    /// (too few stations hear it, or the geometry is underdetermined/singular).
    /// </summary>
    public static double? PositionError(
        double px, double py, IReadOnlyList<ArrayStation> stns, LocalizationMethod method, LocalizationParams p)
    {
        double sigmaT = p.SigmaT, sigmaTheta = p.SigmaTheta;
        double speedOfSound = p.SpeedOfSound, detectRadius = p.DetectRadius;

        // Bearing-only: 2x2 Fisher information in (x,y).
        if (method == LocalizationMethod.Bearing)
        {
            double f00 = 0, f01 = 0, f11 = 0; int used = 0;
            foreach (var s in stns)
            {
                var (R, ux, uy) = Los(px, py, s);
                if (R > detectRadius || R < 1e-6) continue;
                if (!InFront(px, py, s)) continue;        // mirrored if behind → unusable
                double gx = -uy / R, gy = ux / R;          // ∂bearing/∂pos
                double w = 1.0 / (sigmaTheta * sigmaTheta);
                f00 += w * gx * gx; f01 += w * gx * gy; f11 += w * gy * gy;
                used++;
            }
            if (used < 2) return null;
            return RmsFrom2x2OrNull(f00, f01, f11);
        }

        // TDOA / Fusion: 3x3 Fisher information in (x,y,emitTime).
        double a00 = 0, a01 = 0, a02 = 0, a11 = 0, a12 = 0, a22 = 0; int heard = 0;
        foreach (var s in stns)
        {
            var (R, ux, uy) = Los(px, py, s);
            if (R > detectRadius || R < 1e-6) continue;
            heard++;

            // arrival-time row: ∂t/∂(x,y,τ) = (ux/c, uy/c, 1)
            double tx = ux / speedOfSound, ty = uy / speedOfSound, tt = 1.0;
            double wT = 1.0 / (sigmaT * sigmaT);
            a00 += wT * tx * tx; a01 += wT * tx * ty; a02 += wT * tx * tt;
            a11 += wT * ty * ty; a12 += wT * ty * tt; a22 += wT * tt * tt;

            if (method == LocalizationMethod.Fusion && InFront(px, py, s))
            {
                double gx = -uy / R, gy = ux / R;          // bearing row, 0 in τ
                double wB = 1.0 / (sigmaTheta * sigmaTheta);
                a00 += wB * gx * gx; a01 += wB * gx * gy; a11 += wB * gy * gy;
            }
        }
        if (heard < 2) return null; // τ not separable

        // Invert 3x3, take the (x,y) block.
        var cov = Invert3x3(a00, a01, a02, a11, a12, a22);
        if (cov is null) return null; // singular → underdetermined (e.g. TDOA with <3 heard)
        var (c00, _, _, c11, _, _) = cov.Value;
        if (c00 < 0 || c11 < 0) return null;
        return Math.Sqrt(c00 + c11);
    }

    static double? RmsFrom2x2OrNull(double f00, double f01, double f11)
    {
        var det = f00 * f11 - f01 * f01;
        if (Math.Abs(det) < 1e-12) return null;
        double c00 = f11 / det, c11 = f00 / det;
        if (c00 < 0 || c11 < 0) return null;
        return Math.Sqrt(c00 + c11);
    }

    static (double, double, double, double, double, double)? Invert3x3(
        double a00, double a01, double a02, double a11, double a12, double a22)
    {
        // symmetric: a10=a01, a20=a02, a21=a12
        double det =
            a00 * (a11 * a22 - a12 * a12)
          - a01 * (a01 * a22 - a12 * a02)
          + a02 * (a01 * a12 - a11 * a02);
        if (Math.Abs(det) < 1e-9) return null;
        double inv = 1.0 / det;
        double c00 = (a11 * a22 - a12 * a12) * inv;
        double c01 = (a02 * a12 - a01 * a22) * inv;
        double c02 = (a01 * a12 - a02 * a11) * inv;
        double c11 = (a00 * a22 - a02 * a02) * inv;
        double c12 = (a02 * a01 - a00 * a12) * inv;
        double c22 = (a00 * a11 - a01 * a01) * inv;
        return (c00, c01, c02, c11, c12, c22);
    }

    /// <summary>Line-of-sight from station <paramref name="s"/> to the point: range R and unit vector (ux,uy).</summary>
    static (double R, double ux, double uy) Los(double px, double py, ArrayStation s)
    {
        double dx = px - s.X, dy = py - s.Y;
        double R = Math.Sqrt(dx * dx + dy * dy);
        return R < 1e-9 ? (R, 0, 0) : (R, dx / R, dy / R);
    }

    /// <summary>True if the point lies in the station's front hemisphere (within ±90° of its heading).</summary>
    static bool InFront(double px, double py, ArrayStation s)
    {
        // compass bearing station→point, vs station heading; front = within ±90°.
        double bearing = Math.Atan2(px - s.X, py - s.Y) * RadToDeg; // (east,north)
        double diff = ((bearing - s.HeadingDeg) % 360 + 540) % 360 - 180;
        return Math.Abs(diff) <= 90.0;
    }
}
