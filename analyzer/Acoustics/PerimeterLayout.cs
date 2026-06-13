/*
 * QuailTracker - Localization engine: perimeter station layout
 * Copyright (C) 2026 QuailTracker Project
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Acoustics;

/// <summary>Places stations around the boundary of an arbitrary survey polygon.</summary>
public static class PerimeterLayout
{
    /// <summary>
    /// <paramref name="n"/> stations spaced evenly by arc length around the
    /// <paramref name="polygon"/> boundary (vertices in local metres, open or closed),
    /// each aimed inward at the polygon centroid. The arbitrary-shape analogue of
    /// <see cref="RingLayout.Ring(int, double)"/>.
    /// </summary>
    public static ArrayStation[] Ring(IReadOnlyList<(double X, double Y)> polygon, int n)
    {
        var v = PolyUtil.Normalize(polygon);
        if (v.Count < 3 || n < 1) return [];

        var (cx, cy) = PolyUtil.Centroid(v);
        var m = v.Count;

        var seg = new double[m];
        double perimeter = 0;
        for (var i = 0; i < m; i++)
        {
            seg[i] = Dist(v[i], v[(i + 1) % m]);
            perimeter += seg[i];
        }
        if (perimeter <= 0) return [];

        var stns = new ArrayStation[n];
        for (var k = 0; k < n; k++)
        {
            var target = perimeter * k / n;
            double acc = 0;
            var si = 0;
            while (si < m - 1 && acc + seg[si] < target) { acc += seg[si]; si++; }

            var t = seg[si] > 0 ? (target - acc) / seg[si] : 0;
            var (ax, ay) = v[si];
            var (bx, by) = v[(si + 1) % m];
            double x = ax + (bx - ax) * t, y = ay + (by - ay) * t;

            var heading = (Math.Atan2(cx - x, cy - y) * 180.0 / Math.PI + 360) % 360; // aim at centre
            stns[k] = new ArrayStation(x, y, heading);
        }
        return stns;
    }

    private static double Dist((double X, double Y) a, (double X, double Y) b)
    {
        double dx = a.X - b.X, dy = a.Y - b.Y;
        return Math.Sqrt(dx * dx + dy * dy);
    }
}
