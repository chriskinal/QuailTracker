# QuailTracker

Low-cost GPS-synchronized Autonomous Recording Units (ARUs) for quail population monitoring using TDOA localization.

## Project Overview

This project builds a network of solar/battery-powered acoustic recording stations that capture quail vocalizations with GPS-synchronized timestamps. Multiple stations hearing the same call enable Time Difference of Arrival (TDOA) localization to pinpoint covey locations.

## Hardware Architecture

### Core Components
- **MCU:** ESP32 (dual-core) - chosen for dual I2S ports and proper MCLK output
- **Microphone:** PUI AOM-5024L-HD-R electret condenser (80 dB SNR)
- **ADC:** ES7243E 24-bit I2S ADC (I2C configurable, 3.3V operation)
- **GPS:** Quectel L76K with PPS output for time synchronization
- **Storage:** MicroSD card (32GB Class 10)
- **Power:** 1S4P 18650 Li-ion pack (13,600 mAh) → NCP170 LDO (500nA Iq) → 3.0V rail
- **Environment:** SHT30 temperature/humidity sensor (±0.2°C, ±2% RH)

### Audio Signal Chain
```
Sound → PUI Mic (80dB SNR) → Bias Circuit (3.3V/2.2kΩ) → 10µF cap → ES7243E → I2S → ESP32 → SD Card
```

### Key Pin Assignments
| ESP32 Pin | Function | Connection |
|-----------|----------|------------|
| GPIO0 | I2S Master Clock | ES7243E MCLK |
| GPIO14 | I2S Bit Clock | ES7243E SCLK |
| GPIO15 | I2S Word Select | ES7243E LRCK |
| GPIO32 | I2S Data In | ES7243E SDOUT |
| GPIO21 | I2C SDA | ES7243E, SHT30 |
| GPIO22 | I2C SCL | ES7243E, SHT30 |
| GPIO16 | UART RX | GPS TX |
| GPIO17 | UART TX | GPS RX |
| GPIO4 | PPS Input | GPS PPS |
| GPIO5 | SPI CS | SD Card CS |
| GPIO18 | SPI CLK | SD Card CLK |
| GPIO19 | SPI MISO | SD Card MISO |
| GPIO23 | SPI MOSI | SD Card MOSI |
| GPIO35 | ADC Input | Battery voltage divider |

### I2S Configuration
- Sample rate: 48 kHz
- Bit depth: 16-bit (record) / 24-bit (ADC native)
- MCLK: 12.288 MHz (256 × Fs)
- BCK: 3.072 MHz (64 × Fs)
- Format: I2S standard, MSB first

## Firmware Requirements

### Recording Schedule
- GPS-derived sunrise/sunset calculation (NOAA algorithm)
- Configurable pre-sunrise and post-sunset offsets
- Dawn chorus typically most active period for bobwhite calls

### File Format
- WAV files with embedded GPS timestamp in filename
- Format: `YYYYMMDD_HHMMSS_<station_id>.wav`
- PPS interrupt captures precise timestamp at recording start

### Power Management
- Deep sleep between recording windows (~10µA)
- NCP170 LDO with 500nA quiescent current
- GPS firmware standby via PMTK commands (no GPIO power gating needed)
- Battery voltage monitoring via 1M/1M divider (~2µA continuous)
- Target: 90+ days on 2× 18650 cells (dawn/dusk recording only)

### BLE Configuration Interface
- Station ID assignment
- Recording schedule adjustment
- Status monitoring (battery, storage, GPS lock)
- Firmware updates

## Detection Performance

The high-SNR microphone (80 dB) enables ~1000m detection range for bobwhite whistle calls, compared to ~300m with typical MEMS mics (61 dB). This allows TDOA coverage of 11/17 target coveys with just 10 stations.

## Target Specifications

| Parameter | Value |
|-----------|-------|
| Unit cost | ~$33-43 |
| Battery life | 90+ days |
| Detection range | 800-1200m |
| Time sync accuracy | ±1 ms |
| Sample rate | 48 kHz |
| Storage | 32 GB |

## Directory Structure

```
QuailTracker/
├── docs/                    # Documentation
│   └── design_document.md   # Full design specification
├── firmware/                # ESP32 firmware (PlatformIO)
│   ├── src/
│   ├── include/
│   └── platformio.ini
├── hardware/                # KiCad schematics/PCB (future)
├── analysis/                # Python scripts for audio analysis
└── CLAUDE.md               # This file
```

## Development Notes

- Use PlatformIO with Arduino framework for ESP32
- ESP-IDF I2S driver preferred over Arduino I2S library for MCLK control
- ES7243E configured via I2C (address 0x10)
- SHT30 on same I2C bus (address 0x44)
- GPS NMEA parsing: only need GGA and RMC sentences
- Consider SPIFFS for config storage, SD for audio only
