/*
 * QuailTracker - Cross-station co-detection tally (headless diagnostic)
 * Copyright (C) 2026 QuailTracker Project
 *
 * Runs the analyzer's BirdNET service over a set of FLAC/WAV recordings and reports,
 * per species, how many cross-station co-detection events exist (a call detected at
 * >=3 stations within the matching window) — i.e. what's localizable by TDOA.
 * Reuses the production BirdNetService / AudioFileService / TdoaService so results
 * match the analyzer pipeline.
 *
 * Usage: qt-codetect [--model PATH] [--threshold 0.5] [--window-ms 3000] <files-or-dir...>
 *
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

using System.Globalization;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

var ci = CultureInfo.InvariantCulture;

string modelPath = "/Users/chris/Code/QuailTracker/models/Analyzer/birdnet.onnx";
double threshold = 0.5;
int windowMs = 3000;
var localize = false;
var inputs = new List<string>();

for (var i = 0; i < args.Length; i++)
{
    switch (args[i])
    {
        case "--model": modelPath = args[++i]; break;
        case "--threshold": threshold = double.Parse(args[++i], ci); break;
        case "--window-ms": windowMs = int.Parse(args[++i], ci); break;
        case "--localize": localize = true; break;
        default: inputs.Add(args[i]); break;
    }
}

// Expand directories to *.flac/*.wav.
var paths = new List<string>();
foreach (var a in inputs)
{
    if (Directory.Exists(a))
    {
        paths.AddRange(Directory.GetFiles(a, "*.flac"));
        paths.AddRange(Directory.GetFiles(a, "*.wav"));
    }
    else paths.Add(a);
}
paths.Sort();

if (paths.Count == 0)
{
    Console.Error.WriteLine("No input files. Usage: qt-codetect [--model P] [--threshold 0.5] [--window-ms 3000] <files-or-dir...>");
    return 1;
}

var audio = new AudioFileService();
using var bird = new BirdNetService();
Console.WriteLine($"Loading model: {modelPath}");
await bird.LoadModelAsync(modelPath);

// Load metadata for every file.
var files = new List<AudioFile>();
foreach (var p in paths)
{
    var af = await audio.LoadFileAsync(p);
    if (af.IsValid) files.Add(af);
    else Console.Error.WriteLine($"  skip {Path.GetFileName(p)}: {af.ErrorMessage}");
}
Console.WriteLine($"Loaded {files.Count} files from {files.Select(f => f.StationId).Distinct().Count()} stations.");
foreach (var f in files)
    Console.WriteLine($"  {Path.GetFileName(f.FilePath),-30} {f.StationId,-6} pps={(f.HasPpsTiming ? "yes" : "no ")} loc={f.Latitude:F6},{f.Longitude:F6}");

// Run detection across ALL species (targetSpecies: null) so we can pick a common substitute.
Console.WriteLine($"\nRunning BirdNET (threshold {threshold:F2}, all species)…");
var progress = new Progress<BirdNetProgress>(p =>
    Console.Write($"\r  file {p.CurrentFile}/{p.TotalFiles}  seg {p.CurrentSegment}/{p.TotalSegments}  {p.DetectionsFound} detections   "));
var detections = (await bird.AnalyzeBatchAsync(
    files, audio, confidenceThreshold: threshold,
    targetSpecies: null, overlapSeconds: 0.0, progress: progress)).ToList();
Console.WriteLine($"\nTotal detections: {detections.Count}");

// Re-anchor each detection's timestamp to true PPS UTC where available, so cross-station
// matching uses a common clock rather than per-station filename/RTC time.
var byPath = files.ToDictionary(f => f.FilePath);
foreach (var d in detections)
{
    if (!byPath.TryGetValue(d.AudioFilePath, out var af) || !af.HasPpsTiming) continue;
    if (af.EffectiveSampleRate is double rate &&
        af.NativeSampleToUtc(d.OffsetSeconds * rate) is DateTime utc)
        d.Timestamp = utc;
}

// Build the station list (id + survey location) for TDOA matching.
var stations = files
    .GroupBy(f => f.StationId)
    .Select(g => { var f = g.First(); return new Station { Id = f.StationId, Name = f.StationId, Latitude = f.Latitude ?? 0.0, Longitude = f.Longitude ?? 0.0 }; })
    .ToList();
var stationIds = stations.Select(s => s.Id).OrderBy(s => s).ToList();

// ---- Per-species / per-station detection tally ----
Console.WriteLine("\n================ Detections per species per station ================");
Console.WriteLine($"  (confidence >= {threshold:F2})\n");
Console.Write($"  {"Species",-34}");
foreach (var sid in stationIds) Console.Write($"{sid,7}");
Console.WriteLine($"{"stations",10}{"total",7}");

var perSpecies = detections
    .GroupBy(d => d.CommonName)
    .Select(g => new
    {
        Species = g.Key,
        ByStation = stationIds.ToDictionary(sid => sid, sid => g.Count(d => d.StationId == sid)),
        Stations = g.Select(d => d.StationId).Distinct().Count(),
        Total = g.Count(),
    })
    .OrderByDescending(x => x.Stations).ThenByDescending(x => x.Total)
    .ToList();

foreach (var s in perSpecies)
{
    Console.Write($"  {Trunc(s.Species, 34),-34}");
    foreach (var sid in stationIds) Console.Write($"{s.ByStation[sid],7}");
    Console.WriteLine($"{s.Stations,10}{s.Total,7}");
}

// ---- >=3 distinct-station co-detections via the (now fixed) analyzer matcher ----
// Pass the audio service so LocalizeAsync can PPS-refine via GCC-PHAT cross-correlation.
var tdoa = new TdoaService(audio) { MaxTimeDifferenceMs = windowMs };
var matches = tdoa.MatchDetections(detections, stations);

Console.WriteLine($"\n========= Cross-station co-detections (>=3 distinct stations, within {windowMs} ms) =========");
Console.WriteLine("  (via TdoaService.MatchDetections)\n");
if (matches.Count == 0)
{
    Console.WriteLine("  None. No species was detected at 3 distinct stations within the window.");
    Console.WriteLine("  (Lower --threshold or widen --window-ms to surface marginal events.)");
}
else
{
    foreach (var g in matches.GroupBy(m => m.Species).OrderByDescending(g => g.Count()))
        Console.WriteLine($"  {Trunc(g.Key, 34),-34}{g.Count(),14} events");

    Console.WriteLine("\n  Events (distinct stations + best confidence each):");
    foreach (var m in matches.OrderBy(m => m.ReferenceTime))
    {
        var who = string.Join(" ", m.Detections.OrderBy(d => d.Station.Id).Select(d => $"{d.Station.Id}@{d.Detection.Confidence:F2}"));
        Console.WriteLine($"    {m.ReferenceTime:yyyy-MM-dd HH:mm:ss}  {Trunc(m.Species, 28),-28} [{who}]");
    }
}

// ---- End-to-end TDOA localization (GCC-PHAT cross-correlation + multilateration) ----
if (localize && matches.Count > 0)
{
    Console.WriteLine($"\n========= TDOA localization (PPS-anchored GCC-PHAT) =========\n");
    var locs = await tdoa.LocalizeAllAsync(matches);
    Console.WriteLine($"  {"When (UTC)",-20}{"Species",-26}{"Position (lat,lon)",-26}{"timing",-8}{"resid",-9}{"ellipse m",-12}qual");
    foreach (var l in locs.OrderBy(l => l.Timestamp))
        Console.WriteLine($"  {l.Timestamp:yyyy-MM-dd HH:mm:ss} " +
                          $"{Trunc(l.Species, 25),-26}{l.Latitude:F6},{l.Longitude:F6}   " +
                          $"{l.TimingSource,-8}{l.ResidualError,7:F1}  {l.ErrorEllipseMajor,5:F0}x{l.ErrorEllipseMinor,-5:F0}{l.QualityScore,5:F2}");
    Console.WriteLine($"\n  Localized {locs.Count}/{matches.Count} matches; {locs.Count(l => l.PpsRefined)} PPS-refined.");
}
else if (localize)
{
    Console.WriteLine("\n  (--localize: no >=3-station matches to localize at this threshold.)");
}

return 0;

static string Trunc(string s, int n) => s.Length <= n ? s : s[..(n - 1)] + "…";
