# QuailTracker

<p align="center">
  <img src="docs/logo.svg" alt="QuailTracker Logo" width="200" />
</p>

<p align="center">
  Open-source wildlife acoustic monitoring — hardware, analyzer, and ML training in one ecosystem.
</p>

---

QuailTracker is a complete bioacoustic monitoring system: low-cost recording hardware with on-device species detection, an ESP32-served web UI for in-field configuration, a desktop analyzer for post-processing, and a Dockerized model training pipeline. BirdNET provides 6,000+ species identification out of the box, with support for custom-trained on-device models. Defaults are tuned for Northern Bobwhite quail, but every component is configurable for any vocalizing species.

**Use cases:** population surveys, presence/absence monitoring, vocalization mapping, behavioral studies, TDOA source localization.

## NOT LICENSED FOR ANY COMMERCIAL PURPOSES.

## Components

### Recording Hardware — Autonomous Recording Unit (~$50/unit)

- **STM32U575** MCU (160 MHz Cortex-M33, 784 KB RAM) with **IM72D128** PDM MEMS mic
- **ATGM336H-5N31** GPS with PPS for sub-millisecond time synchronization across stations
- **ESP32-C3 Super Mini** companion radio: hosts a Wi-Fi access point + web UI for in-field configuration and STM32 OTA flashing over SPI2
- 48 kHz FLAC recording with GPS coordinates, temperature, and humidity in Vorbis metadata
- On-device **TFLite Micro** inference (DS-CNN, int8, ~80–150 KB) between audio frames
- **CN3791** solar MPPT charger — 90+ days on battery, indefinite with 6V/2W panel

### Configuration Web UI (ESP32-C3)

The recording station hosts its own web interface from the ESP32-C3 — point a phone or laptop at the station's Wi-Fi AP and configure gain, schedule, filters, station ID, and more from any browser. No companion app required. Health (battery, GPS, temperature, humidity) and live detection results are also served from the same UI.

### Desktop Analyzer (Windows / macOS / Linux)

Avalonia 12 / .NET 10 desktop app organized into four top-level tabs:

- **Single Analysis** — Mel spectrogram viewer with noise reduction toggle, BirdNET ONNX inference (sigmoid activation), drag-and-drop file loading, detection grid with bearing display for stereo recordings.
- **Bulk Analysis** — Multi-threaded BirdNET batch over directories of WAV/FLAC files; configurable confidence threshold, segment overlap, sensitivity, target-species filter, merge window.
- **Analytics & Visualization** — TDOA source localization from multi-station synchronized recordings, N-mixture population models with Open-Meteo weather covariates, interactive Cesium 3D map with Google satellite tiles and KML export.
- **Modeling** — *Data*: training-data curation from detections plus xeno-canto species download (autocomplete from BirdNET's 6,000-species catalog). *Training*: native UI for the training container — hyperparameter form, live epoch progress, val_auc readout, scrolling log, cancellable. *Evaluation*: chart strip with train/validation curves and confusion matrices, artifact list with checkbox-selected download (`.tflite`, `.onnx`, `model_config.json`, etc.).

The analyzer talks to the training container's REST API at `http://localhost:5050` (configurable). The container can be started directly from the analyzer via the **Start Container** button.

### Training Container (Docker)

Dockerized Python/TensorFlow pipeline with a Flask web UI *and* REST API:
1. Download from xeno-canto with **BirdNET-verified** clip extraction
2. Mel spectrogram dataset with augmentation (time shift, noise, SpecAugment, pitch)
3. DS-CNN training with focal loss and early stopping on AUC
4. Int8 TFLite export + C header for direct firmware embedding

The REST API (`/api/train`, `/api/progress` SSE, `/api/cancel`, `/api/outputs`, `/api/species`, etc.) backs the analyzer's native Modeling tab so you don't have to switch between app and browser to train.

## Repository Structure

```
QuailTracker/
├── stm32/QuailTracker_U575/   # STM32U575 firmware (CubeMX + PlatformIO)
├── esp32_bridge/              # ESP32-C3 companion radio firmware (PlatformIO)
├── analyzer/                  # Desktop analyzer (Avalonia 12 / .NET 10)
├── training/                  # Model training pipeline (Docker / Flask / TF)
├── hardware/                  # Schematics, BOM, pinout docs
├── docs/                      # Documentation
│   └── ecosystem.md           # Full ecosystem specification
└── README.md
```

## Getting Started

### Firmware (STM32U575)

```bash
pio run                    # build
pio run --target upload    # flash via J-Link
```

### Training Container

```bash
cd training
docker compose up -d --build
# Open http://localhost:5050, or start it from the analyzer's Modeling → Training tab
```

### Desktop Analyzer

```bash
dotnet run --project analyzer/Desktop
```

## Adapting to Other Species

| Component | What to change |
|-----------|---------------|
| **Training** | Pick a species from the BirdNET catalog (autocomplete in the analyzer or web UI), adjust BirdNET confidence threshold, run pipeline |
| **Hardware** | Set gain/filter for target frequency range via the on-device web UI, deploy new TFLite model |
| **Analyzer** | Set BirdNET species filter, adjust population model parameters |

Everything else — TDOA, population modeling, export formats, power management — works identically regardless of target species.

## Documentation

See [docs/ecosystem.md](docs/ecosystem.md) for the full ecosystem specification.

## License

[GNU General Public License v3.0](LICENSE.md)
