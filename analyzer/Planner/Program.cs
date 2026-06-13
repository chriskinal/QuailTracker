/*
 * QuailTracker - Array Deployment Planner (CLI)
 * Copyright (C) 2026 QuailTracker Project
 *
 * Command-line front-end over the QuailTracker.Acoustics CRLB engine. Models
 * acoustic-localization error across a survey area for a candidate station layout
 * and compares localization methods:
 *   - TDOA     : time-of-arrival across PPS-synced stations (no front/back issue)
 *   - Bearing  : stereo direction-of-arrival only (front hemisphere, ±90°)
 *   - Fusion   : TDOA + stereo bearing combined
 *
 * Answers: "how few synced stations, and where, to localize anywhere in this
 * area to within X metres?" — and how much the stereo actually adds.
 *
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

using System.Globalization;
using QuailTracker.Acoustics;

var ci = CultureInfo.InvariantCulture;

// ---------------- configuration (override via --flags) ----------------
double centerLat = 32.591700, centerLon = -87.180000; // QT001/QT002 site
double areaRadius = 800;      // survey-area radius, metres (~1 mile diameter)
double targetErr = 50;        // acceptable 1σ position error, metres
double sigmaTms = 1.0;        // arrival-time std, ms (GCC-PHAT cross-correlation limited — SNR/reverb; was 5 ms for naive timestamp matching). GPS station-position error (~2-3 m) is a separate unmodeled floor.
double sigmaDeg = 10.0;       // stereo bearing std, degrees (≈ your 0.46 confidence)
double speedOfSound = 343.0;  // m/s
double detectRadius = 500;    // call audibility radius, metres (open-range bobwhite ≈ 500 m; NOT the ~1-mile foraging range)
int gridRes = 41;             // heatmap resolution
int nMin = 3, nMax = 8;       // station-count sweep
double? arrayRadiusOverride = null; // ring radius; default = areaRadius (perimeter)
double coverageGoal = 0.90;   // fraction of area within target for "meets goal"

for (var i = 0; i + 1 < args.Length; i += 2)
{
    var v = args[i + 1];
    switch (args[i])
    {
        case "--center": { var p = v.Split(','); centerLat = double.Parse(p[0], ci); centerLon = double.Parse(p[1], ci); break; }
        case "--radius": areaRadius = double.Parse(v, ci); break;
        case "--target": targetErr = double.Parse(v, ci); break;
        case "--sigma-t": sigmaTms = double.Parse(v, ci); break;
        case "--sigma-deg": sigmaDeg = double.Parse(v, ci); break;
        case "--detect": detectRadius = double.Parse(v, ci); break;
        case "--grid": gridRes = int.Parse(v, ci); break;
        case "--nmax": nMax = int.Parse(v, ci); break;
        case "--array-radius": arrayRadiusOverride = double.Parse(v, ci); break;
        case "--goal": coverageGoal = double.Parse(v, ci); break;
    }
}

double sigmaT = sigmaTms / 1000.0;
double arrayRadius = arrayRadiusOverride ?? areaRadius;
var proj = new GeoProjection(centerLat, centerLon);
var prm = LocalizationParams.FromUnits(sigmaTms, sigmaDeg, speedOfSound, detectRadius, targetErr);

Console.WriteLine("QuailTracker Array Deployment Planner");
Console.WriteLine("=====================================");
Console.WriteLine($"Survey area : radius {areaRadius:F0} m  (~{2 * areaRadius / 1609.0:F2} mile across) about {centerLat:F5},{centerLon:F5}");
Console.WriteLine($"Target      : 1σ position error ≤ {targetErr:F0} m, covering ≥ {coverageGoal:P0} of area");
Console.WriteLine($"Assumptions : timing σ {sigmaTms:F2} ms (→ {speedOfSound * sigmaT:F2} m range), bearing σ {sigmaDeg:F1}°, audible to {detectRadius:F0} m");
Console.WriteLine($"Layout      : {nMin}–{nMax} stations on a ring of radius {arrayRadius:F0} m, arrows aimed inward at centre");
Console.WriteLine();

// ---------------- sweep ----------------
Console.WriteLine("Coverage within target (and median 1σ error over points with a fix):");
Console.WriteLine();
Console.WriteLine("  N | TDOA  cov%  med    | Bearing  cov%  med    | Fusion  cov%  med");
Console.WriteLine("  --+--------------------+-----------------------+---------------------");

int? bestFusionN = null;
foreach (var n in Enumerable.Range(nMin, nMax - nMin + 1))
{
    var stns = RingLayout.Ring(n, arrayRadius);
    var t = Evaluate(stns, LocalizationMethod.Tdoa);
    var b = Evaluate(stns, LocalizationMethod.Bearing);
    var f = Evaluate(stns, LocalizationMethod.Fusion);

    Console.WriteLine(
        $"  {n,1} | {t.Coverage,5:P0} {Med(t),6} | {b.Coverage,8:P0} {Med(b),6} | {f.Coverage,7:P0} {Med(f),6}");

    if (bestFusionN is null && f.Coverage >= coverageGoal) bestFusionN = n;
}
Console.WriteLine();

// ---------------- recommended config + heatmap ----------------
var pickN = bestFusionN ?? nMax;
Console.WriteLine(bestFusionN is null
    ? $"No ring layout up to {nMax} stations meets the goal under Fusion — showing N={pickN} (best available)."
    : $"Smallest layout meeting the goal under Fusion: N = {pickN} stations.");
Console.WriteLine();

var chosen = RingLayout.Ring(pickN, arrayRadius);

Console.WriteLine($"Error map — Fusion (TDOA+bearing), N={pickN}  (north up):");
PrintHeatmap(chosen, LocalizationMethod.Fusion);
Console.WriteLine();
Console.WriteLine($"Error map — Bearing-only, same N={pickN} layout (for contrast):");
PrintHeatmap(chosen, LocalizationMethod.Bearing);
Console.WriteLine($"Legend:  @ ≤{targetErr:F0}m   O ≤{2 * targetErr:F0}m   o ≤{4 * targetErr:F0}m   . >{4 * targetErr:F0}m   (blank=no fix / outside area)   S=station");
Console.WriteLine();

Console.WriteLine($"Recommended station placement (ring, inward-facing), N={pickN}:");
Console.WriteLine("  #   Latitude     Longitude     Heading");
for (var k = 0; k < chosen.Length; k++)
{
    var s = chosen[k];
    var (lat, lon) = proj.ToGeo(s.X, s.Y);
    Console.WriteLine($"  {k + 1,-2}  {lat,11:F6}  {lon,12:F6}   {s.HeadingDeg,3:F0}°");
}
Console.WriteLine();
Console.WriteLine("Note: 1σ errors are a best-case Cramér-Rao bound (perfect call-matching, the stated noise).");
Console.WriteLine("Real fixes are worse; treat these as relative comparisons between layouts/methods.");

return;

// ======================= CLI helpers =======================

AreaResult Evaluate(ArrayStation[] stns, LocalizationMethod method)
    => AreaModel.EvaluateDisc(stns, method, prm, areaRadius, gridRes);

string Med(AreaResult r) => r.FixCount == 0
    ? "  -- "
    : (r.MedianError < 10 ? $"{r.MedianError,4:F1}m" : $"{r.MedianError,4:F0}m");

void PrintHeatmap(ArrayStation[] stns, LocalizationMethod method)
{
    var step = 2 * areaRadius / (gridRes - 1);
    for (var iy = gridRes - 1; iy >= 0; iy--) // north on top
    {
        var row = new char[gridRes];
        for (var ix = 0; ix < gridRes; ix++)
        {
            var x = -areaRadius + ix * step;
            var y = -areaRadius + iy * step;
            if (x * x + y * y > areaRadius * areaRadius) { row[ix] = ' '; continue; }
            if (NearStation(x, y, stns, step)) { row[ix] = 'S'; continue; }

            var err = Crlb.PositionError(x, y, stns, method, prm);
            char ch;
            if (err is not { } e) ch = ' ';
            else if (e <= targetErr) ch = '@';
            else if (e <= 2 * targetErr) ch = 'O';
            else if (e <= 4 * targetErr) ch = 'o';
            else ch = '.';
            row[ix] = ch;
        }
        Console.WriteLine("  " + new string(row));
    }
}

bool NearStation(double x, double y, ArrayStation[] stns, double step)
{
    foreach (var s in stns)
        if (Math.Abs(x - s.X) <= step / 2 && Math.Abs(y - s.Y) <= step / 2) return true;
    return false;
}
