# QuailTracker Ecosystem

A general-purpose wildlife acoustic monitoring ecosystem built around four components: recording hardware, a companion mobile app, a desktop analyzer, and a training container. Species identification is powered by BirdNET (6,000+ species out of the box) with support for custom-trained models. Defaults are tuned for Northern Bobwhite quail, but every component is configurable for any vocalizing species — birds, frogs, bats, marine mammals, and more.

**Use cases:** population surveys, presence/absence monitoring, vocalization mapping, behavioral studies, TDOA source localization.

---

## Recording Hardware — Autonomous Recording Unit

An open-source, low-cost ARU (~$50/unit including enclosure, batteries, and antenna) designed for unattended multi-month deployment.

**Core hardware:**
- STM32U575 MCU (160 MHz Cortex-M33, 784 KB RAM, 1 MB flash)
- Infineon IM72D128 PDM MEMS microphone on a separate breakout board
- ATGM336H-5N31 GPS with PPS output for sub-millisecond time synchronization
- 32 GB MicroSD card for audio storage
- Ai-Thinker PB-03F BLE 5.2 module for wireless configuration
- SHT30 temperature/humidity sensor
- 1S2P 18650 Li-ion pack (7 Ah) with NCP170 LDO (500 nA quiescent)
- CN3791 solar MPPT charger with 6V/2W panel for indefinite field runtime

**Recording:**
- 48 kHz 24-bit PDM capture, stored as 16-bit WAV or FLAC
- GPS-timestamped filenames (`YYYYMMDD_HHMMSS_<station_id>.flac`)
- FLAC Vorbis comment metadata: GPS coordinates, PPS sync status, station ID, temperature, humidity
- Configurable gain (0–24 dB in 3 dB steps), bandpass filter (HPF/LPF), and sample rate
- Amplitude trigger with pre-roll and post-roll for activity-gated recording
- Scheduling: sunrise/sunset with configurable offsets, up to 8 freeform time windows, or continuous

**On-device detection:**
- TensorFlow Lite Micro inference on 3-second clips (24 kHz, 40-mel spectrogram)
- DS-CNN model (int8 quantized, ~80–150 KB) runs in real time between audio frames
- Mission modes: record only, detect only, or both
- Configurable confidence threshold; detection events logged to CSV (with temperature and humidity) and streamed over BLE

**Power management:**
- Three switched power rails (TPS22916 load switches, ~1 nA off): GPS_VCC, BLE_VCC, PERIPH_VCC (SD + SHT30)
- Stop 2 sleep with BLE advertising: ~350 µA total (BLE deep sleep + MCU ~5 µA)
- BLE wake-on-connect from Stop 2 via UART EXTI; RTC wake for scheduled recordings
- GPS backup mode (~7 µA) preserves ephemeris for fast reacquisition
- Target: 90+ days on battery alone, indefinite with solar panel; Samsung 35E 18650 cells (3,500 mAh each)
- Solar charge status (charging/done) reported via BLE

**Multi-station networks:**
- PPS-synchronized timestamps across stations enable TDOA localization
- Minimum 3 stations with overlapping coverage to triangulate a sound source

**Adapting to other species:** Gain, bandpass filter, and sample rate are all configurable to match any target vocalization frequency range. The amplitude trigger and activity gate reduce storage for sparse callers. Any int8 TFLite model can be deployed — train one with the training container and push it via firmware build or BLE.

---

## Companion Mobile App

A cross-platform mobile application for configuring and monitoring stations in the field over BLE.

**Platforms:** iOS (15+), Android (API 21+), and desktop (Windows/macOS/Linux) — built with Avalonia 11.2 on .NET 10, using Plugin.BLE for Bluetooth connectivity.

**Tabs:**

- **Health** — Battery voltage, GPS satellite count and signal quality, temperature, humidity, SD card usage. Refresh button for live polling.

- **Operations** — Start/stop recording, real-time audio level meter (peak dBFS and activity percentage), current filename, bytes written, detection count, buffer status.

- **Schedule** — Sunrise/sunset toggle with pre/post offset sliders, freeform time window editor (up to 8 windows), activity mode selection (off, monitor, squelch, gate with holdoff).

- **Config** — Station ID, gain, bandpass filter frequencies, recording format (FLAC/WAV), low-battery threshold, detection confidence threshold.

- **Detection** — Live inference results streamed from the device, per-segment species and confidence display, detection log.

**Additional features:**
- Survey-in: GPS position averaging for precise, fixed station coordinates
- SD card mount/eject control
- Designed for one-handed field use (400×700 mobile-optimized layout)

---

## Desktop Analyzer

A cross-platform desktop application (Windows, macOS, Linux) for audio analysis, species identification, localization, and population modeling. Built with Avalonia 11.2 on .NET 10.

**Import** — Batch load directories of WAV/FLAC files. Define stations with GPS coordinates. File metadata display (duration, sample rate, recording timestamp).

**Single Analysis** — Load an individual recording and view its mel spectrogram. Toggle noise reduction for the display and playback (audio fed to BirdNET is always raw — models were trained on unprocessed audio). Run BirdNET ONNX inference and browse detections by species, time, and confidence. Export detections as BirdNET CSV or Raven Selection Tables.

**Batch Processing** — Multi-threaded BirdNET analysis across hundreds of files (scales to available CPU cores). Configurable confidence threshold, segment overlap, sensitivity, and merge window for combining nearby detections. Species filter to focus on target species. Progress tracking per file and segment.

**Training Data** — Curate BirdNET detections into labeled training sets. Export positive and negative WAV clips ready for model retraining in the training container.

**Localization** — TDOA source localization from multi-station synchronized recordings. Requires a minimum of 3 stations with overlapping detection of the same vocalization. Configurable speed-of-sound and maximum time-difference threshold. Outputs localization points with accuracy estimates.

**Population** — N-mixture models for abundance estimation. Aggregates detections into survey occasions, fetches weather covariates from Open-Meteo, and fits models to estimate detection probability and population size. Configurable covey size and survey parameters.

**Map** — Interactive Leaflet map displaying stations, detections, and localization results. Click to inspect individual points. KML export for use in Google Earth or GIS software.

**Adapting to other species:** BirdNET's 6,000+ species library works immediately for any supported species — just set the species filter. TDOA and population modeling are species-agnostic. The training data export lets you build custom models for rare or regional species not well covered by BirdNET.

---

## Training Container

A Dockerized Python/TensorFlow pipeline with a web UI for training custom on-device detection models.

**Setup:** `python:3.11-slim` base image with TensorFlow 2.15+, birdnetlib, librosa, scikit-learn, and Flask. Exposed on port 5000. Two modes in the web UI: **Quick Train** (upload your own clips) and **Full Pipeline** (download → train → export).

**Pipeline stages:**

1. **Download & BirdNET verification** — Fetch recordings from xeno-canto by common name. Quality filtering (A/B/C grades), configurable max recordings per species. Each recording is analyzed by BirdNET to verify the target species is present — only 3-second clips where BirdNET confirms the species above a configurable confidence threshold (default 0.5) are extracted as training data. Segments with no detection become noise clips. Greedy deduplication ensures no overlapping clips share audio.

2. **Dataset generation** — Mel spectrograms (512-point FFT, 256 hop, 40 mels, 500–10,000 Hz). Data augmentation: time shift, gain variation, additive noise (white/pink/brown), SpecAugment (frequency and time masking), pitch shift. 80/20 train/validation split.

3. **Model training** — DS-CNN architecture: Conv2D(32) → depthwise-separable blocks (64, 128, 128) → global average pooling → dropout → sigmoid output. Focal loss, 200 epochs, early stopping on AUC. Weights saved via numpy serialization to avoid TF 2.15 serialization bugs.

4. **Export** — Int8 quantized TFLite model (~80–150 KB), C header file (`quail_model.h`) with model bytes as a const array for direct firmware embedding, and a JSON config for BLE-based model deployment without rebuilding firmware.

5. **Evaluation** — Per-class precision/recall/F1, confusion matrix, ROC curves, and TFLite verification against the test set.

**Outputs:** `model.tflite`, `quail_model.h`, `labels.txt`, `training_history.json`, `model_weights.npz`.

**Adapting to other species:** Enter any species name to download training data from xeno-canto. Adjust the BirdNET confidence threshold to control training data quality (higher = fewer but cleaner clips, lower = more clips with potential noise). The DS-CNN architecture and training pipeline are species-agnostic — only the data and confidence threshold change.

---

## How It All Connects

```
                         ┌─────────────────────┐
                         │    xeno-canto API    │
                         │  (species recordings)│
                         └────────┬─────────────┘
                                  │ download
                                  ▼
┌──────────────┐        ┌─────────────────────┐
│   Analyzer   │───────▶│  Training Container  │
│  (Desktop)   │ export │  (Docker/Flask)      │
│              │training│                      │
│              │  clips │  verify → train →    │
│              │        │  quantize → export   │
└──────┬───────┘        └────────┬─────────────┘
       │ load                    │ TFLite model +
       │ WAV/FLAC                │ C header
       │ files                   ▼
       │               ┌─────────────────────┐
       │               │  Recording Hardware  │
       │◀──────────────│  (STM32U575 ARU)    │
       │   SD card     │                      │
       │   transfer    │  record → detect →   │
       │               │  store to SD         │
       │               └────────┬─────────────┘
       │                        │ BLE
       │                        ▼
       │               ┌─────────────────────┐
       │               │   Companion App      │
       │               │  (iOS / Android)     │
       │               │                      │
       │               │  configure, monitor, │
       │               │  deploy models       │
       │               └──────────────────────┘
       │
       ▼
  ┌──────────┐
  │  Outputs  │
  ├──────────┤
  │ BirdNET CSV
  │ Raven tables
  │ KML maps
  │ Population estimates
  │ TDOA localizations
  └──────────┘
```

**Key data flows:**

- **Hardware → Analyzer** — SD card files (WAV/FLAC with GPS timestamps) are loaded into the analyzer for BirdNET inference, TDOA localization, and population modeling.
- **App ↔ Hardware** — BLE connection for real-time configuration (gain, schedule, filters, station ID), health monitoring, recording control, detection streaming, and model deployment.
- **Analyzer → Training Container** — Curated training clips (positive calls and negative noise segments) exported from the analyzer's training data tab feed into the container's Quick Train mode.
- **Training Container → Hardware** — Exported TFLite model is embedded in firmware via the C header, or transferred over BLE using the JSON config path. No firmware rebuild needed for BLE deployment.
- **xeno-canto → Training Container** — The full pipeline downloads species-specific recordings directly from xeno-canto, verifies clips with BirdNET, and trains custom models.
- **BirdNET ONNX → Analyzer** — The analyzer runs BirdNET's 6,000+ species model for identification. Custom TFLite models trained in the container can also be used for species not well covered by BirdNET.

---

## Adapting to Other Species

To retarget the entire ecosystem for a different species:

| Component | What to change |
|-----------|---------------|
| **Training Container** | Enter the target species name. Adjust BirdNET confidence threshold to control training data quality. Run the full pipeline to produce a new TFLite model. |
| **Recording Hardware** | Set gain and bandpass filter to match the target frequency range. Deploy the new TFLite model via firmware build or BLE. Adjust recording schedule if the species is nocturnal or has different activity peaks. Detection CSV includes temperature and humidity for environmental correlation. |
| **Analyzer** | BirdNET already covers 6,000+ species — set the species filter to your target. For rare or regional species, load a custom model trained in the container. Adjust population model parameters (covey/group size, survey occasions). |
| **Companion App** | Update gain, filter, and schedule settings on each station to match the new species profile. No app changes needed — all parameters are already configurable. |

Everything else — TDOA localization, population modeling, export formats, power management — works identically regardless of target species.
