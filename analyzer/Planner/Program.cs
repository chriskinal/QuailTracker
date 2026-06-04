/*
 * QuailTracker - Array Deployment Planner
 * Copyright (C) 2026 QuailTracker Project
 *
 * Models acoustic-localization error (Cramér-Rao lower bound) across a survey
 * area for a candidate station layout, and compares localization methods:
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

var ci = CultureInfo.InvariantCulture;

// ---------------- configuration (override via --flags) ----------------
double centerLat = 32.591700, centerLon = -87.180000; // QT001/QT002 site
double areaRadius = 800;      // survey-area radius, metres (~1 mile diameter)
double targetErr = 50;        // acceptable 1σ position error, metres
double sigmaTms = 5.0;        // arrival-time std, milliseconds (call-matching limited; PPS clock is sub-ms)
double sigmaDeg = 10.0;       // stereo bearing std, degrees (≈ your 0.46 confidence)
double speedOfSound = 343.0;  // m/s
double detectRadius = 1609;   // call audibility radius, metres (~1 mile)
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
double sigmaTheta = sigmaDeg * Math.PI / 180.0;
double arrayRadius = arrayRadiusOverride ?? areaRadius;
const double DegToRad = Math.PI / 180.0;
const double RadToDeg = 180.0 / Math.PI;
const double MetersPerDegLat = 111_320.0;
double metersPerDegLon = MetersPerDegLat * Math.Cos(centerLat * DegToRad);

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
    var stns = Ring(n, arrayRadius);
    var t = Evaluate(stns, Method.Tdoa);
    var b = Evaluate(stns, Method.Bearing);
    var f = Evaluate(stns, Method.Fusion);

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

var chosen = Ring(pickN, arrayRadius);

Console.WriteLine($"Error map — Fusion (TDOA+bearing), N={pickN}  (north up):");
PrintHeatmap(chosen, Method.Fusion);
Console.WriteLine();
Console.WriteLine($"Error map — Bearing-only, same N={pickN} layout (for contrast):");
PrintHeatmap(chosen, Method.Bearing);
Console.WriteLine($"Legend:  @ ≤{targetErr:F0}m   O ≤{2 * targetErr:F0}m   o ≤{4 * targetErr:F0}m   . >{4 * targetErr:F0}m   (blank=no fix / outside area)   S=station");
Console.WriteLine();

Console.WriteLine($"Recommended station placement (ring, inward-facing), N={pickN}:");
Console.WriteLine("  #   Latitude     Longitude     Heading");
for (var k = 0; k < chosen.Length; k++)
{
    var s = chosen[k];
    var lat = centerLat + s.Y / MetersPerDegLat;
    var lon = centerLon + s.X / metersPerDegLon;
    Console.WriteLine($"  {k + 1,-2}  {lat,11:F6}  {lon,12:F6}   {s.HeadingDeg,3:F0}°");
}
Console.WriteLine();
Console.WriteLine("Note: 1σ errors are a best-case Cramér-Rao bound (perfect call-matching, the stated noise).");
Console.WriteLine("Real fixes are worse; treat these as relative comparisons between layouts/methods.");

return;

// ======================= model =======================

string Med(AreaResult r) => r.HasFixCount == 0
    ? "  -- "
    : (r.MedianError < 10 ? $"{r.MedianError,4:F1}m" : $"{r.MedianError,4:F0}m");

AreaResult Evaluate(Stn[] stns, Method method)
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

        var err = PositionError(x, y, stns, method);
        if (err is { } e)
        {
            fixes++;
            errors.Add(e);
            if (e <= targetErr) within++;
        }
    }

    errors.Sort();
    return new AreaResult(
        Coverage: total == 0 ? 0 : (double)within / total,
        HasFixCount: fixes,
        MedianError: errors.Count == 0 ? 0 : errors[errors.Count / 2]);
}

// 1σ position error (DRMS, metres) from the CRLB, or null if not localizable.
double? PositionError(double px, double py, Stn[] stns, Method method)
{
    // Bearing-only: 2x2 Fisher information in (x,y).
    if (method == Method.Bearing)
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

        if (method == Method.Fusion && InFront(px, py, s))
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

double? RmsFrom2x2OrNull(double f00, double f01, double f11)
{
    var det = f00 * f11 - f01 * f01;
    if (Math.Abs(det) < 1e-12) return null;
    double c00 = f11 / det, c11 = f00 / det;
    if (c00 < 0 || c11 < 0) return null;
    return Math.Sqrt(c00 + c11);
}
(double,double,double,double,double,double)? Invert3x3(
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

(double R, double ux, double uy) Los(double px, double py, Stn s)
{
    double dx = px - s.X, dy = py - s.Y;
    double R = Math.Sqrt(dx * dx + dy * dy);
    return R < 1e-9 ? (R, 0, 0) : (R, dx / R, dy / R);
}

bool InFront(double px, double py, Stn s)
{
    // compass bearing station→point, vs station heading; front = within ±90°.
    double bearing = Math.Atan2(px - s.X, py - s.Y) * RadToDeg; // (east,north)
    double diff = ((bearing - s.HeadingDeg) % 360 + 540) % 360 - 180;
    return Math.Abs(diff) <= 90.0;
}

Stn[] Ring(int n, double r)
{
    var stns = new Stn[n];
    for (var k = 0; k < n; k++)
    {
        double phi = 2 * Math.PI * k / n;
        double x = r * Math.Sin(phi), y = r * Math.Cos(phi);   // east, north
        double heading = (Math.Atan2(-x, -y) * RadToDeg + 360) % 360; // aim at centre
        stns[k] = new Stn(x, y, heading);
    }
    return stns;
}

void PrintHeatmap(Stn[] stns, Method method)
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

            var err = PositionError(x, y, stns, method);
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

bool NearStation(double x, double y, Stn[] stns, double step)
{
    foreach (var s in stns)
        if (Math.Abs(x - s.X) <= step / 2 && Math.Abs(y - s.Y) <= step / 2) return true;
    return false;
}

enum Method { Tdoa, Bearing, Fusion }
record struct Stn(double X, double Y, double HeadingDeg);
record struct AreaResult(double Coverage, int HasFixCount, double MedianError);
