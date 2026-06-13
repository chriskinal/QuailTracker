/*
 * QuailTracker - Localization engine: polygon helpers
 * Copyright (C) 2026 QuailTracker Project
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Acoustics;

/// <summary>Small 2D-polygon helpers (local metric frame), shared by layout + evaluation.</summary>
internal static class PolyUtil
{
    /// <summary>Return the vertices with any closing duplicate (last == first) removed.</summary>
    public static List<(double X, double Y)> Normalize(IReadOnlyList<(double X, double Y)> poly)
    {
        var v = new List<(double X, double Y)>(poly);
        if (v.Count >= 2 && Coincident(v[0], v[^1])) v.RemoveAt(v.Count - 1);
        return v;
    }

    /// <summary>Vertex-average centroid (good enough for inward-facing headings).</summary>
    public static (double X, double Y) Centroid(IReadOnlyList<(double X, double Y)> v)
        => (v.Average(p => p.X), v.Average(p => p.Y));

    /// <summary>Even-odd ray-cast point-in-polygon test.</summary>
    public static bool Contains(IReadOnlyList<(double X, double Y)> v, double x, double y)
    {
        var inside = false;
        for (int i = 0, j = v.Count - 1; i < v.Count; j = i++)
        {
            if ((v[i].Y > y) != (v[j].Y > y) &&
                x < (v[j].X - v[i].X) * (y - v[i].Y) / (v[j].Y - v[i].Y) + v[i].X)
                inside = !inside;
        }
        return inside;
    }

    private static bool Coincident((double X, double Y) a, (double X, double Y) b)
        => Math.Abs(a.X - b.X) < 1e-9 && Math.Abs(a.Y - b.Y) < 1e-9;
}
