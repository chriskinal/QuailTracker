/*
 * QuailTracker - Localization engine: detection-coverage layout
 * Copyright (C) 2026 QuailTracker Project
 *
 * Detection mode optimises for "is a call heard anywhere in the area" rather than
 * "where did it come from". A mic hears OMNIDIRECTIONALLY, so detection coverage is a
 * full 360° disc of the audibility radius (the ±90° hemisphere is only a bearing-
 * resolution limit, used by localization — not detection). Stations are spread to tile
 * the area with those discs.
 *
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Acoustics;

/// <summary>Omnidirectional detection test: is a point within a station's audibility radius?</summary>
internal static class DetectionRange
{
    public static bool Reaches(double sx, double sy, double px, double py, double detectRadius)
    {
        double dx = px - sx, dy = py - sy;
        return dx * dx + dy * dy <= detectRadius * detectRadius;
    }
}

/// <summary>Greedy maximum-coverage station placement for detection mode (omnidirectional discs).</summary>
public static class DetectionLayout
{
    private const double RadToDeg = 180.0 / Math.PI;

    /// <summary>
    /// Greedily place up to <paramref name="maxN"/> stations to maximise the fraction of the
    /// polygon within ≥1 station's audibility disc. Each step adds the position that newly
    /// covers the most area (mics are omnidirectional, so heading doesn't affect detection —
    /// each placed unit is simply aimed at the centroid for a sensible box orientation). A
    /// minimum-separation preference keeps the units spread rather than clustered. The first k
    /// of the returned layout IS the best k-station layout; <c>CoverageByCount[k-1]</c> its coverage.
    /// </summary>
    public static (ArrayStation[] Layout, double[] CoverageByCount) GreedySweep(
        IReadOnlyList<(double X, double Y)> polygon, int maxN, LocalizationParams p, int gridRes)
    {
        var v = PolyUtil.Normalize(polygon);
        if (v.Count < 3 || maxN < 1) return ([], []);
        if (gridRes < 2) gridRes = 2;

        double minX = v.Min(q => q.X), maxX = v.Max(q => q.X);
        double minY = v.Min(q => q.Y), maxY = v.Max(q => q.Y);
        var stepX = (maxX - minX) / (gridRes - 1);
        var stepY = (maxY - minY) / (gridRes - 1);

        var cells = new List<(double X, double Y)>();
        for (var iy = 0; iy < gridRes; iy++)
        for (var ix = 0; ix < gridRes; ix++)
        {
            double x = minX + ix * stepX, y = minY + iy * stepY;
            if (PolyUtil.Contains(v, x, y)) cells.Add((x, y));
        }
        if (cells.Count == 0) return ([], []);

        var candidates = Subsample(cells, 200);
        double cx = v.Average(q => q.X), cy = v.Average(q => q.Y);   // centroid (box-aim only)

        var covered = new bool[cells.Count];
        var layout = new List<ArrayStation>(maxN);
        var coverageByCount = new double[maxN];

        // Spread preference: keep units ≥ ~0.6× the natural even spacing apart.
        var area = cells.Count * stepX * stepY;
        var minSep2 = Math.Pow(0.6 * Math.Sqrt(area / Math.Max(maxN, 1)), 2);

        (double Gain, double X, double Y) FindBest(bool requireSep)
        {
            double bg = 0, bx = 0, by = 0;
            foreach (var c in candidates)
            {
                if (requireSep)
                {
                    var tooClose = false;
                    foreach (var s in layout)
                    {
                        double dx = c.X - s.X, dy = c.Y - s.Y;
                        if (dx * dx + dy * dy < minSep2) { tooClose = true; break; }
                    }
                    if (tooClose) continue;
                }
                var gain = 0;
                for (var i = 0; i < cells.Count; i++)
                    if (!covered[i] && DetectionRange.Reaches(c.X, c.Y, cells[i].X, cells[i].Y, p.DetectRadius))
                        gain++;
                if (gain > bg) { bg = gain; bx = c.X; by = c.Y; }
            }
            return (bg, bx, by);
        }

        for (var k = 0; k < maxN; k++)
        {
            var best = FindBest(requireSep: true);
            if (best.Gain <= 0) best = FindBest(requireSep: false);   // relax if separation blocks all gain

            if (best.Gain <= 0)
            {
                var last = k == 0 ? 0.0 : coverageByCount[k - 1];
                for (var j = k; j < maxN; j++) coverageByCount[j] = last;
                break;
            }

            for (var i = 0; i < cells.Count; i++)
                if (!covered[i] && DetectionRange.Reaches(best.X, best.Y, cells[i].X, cells[i].Y, p.DetectRadius))
                    covered[i] = true;

            var heading = (Math.Atan2(cx - best.X, cy - best.Y) * RadToDeg + 360) % 360; // aim at centre
            layout.Add(new ArrayStation(best.X, best.Y, heading));
            coverageByCount[k] = (double)CountTrue(covered) / cells.Count;
        }

        return (layout.ToArray(), coverageByCount);
    }

    private static int CountTrue(bool[] a)
    {
        var c = 0;
        foreach (var b in a) if (b) c++;
        return c;
    }

    private static List<(double X, double Y)> Subsample(List<(double X, double Y)> cells, int max)
    {
        if (cells.Count <= max) return cells;
        var outp = new List<(double X, double Y)>(max);
        var step = (double)cells.Count / max;
        for (var i = 0; i < max; i++) outp.Add(cells[(int)(i * step)]);
        return outp;
    }
}
