# QuailTracker

Low-cost GPS-synchronized Autonomous Recording Units (ARUs) for bobwhite quail population monitoring.

## Overview

QuailTracker is an open-source bioacoustic monitoring system designed to track quail populations through passive acoustic surveillance. Networks of recording stations capture vocalizations with GPS-synchronized timestamps, enabling Time Difference of Arrival (TDOA) localization to pinpoint covey locations.

## Features

- **GPS Time Synchronization:** ±1ms accuracy across all stations via PPS
- **High-Performance Audio:** 80 dB SNR microphone with ~1km detection range
- **Long Battery Life:** 90+ days on 2× 18650 cells
- **Low Cost:** ~$35/unit in single quantities
- **Weatherproof:** IP65-rated enclosure for field deployment

## Hardware

| Component | Part | Purpose |
|-----------|------|---------|
| MCU | ESP32 (dual-core) | Control, I2S audio, BLE config |
| Microphone | PUI AOM-5024L-HD-R | 80 dB SNR electret condenser |
| ADC | PCM1808 | 24-bit I2S analog-to-digital |
| GPS | Quectel L76K | Time sync via PPS output |
| Storage | MicroSD 32GB | WAV file storage |
| Power | 1S4P 18650 + HT7333 | 3.3V regulated supply |

## Repository Structure

```
QuailTracker/
├── docs/                    # Documentation
│   └── design_document.md   # Full design specification
├── firmware/                # ESP32 firmware (PlatformIO)
├── hardware/                # Schematics and PCB files
├── analysis/                # Audio analysis scripts
├── CLAUDE.md               # AI assistant context
└── README.md               # This file
```

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 DevKit board
- PCM1808 ADC module
- PUI AOM-5024L-HD-R microphone
- Quectel L76K GPS module

### Building Firmware

```bash
cd firmware
pio run
pio run --target upload
```

## Documentation

See [docs/design_document.md](docs/design_document.md) for complete design specifications including:

- System architecture and block diagrams
- Component selection rationale
- Bill of materials
- Wiring diagrams
- Power budget analysis
- TDOA coverage analysis

## License

[MIT License](LICENSE)

## Acknowledgments

Designed for bobwhite quail population research in the southeastern United States.
