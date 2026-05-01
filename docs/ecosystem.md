# QuailTracker Ecosystem

<p align="center">
  <img src="logo.svg" alt="QuailTracker Logo" width="200" />
</p>

A general-purpose wildlife acoustic monitoring ecosystem built around three components: recording hardware (STM32U575 + ESP32-C3 companion radio), a desktop analyzer, and a training container. Species identification is powered by BirdNET (6,000+ species out of the box) with support for custom-trained models. Defaults are tuned for Northern Bobwhite quail, but every component is configurable for any vocalizing species — birds, frogs, bats, marine mammals, and more.

**Use cases:** population surveys, presence/absence monitoring, vocalization mapping, behavioral studies, TDOA source localization.

---

## Recording Hardware — Autonomous Recording Unit

An open-source, low-cost ARU (~$50/unit including enclosure, batteries, and antenna) designed for unattended multi-month deployment.

**Core hardware:**
- STM32U575 MCU (160 MHz Cortex-M33, 784 KB RAM, 1 MB flash)
- Infineon IM72D128 PDM MEMS microphone on a separate breakout board
- ATGM336H-5N31 GPS with PPS output for sub-millisecond time synchronization
- 32 GB MicroSD card for audio storage
- ESP32-C3 Super Mini companion radio: hosts a Wi-Fi access point + web UI for in-field configuration, BLE beacon for proximity, and STM32 OTA flashing over SPI2
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
- Configurable confidence threshold; detection events logged to CSV (with temperature and humidity) and surfaced in the on-device web UI

**Power management:**
- Two switched power rails (TPS22916 load switches, ~1 nA off): GPS_VCC, PERIPH_VCC (SD + SHT30). ESP32-C3 sits on always-on 3V3 (a switched rail caused boot deadlock).
- RTC wake for scheduled recordings; ESP32 brought up on demand for configuration sessions.
- GPS backup mode (~7 µA) preserves ephemeris for fast reacquisition
- Target: 90+ days on battery alone, indefinite with solar panel; Samsung 35E 18650 cells (3,500 mAh each)

**Multi-station networks:**
- PPS-synchronized timestamps across stations enable TDOA localization
- Minimum 3 stations with overlapping coverage to triangulate a sound source

**Adapting to other species:** Gain, bandpass filter, and sample rate are all configurable to match any target vocalization frequency range. The amplitude trigger and activity gate reduce storage for sparse callers. Any int8 TFLite model can be deployed — train one with the training container and push it via firmware build or the ESP32 OTA path.

---

## On-Device Configuration Web UI

The ESP32-C3 hosts a Wi-Fi access point and a self-served web interface — point a phone or laptop at the station's AP and configure it from any browser. No companion app required.

**Sections:**

- **Health** — battery voltage, GPS satellite count and signal quality, temperature, humidity, SD card usage.
- **Operations** — start/stop recording, audio level meter, current filename, bytes written, detection count.
- **Schedule** — sunrise/sunset toggle with pre/post offset, freeform time windows (up to 8), activity mode selection.
- **Config** — station ID, gain, bandpass filter frequencies, recording format (FLAC/WAV), low-battery threshold, detection confidence threshold.
- **Detection** — live inference results streamed from the device, per-segment species and confidence display.

**Survey-in:** GPS position averaging for precise, fixed station coordinates. SD card mount/eject control.

---

## Desktop Analyzer

A cross-platform desktop application (Windows, macOS, Linux) for audio analysis, species identification, localization, population modeling, and ML model training. Built with Avalonia 12 on .NET 10. Organized into four top-level tabs:

**Single Analysis** — Drag a recording in to see its mel spectrogram. Toggle noise reduction for the display and playback (audio fed to BirdNET is always raw — models were trained on unprocessed audio). Run BirdNET ONNX inference (sigmoid activation, scoring each class independently). Browse detections in a grid, see per-detection bearing for stereo recordings, export as BirdNET CSV or Raven Selection Tables.

**Bulk Analysis** — *Import*: batch load directories of WAV/FLAC files; define stations with GPS coordinates. *Processing*: multi-threaded BirdNET analysis across hundreds of files (scales to available CPU cores). Configurable confidence threshold, segment overlap, sensitivity, and merge window for combining nearby detections. Species filter to focus on target species.

**Analytics & Visualization** — *Localization*: TDOA from multi-station synchronized recordings (minimum 3 stations with overlapping detection). *Population*: N-mixture abundance models with Open-Meteo weather covariates. *Mapping*: interactive Cesium 3D map with Google satellite tiles, station / detection / localization markers, and KML export.

**Modeling** — End-to-end pipeline UI for the training container. *Data*: training-clip curation from the analyzer's own detections, plus a xeno-canto species download (autocomplete from BirdNET's 6,000-species catalog so typos can't create duplicate directories). *Training*: native hyperparameter form, **Start / Cancel**, live epoch progress with val_auc readout and a scrolling log, container reachability indicator. *Evaluation*: chart strip with training/validation curves and confusion matrices, artifact list with checkbox-selected download (`.tflite`, `.onnx`, `model_config.json`, etc.) into a folder of your choice.

The analyzer talks to the training container over HTTP (configurable URL persisted in app config). The container can be started directly from the analyzer's **Start Container** button in any of the Modeling sub-tabs.

**Adapting to other species:** BirdNET's 6,000+ species library works immediately for any supported species — just set the species filter. TDOA and population modeling are species-agnostic. The Modeling tab lets you build custom models for rare or regional species not well covered by BirdNET, end-to-end without leaving the app.

---

## Training Container

A Dockerized Python/TensorFlow pipeline with a Flask web UI **and** a REST API used by the analyzer's Modeling tab.

**Setup:** `python:3.11-slim` base image with TensorFlow 2.15+, birdnetlib, librosa, scikit-learn, and Flask. Default port 5050 (host) → 5000 (container). Compose-driven (`docker compose up -d --build` from `training/`).

**Web UI modes:** *Quick Train* (use clips already on disk) and *Full Pipeline* (download → train → export → evaluate).

**REST API surface used by the analyzer:**
- `GET /api/status` — current job state (idle / running / done / cancelled / error).
- `GET /api/progress` — Server-Sent Events stream of stage transitions, per-epoch metrics, and log lines.
- `POST /api/train`, `POST /api/full-pipeline`, `POST /api/download-species`, `POST /api/cancel` — job control.
- `GET /api/outputs`, `GET /api/outputs/<file>` — list and download artifacts.
- `GET /api/species` — union of BirdNET's canonical species list and species the user already has clips for; backs the species autocomplete in both the web UI and the analyzer.

**Pipeline stages:**

1. **Download & BirdNET verification** — Fetch recordings from xeno-canto by common name. Quality filtering (A/B/C grades), configurable max recordings per species. Each recording is analyzed by BirdNET to verify the target species is present — only 3-second clips where BirdNET confirms the species above a configurable confidence threshold (default 0.5) are extracted as training data. Segments with no detection become noise clips. Greedy deduplication ensures no overlapping clips share audio.

2. **Dataset generation** — Mel spectrograms (512-point FFT, 256 hop, 40 mels, 500–10,000 Hz). Data augmentation: time shift, gain variation, additive noise (white/pink/brown), SpecAugment (frequency and time masking), pitch shift. 80/20 train/validation split.

3. **Model training** — DS-CNN architecture: Conv2D(32) → depthwise-separable blocks (64, 128, 128) → global average pooling → dropout → sigmoid output. Focal loss, configurable epochs, early stopping on AUC. Weights saved via numpy serialization to avoid TF 2.15 serialization bugs.

4. **Export** — Int8 quantized TFLite model (~80–150 KB), C header file (`quail_model.h`) with model bytes as a const array for direct firmware embedding, and a JSON config for ESP32-OTA model deployment without rebuilding firmware.

5. **Evaluation** — Per-class precision/recall/F1, confusion matrix, ROC curves, and TFLite verification against the test set.

**Outputs:** `model.tflite`, `quail_model.h`, `labels.txt`, `training_history.json`, `model_weights.npz`, training-curves and confusion-matrix PNGs.

**Adapting to other species:** Pick any species from the BirdNET catalog (autocomplete in the analyzer or web UI). Adjust the BirdNET confidence threshold to control training data quality (higher = fewer but cleaner clips, lower = more clips with potential noise). The DS-CNN architecture and training pipeline are species-agnostic — only the data and confidence threshold change.

---

## How It All Connects

```
                         ┌─────────────────────┐
                         │    xeno-canto API    │
                         │  (species recordings)│
                         └────────┬─────────────┘
                                  │ download
                                  ▼
┌──────────────┐  REST   ┌─────────────────────┐
│   Analyzer   │◀──────▶ │  Training Container  │
│  (Avalonia)  │ /api/*  │  (Docker / Flask)    │
│              │         │                      │
│              │ ──────▶ │  verify → train →    │
│              │ training│  quantize → export   │
│              │  clips  │                      │
└──────┬───────┘         └────────┬─────────────┘
       │ load                     │ TFLite model +
       │ WAV/FLAC                 │ C header
       │ from SD                  ▼
       │                ┌─────────────────────┐
       │                │  Recording Hardware  │
       │◀───────────────│  (STM32U575 ARU)    │
       │   SD transfer  │                      │
       │                │  record → detect →   │
       │                │  store to SD         │
       │                └────────┬─────────────┘
       │                         │ Wi-Fi (in field)
       │                         ▼
       │                ┌─────────────────────┐
       │                │   ESP32-C3 web UI    │
       │                │  configure, monitor, │
       │                │  OTA model deploy    │
       │                └──────────────────────┘
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

- **Hardware → Analyzer** — SD card files (WAV/FLAC with GPS timestamps) load into the analyzer for BirdNET inference, TDOA localization, and population modeling.
- **Phone/laptop ↔ Hardware** — Wi-Fi connection to the ESP32's hosted web UI for real-time configuration (gain, schedule, filters, station ID), health monitoring, recording control, and detection streaming.
- **Analyzer ↔ Training Container** — REST + SSE on `localhost:5050` (configurable). The analyzer's Modeling tab drives every container endpoint; species lists, training jobs, and artifact downloads all flow through the same connection.
- **Training Container → Hardware** — Exported TFLite model is embedded in firmware via the C header, or transferred via ESP32 OTA using the JSON config path.
- **xeno-canto → Training Container** — The full pipeline downloads species-specific recordings directly from xeno-canto, verifies clips with BirdNET, and trains custom models.
- **BirdNET ONNX → Analyzer** — BirdNET's 6,000+ species model for identification. Custom TFLite models trained in the container can also be loaded for species not well covered by BirdNET.

---

## Adapting to Other Species

| Component | What to change |
|-----------|---------------|
| **Training Container** | Pick the target species from the autocomplete list. Adjust BirdNET confidence threshold to control training data quality. Run the full pipeline to produce a new TFLite model. |
| **Recording Hardware** | Set gain and bandpass filter to match the target frequency range from the on-device web UI. Deploy the new TFLite model via firmware build or ESP32 OTA. Adjust recording schedule if the species is nocturnal or has different activity peaks. Detection CSV includes temperature and humidity for environmental correlation. |
| **Analyzer** | BirdNET already covers 6,000+ species — set the species filter to your target. For rare or regional species, train a custom model from the Modeling tab and load its `.tflite`. Adjust population model parameters (covey/group size, survey occasions). |

Everything else — TDOA localization, population modeling, export formats, power management — works identically regardless of target species.
