# QuailTracker

Open-source wildlife acoustic monitoring ecosystem — recording hardware, companion app, desktop analyzer, and ML training pipeline. See [docs/ecosystem.md](docs/ecosystem.md) for the full specification.

## Hardware

- **MCU:** STM32U575VGT6 (160 MHz Cortex-M33, 784 KB RAM, 1 MB flash, LQFP-100, non-SMPS LDO variant)
- **Microphone:** Infineon IM72D128 PDM MEMS on separate breakout board
- **GPS:** ATGM336H-5N31 (GPS+BDS, PPS output for sub-ms time sync)
- **BLE:** Ai-Thinker PB-03F (BLE 5.2, UART AT commands)
- **Storage:** MicroSD via SPI1 (PA4 CS, PA5/PA6/PA7 SCK/MISO/MOSI), PC4 card detect
- **Sensors:** SHT30 temperature/humidity on I2C1 (PB6/PB7)
- **Audio:** ADF1 PDM capture at 48 kHz, 16-bit FLAC or WAV output
- **Power:** 1S2P 18650 + CN3791 solar MPPT, three TPS22916 switched rails (GPS/BLE/PERIPH)
- **Detection:** TFLite Micro DS-CNN inference on 3s clips (24 kHz, 40-mel spectrogram)

## Directory Structure

```
QuailTracker/
├── stm32/QuailTracker_U575/   # STM32U575 firmware (CubeMX + PlatformIO)
├── app/                       # Companion mobile app (Avalonia 11.2 / .NET 10)
├── analyzer/                  # Desktop analyzer (Avalonia 11.2 / .NET 10)
├── training/                  # Model training pipeline (Docker/Flask/TF 2.15)
├── hardware/                  # Schematics, BOM, pinout docs
├── docs/                      # Documentation and ecosystem spec
├── tools/                     # Utility scripts
└── platformio.ini             # PlatformIO config (default env: stm32u575)
```

## Building Firmware

```bash
pio run                    # build
pio run --target upload    # flash via J-Link
```

## Development Notes

- CubeMX reference project at `/Users/chris/Code/Qt_U575VGT6` — generate there, copy init code into real project
- CubeMX wipes `Middlewares/` on code gen — restore FatFS from git after each generation
- `pio run -t clean` when changes don't appear (PlatformIO caches .o files)
- UART init MUST come before ADF1 init or Error_Handler silently hangs
- SPI1 recovery: RCC reset + GPIO toggle required after CSUSP corruption (see `SPI_Recover()` in `user_diskio.c`)
- Version tracked in `Core/Inc/main.h` FW_VERSION — increment on each change
