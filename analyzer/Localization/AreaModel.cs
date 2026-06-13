/*
 * QuailTracker - Localization engine: survey-area evaluation
 * Copyright (C) 2026 QuailTracker Project
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Localization;

/// <summary>Rasters a survey area and aggregates per-point CRLB error into coverage stats.</summary>
public static class AreaModel
{
    /// <summary>
    /// Evaluate a layout over a disc of radius <paramref name="areaRadius"/> (centred on the
    /// origin) at <paramref name="gridRes"/>×<paramref name="gridRes"/> resolution, returning
    /// coverage within the target, fix count, and median 1σ error.
    /// </summary>
    public static AreaResult EvaluateDisc(
        IReadOnlyList<Station> stns, LocalizationMethod method, LocalizationParams p,
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
}
