# Analyzer Guide

The QuailTracker analyzer is the desktop side of the system: it
processes recordings off SD cards, runs BirdNET inference, localizes
sound sources across multiple stations using TDOA, fits population
models, and trains custom on-device species detectors. It runs on
macOS, Windows, and Linux from the same .NET 10 / Avalonia 12 codebase.

This guide assumes you've already pulled recordings off a station
(WAV or FLAC files, with GPS metadata in the Vorbis comments for
FLAC) and you want to actually look at the data.

## Installing and running

You need:

- **.NET 10 SDK** — install from <https://dotnet.microsoft.com/download>.
- (Optional) **Docker Desktop** — only if you want to train custom
  models. The analyzer will work fine without Docker; training is
  the only feature that depends on it.

Build and launch:

```bash
git clone <repo URL>
cd QuailTracker
dotnet run --project analyzer/Desktop
```

First run takes a minute (NuGet restore + first build). Subsequent
launches are quick.

The analyzer ships with everything you need bundled — BirdNET ONNX
models drop into a configurable directory, Cesium 3D globe loads
Google satellite tiles when online, no API keys required for the
core features. Xeno-canto downloads do require an API key (free,
register at <https://xeno-canto.org/>); set it from the Modeling →
Data tab when you need it.

## The four top-level tabs

The analyzer is organized into four tabs across the top:

| Tab | What it's for |
|-----|---------------|
| **Single Analysis** | Inspect one recording at a time — spectrogram, BirdNET detections, stereo bearing |
| **Bulk Analysis** | Batch-process directories of recordings into a detection database |
| **Analytics & Visualization** | TDOA source localization, population models, 3D map |
| **Modeling** | Curate training data, train custom species models, evaluate results |

## Single Analysis tab

The fastest way to look at one recording. Use this when you want to
sanity-check a deployment or investigate a single detection in detail.

**Header bar**

- File name and basic info (sample rate, channel count, duration).
- **Model** field — shows whether a BirdNET ONNX model is loaded and
  its path. Use **Load Model…** to point at one.

**Controls**

- **Open File…** or drag-and-drop a WAV / FLAC file into the
  spectrogram pane.
- **Spectrogram** checkbox — toggle the mel spectrogram render.
- **Noise Reduction** checkbox — applies a spectral-subtraction
  cleanup to the *display* and *playback* only. The noise-reduced
  audio is **not** sent to BirdNET — the model was trained on raw
  audio and performs worse with NR applied.
- **Max Freq** slider — caps the spectrogram's vertical range
  (helpful when looking at species with narrow vocalization bands).
- **Confidence**, **Sensitivity**, **Overlap (s)**, **Merge** —
  BirdNET inference parameters. Defaults are reasonable; see the
  BirdNET-Analyzer documentation if you want to tune them.

**Analyze** runs BirdNET across the loaded file with the current
settings. **Cancel** stops a run in progress.

**Detection grid**

Below the spectrogram, a table lists every detection with start time,
end time, species, confidence, and (for stereo recordings) the
estimated bearing in degrees relative to the mic array's "front"
direction. The bearing comes from inter-channel phase difference at
the dominant frequency of the call — accurate to roughly ±15° in
quiet conditions, less in noisy ones.

Click a row to seek the spectrogram to that detection's time range.

## Bulk Analysis tab

For processing whole deployments. Two sub-tabs:

### Import

Drag in (or browse for) a folder of WAV/FLAC files. The grid shows
each file's station ID, timestamp, duration, sample rate, and any
import errors (corrupted, wrong rate, etc.).

The bottom half of the tab manages **Stations** — a small database of
station IDs paired with latitude/longitude. The analyzer reads station
ID and timestamp from each file's metadata where it can; for legacy
files without metadata, you can match files to stations manually here.

For TDOA work, you must have a station entry with coordinates for
every station whose recordings you import — the localization step
needs the array geometry.

### Processing

Once files are imported, the Processing tab runs BirdNET in batch over
all of them. Multi-threaded — uses all available CPU cores. Progress
bar + per-file status; cancellable.

The output is a per-file detection list saved next to each audio file
(or to a configurable output directory). These detection records feed
the Localization, Population, and Mapping views downstream.

## Analytics & Visualization tab

Three sub-tabs, all working from the detection database produced by
Bulk Analysis.

### Localization

Cross-station TDOA source localization. Pick a species and a time
window; the analyzer finds detections of that species at multiple
stations within the GPS-PPS-aligned window, computes the inter-station
arrival-time differences, and triangulates an estimated source
position.

Requires:

- **Three or more** stations with synchronized recordings (PPS-locked
  GPS clocks are why the firmware ties hard to PPS).
- **Known station coordinates** (set in the Import tab).
- A **detection match** of the same species across stations within
  the species' typical call duration.

Outputs an estimated lat/lon for the source plus an uncertainty
ellipse based on the residual cross-correlation peaks.

### Population

N-mixture occupancy/abundance models for population estimation. Pulls
hourly weather covariates from Open-Meteo (free, no API key) at each
station's coordinates and feeds them into a hierarchical Bayesian
model alongside the detection counts.

Output is per-site abundance estimates with credible intervals,
plottable across time. Use this when your goal is a population
density estimate over a survey period rather than localizing
individual calls.

### Mapping

Interactive Cesium 3D globe with Google satellite imagery. Plots
station locations, detection events (color-coded by species or
confidence), and TDOA-localized source positions.

Useful for:

- Visual sanity-checking station placement before deployment.
- Spotting clusters of detections — territories, dawn-chorus hotspots.
- **KML export** for sharing with collaborators or pulling into
  Google Earth / GIS tools.

## Modeling tab

For when you want a custom on-device detector — a species model the
station can run in TFLite Micro between audio frames, in addition to
or instead of BirdNET.

Three sub-tabs:

### Data

Two ways to build a training set:

- **Curate from detections** — pull confirmed BirdNET detections out
  of your own recordings (review, accept/reject, label) to build a
  positive-class set anchored to your deployment site's acoustics.
- **Xeno-canto download** — type a species name (autocomplete pulls
  from BirdNET's full ~6000-species catalog), filter by quality and
  location, and download example calls. Requires an API key set in
  this tab.

The combined dataset feeds the Training tab.

### Training

A native UI for the training container's REST API. The container is
a Dockerized Python/TensorFlow pipeline that does the heavy lifting;
this tab lets you drive it without leaving the analyzer.

- **Start Container** — first-time setup; the analyzer runs
  `docker compose up -d --build` on the bundled `training/` directory.
  After the initial build the container starts in seconds.
- **Hyperparameter form** — epochs, batch size, learning rate,
  augmentation toggles (time shift, noise, SpecAugment, pitch).
- **Run** — kicks off training. The right pane shows live epoch
  progress, the latest val_auc, and a scrolling log streamed from the
  container.
- **Cancel** — stops the run cleanly.

If the analyzer can't reach the training container (Docker not
running, port 5050 blocked), every Modeling tab shows an "offline"
status and most actions stay disabled until reachability returns.

### Evaluation

After a successful training run:

- Chart strip: train/validation loss curves and confusion matrix.
- Artifact list: `quail_model.tflite` (for the firmware), `.onnx` (for
  the analyzer's BirdNET-style inference), `model_config.json`
  (mel parameters + class labels), `mel_filterbank.h` and
  `quail_model.h` (C headers for direct firmware embedding).
- Each artifact has a checkbox; **Save Selected** writes the chosen
  files to a folder you pick.

Drop the `.tflite` and `model_config.json` onto the SD card's
`/model/` directory and reload the model from the station's web UI
(Detect tab → Reload) to start running your custom detector.

## Common workflows

### Quick look at a single field recording

1. **Single Analysis** tab.
2. Drag in a WAV/FLAC.
3. **Load Model…** for a BirdNET ONNX model if not already loaded.
4. **Analyze**. Skim the detection grid. Click rows to seek.

### Process a multi-day deployment

1. **Bulk Analysis → Import** — drag the recording folder, confirm
   stations have coordinates.
2. **Bulk Analysis → Processing** — run, wait for it to finish.
3. **Analytics & Visualization → Mapping** — see where detections
   landed. Export KML if needed.
4. **Analytics & Visualization → Localization** — pick a target
   species and time window for source positions.
5. **Analytics & Visualization → Population** — fit an N-mixture model
   for abundance estimates.

### Train a custom on-device species detector

1. **Modeling → Data** — curate from your detections, or download
   xeno-canto clips for the target species (and at least one
   negative class — silence or non-target birds).
2. **Modeling → Training** — Start Container if not already running,
   set hyperparameters, run. Monitor live val_auc; cancel and
   re-run if the curves don't look right.
3. **Modeling → Evaluation** — review the confusion matrix and
   training curves, then save the artifacts.
4. Copy `quail_model.tflite` + `model_config.json` to a station's SD
   card under `/model/`.
5. Open the station's [web UI](web_ui.md), go to the Detect tab, tap
   **Reload**. The new model is now running.

### Visualize stations before deployment

1. **Bulk Analysis → Import** — add stations with their planned
   coordinates (no audio files needed).
2. **Analytics & Visualization → Mapping** — confirm placement
   geometry on the satellite imagery. For TDOA, the array should
   span the species' typical territory size with overlapping
   detection radii.

## Where the analyzer fits in the bigger picture

This is the last of the four guides. End-to-end you've now:

1. [**Built the hardware**](board_build.md) — main board + stereo
   mic breakouts.
2. [**Bootstrapped the firmware**](bootstrap.md) — ESP32 over USB-C,
   STM32 over the web UI.
3. [**Configured the station**](web_ui.md) — schedule, gain,
   mission mode, station ID, mic heading.
4. **Processed the recordings** — this guide.

Anything that breaks in production usually surfaces in one of those
four layers; the guides loop back into one another via cross-links to
help you triage.

Welcome to the rabbit hole.
