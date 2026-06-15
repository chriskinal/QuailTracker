/*
 * QuailTracker - Localization engine: survey-area evaluation
 * Copyright (C) 2026 QuailTracker Project
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Acoustics;

/// <summary>Rasters a survey area and aggregates per-point CRLB error into coverage stats.</summary>
public static class AreaModel
{
    /// <summary>
    /// Evaluate a layout over a disc of radius <paramref name="areaRadius"/> (centred on the
    /// origin) at <paramref name="gridRes"/>×<paramref name="gridRes"/> resolution, returning
    /// coverage within the target, fix count, and median 1σ error.
    /// </summary>
    public static AreaResult EvaluateDisc(
        IReadOnlyList<ArrayStation> stns, LocalizationMethod method, LocalizationParams p,
        double areaRadius, int gridRes)
    {
        var errors = new List<double>();
        int total = 0, fixes = 0, within = 0;
        var step = 2 * areaRadius / (gridRes - 1);

        for (var iy = 0; iy < gridRes; iy++)
        for (var ix = 0; ix < gridRes; ix++)
        {
            var x = -areaRadius + ix * step;
            var y = -areaRadius + iy * step;
            if (x * x + y * y > areaRadius * areaRadius) continue; // disc
            total++;

            var err = Crlb.PositionError(x, y, stns, method, p);
            if (err is { } e)
            {
                fixes++;
                errors.Add(e);
                if (e <= p.TargetError) within++;
            }
        }

        errors.Sort();
        return new AreaResult(
            Coverage: total == 0 ? 0 : (double)within / total,
            FixCount: fixes,
            MedianError: errors.Count == 0 ? 0 : errors[errors.Count / 2]);
    }

    /// <summary>
    /// Evaluate a layout over an arbitrary survey <paramref name="polygon"/> (vertices in
    /// local metres). Rasters the polygon's bounding box at <paramref name="gridRes"/>×
    /// <paramref name="gridRes"/> and keeps only points inside the polygon.
    /// </summary>
    public static AreaResult EvaluatePolygon(
        IReadOnlyList<ArrayStation> stns, LocalizationMethod method, LocalizationParams p,
        IReadOnlyList<(double X, double Y)> polygon, int gridRes)
    {
        var v = PolyUtil.Normalize(polygon);
        if (v.Count < 3) return new AreaResult(0, 0, 0);
        if (gridRes < 2) gridRes = 2;

        double minX = v.Min(q => q.X), maxX = v.Max(q => q.X);
        double minY = v.Min(q => q.Y), maxY = v.Max(q => q.Y);
        var stepX = (maxX - minX) / (gridRes - 1);
        var stepY = (maxY - minY) / (gridRes - 1);

        var errors = new List<double>();
        int total = 0, fixes = 0, within = 0;

        for (var iy = 0; iy < gridRes; iy++)
        for (var ix = 0; ix < gridRes; ix++)
        {
            var x = minX + ix * stepX;
            var y = minY + iy * stepY;
            if (!PolyUtil.Contains(v, x, y)) continue;
            total++;

            var err = Crlb.PositionError(x, y, stns, method, p);
            if (err is { } e)
            {
                fixes++;
                errors.Add(e);
                if (e <= p.TargetError) within++;
            }
        }

        errors.Sort();
        return new AreaResult(
            Coverage: total == 0 ? 0 : (double)within / total,
            FixCount: fixes,
            MedianError: errors.Count == 0 ? 0 : errors[errors.Count / 2]);
    }

    /// <summary>
    /// Detection coverage over <paramref name="polygon"/>: the fraction of area within ≥1
    /// station's audibility disc. Mics hear OMNIDIRECTIONALLY, so this is range-only (no
    /// ±90° hemisphere — that's a bearing limit, not a detection one). <see cref="AreaResult.Coverage"/>
    /// is the fraction; <see cref="AreaResult.FixCount"/> is the covered-cell count.
    /// </summary>
    public static AreaResult EvaluateDetectionPolygon(
        IReadOnlyList<ArrayStation> stns, LocalizationParams p,
        IReadOnlyList<(double X, double Y)> polygon, int gridRes)
    {
        var v = PolyUtil.Normalize(polygon);
        if (v.Count < 3) return new AreaResult(0, 0, 0);
        if (gridRes < 2) gridRes = 2;

        double minX = v.Min(q => q.X), maxX = v.Max(q => q.X);
        double minY = v.Min(q => q.Y), maxY = v.Max(q => q.Y);
        var stepX = (maxX - minX) / (gridRes - 1);
        var stepY = (maxY - minY) / (gridRes - 1);

        int total = 0, covered = 0;
        for (var iy = 0; iy < gridRes; iy++)
        for (var ix = 0; ix < gridRes; ix++)
        {
            var x = minX + ix * stepX;
            var y = minY + iy * stepY;
            if (!PolyUtil.Contains(v, x, y)) continue;
            total++;

            foreach (var s in stns)
                if (DetectionRange.Reaches(s.X, s.Y, x, y, p.DetectRadius))
                {
                    covered++;
                    break;
                }
        }

        return new AreaResult(total == 0 ? 0 : (double)covered / total, covered, 0);
    }
}
