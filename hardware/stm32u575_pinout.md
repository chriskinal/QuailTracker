# STM32U575VGT6 Production Board — Pin Assignments (V5)

MCU: STM32U575VGT6, LQFP-100, non-SMPS (LDO) variant
Datasheet: DS13737 Rev 10 (July 2024), **Figure 15 — LQFP100 pinout**
LCSC: C5270988

Note: The SMPS variant (Q suffix, Figure 14) has a different pinout.
C5270988 is the non-SMPS variant — uses internal LDO, no external inductor.

**V2 changes:** Added SWO trace output (PB3), expanded debug header to 7-pin
(SWD + SWO + UART), added RESET and BOOT0 tactile buttons for DFU mode.

## Pin Assignment Summary

| Function | GPIO | LQFP100 Pin | AF | Peripheral | Notes |
|----------|------|-------------|-----|------------|-------|
| PDM Mic Clock | PE9 | 40 | AF3 | ADF1_CCK0 | Clock out to IM72D128 |
| PDM Mic Data | PE10 | 41 | AF3 | ADF1_SDI0 | Data in from IM72D128 |
| GPS UART TX | PA9 | 68 | AF7 | USART1_TX | To ATGM336H RXD (pin 3) |
| GPS UART RX | PA10 | 69 | AF7 | USART1_RX | From ATGM336H TXD (pin 2) |
| ESP32 SPI SCK | PB13 | 52 | AF5 | SPI2_SCK | To ESP32-C3 GPIO4 |
| ESP32 SPI MISO | PB14 | 53 | AF5 | SPI2_MISO | From ESP32-C3 GPIO5 |
| ESP32 SPI MOSI | PB15 | 54 | AF5 | SPI2_MOSI | To ESP32-C3 GPIO6 |
| ESP32 SPI CS | PB12 | 51 | GPIO | Output, active low | To ESP32-C3 GPIO7, also ROM bootloader NSS |
| Debug UART TX | PD8 | 55 | AF7 | USART3_TX | Debug serial on H2 header |
| Debug UART RX | PD9 | 56 | AF7 | USART3_RX | Debug serial on H2 header |
| SD Card SCK | PA5 | 30 | AF5 | SPI1_SCK | SPI mode SD |
| SD Card MISO | PA6 | 31 | AF5 | SPI1_MISO | SPI mode SD |
| SD Card MOSI | PA7 | 32 | AF5 | SPI1_MOSI | SPI mode SD |
| SD Card CS | PA4 | 29 | GPIO | Output, active low | Software NSS |
| SD Card CD | PC4 | 33 | GPIO | Input, low = inserted | Card detect switch |
| SHT30 SCL | PB6 | 92 | AF4 | I2C1_SCL | 4.7k pull-up R3 |
| SHT30 SDA | PB7 | 93 | AF4 | I2C1_SDA | 4.7k pull-up R4 |
| Battery ADC | PC0 | 15 | analog | ADC1_IN1 | 1M/1M divider R7/R8 |
| GPS PPS | PA8 | 67 | GPIO | EXTI input, rising | ATGM336H 1PPS (pin 4) |
| GPS ON/OFF | PD14 | 61 | GPIO | Output | ATGM336H ON/OFF (pin 5) |
| GPS RESET | PD15 | 62 | GPIO | Output, active low | ATGM336H nRESET (pin 9) |
| GPS_VCC EN | PD12 | 59 | GPIO | Output | HIGH=GPS on, LOW=off |
| ESP_VCC EN | PD10 | 57 | GPIO | Output | HIGH=ESP32 on, LOW=off |
| PERIPH_VCC EN | PD11 | 58 | GPIO | Output | HIGH=SD+SHT30 on, LOW=off |
| Status LED | PD13 | 60 | GPIO | Output | Via 1k R9 to LED1 |
| Solar CHRG | PB0 | 35 | GPIO | Input, active low | CN3791 charging indicator (open drain + 10k pull-up) |
| Solar DONE | PB1 | 36 | GPIO | Input, active low | CN3791 charge complete indicator (open drain + 10k pull-up) |
| SWO Trace | PB3 | 89 | AF0 | TRACESWO | Serial Wire Output for debug trace |
| SWDIO | PA13 | 72 | AF0 | SWD debug | Default after reset |
| SWCLK | PA14 | 76 | AF0 | SWD debug | Default after reset |
| LSE Crystal | PC14 | 8 | - | OSC32_IN | 32.768kHz |
| LSE Crystal | PC15 | 9 | - | OSC32_OUT | 32.768kHz |
| BOOT0 | PH3 | 94 | - | BOOT0 | 10k pull-down R6 |

## Power Pins (LQFP100 non-SMPS / LDO variant)

| Pin | Function | Decoupling |
|-----|----------|------------|
| 6 | VBAT | 100nF to GND |
| 10 | VSS | GND plane |
| 11 | VDD | 100nF to GND + share 10uF bulk |
| 19 | VSSA | Connect to GND plane |
| 20 | VREF- | Tie to VSSA (GND) |
| 21 | VREF+ | 1uF + 100nF to GND |
| 22 | VDDA | 1uF + 100nF to GND |
| 27 | VSS | GND plane |
| 28 | VDD | 100nF to GND |
| 48 | VCAP | 4.7uF to GND (internal LDO output bypass) |
| 49 | VSS | GND plane |
| 50 | VDD | 100nF to GND |
| 73 | VDDUSB | 100nF to GND (tie to VDD if USB unused) |
| 74 | VSS | GND plane |
| 75 | VDD | 100nF to GND |
| 99 | VSS | GND plane |
| 100 | VDD | 100nF to GND |

## Clock Pins

| Pin | Function | Notes |
|-----|----------|-------|
| 8 | PC14 / OSC32_IN | 32.768kHz LSE crystal |
| 9 | PC15 / OSC32_OUT | 32.768kHz LSE crystal |
| 12 | PH0 / OSC_IN | HSE — NC for V1 (using HSI16 + PLL) |
| 13 | PH1 / OSC_OUT | HSE — NC for V1 |
| 14 | NRST | 100nF C10 to GND |

## Internal LDO Configuration

The non-SMPS variant uses the internal LDO regulator for the 1.1V core supply.
No external inductor or SMPS components needed — simpler than SMPS variant.

VCAP (pin 48) is the internal LDO output bypass — connect 4.7uF ceramic cap to GND.
VREF- (pin 20) ties directly to VSSA (GND plane).

Power consumption: ~40 µA/MHz in Run mode (vs ~19 µA/MHz with SMPS).
Low-power modes (Stop 2, Standby, Shutdown) are similar between LDO and SMPS.

## Peripheral Details

### ADF1 — PDM Microphone (IM72D128)

Selected PE9/PE10 over PB3/PB4 because:
- PB3 = JTDO/TRACESWO after reset — now used for SWO debug trace output
- PB4 = NJTRST after reset (same issue)
- PE9/PE10 are clean GPIOs, adjacent pins, no boot-time conflicts

ADF1 provides hardware decimation filtering for the PDM bitstream.
Supports LPBAM for autonomous audio capture in Stop 2 mode.

### USART1 — GPS (ATGM336H-5N31)

PA9/PA10 are the boot-default USART1 pins. 3.3V logic levels match ATGM336H.
GPS module (U2, ATGM336H-5N31 LCC-18 SMD, 10.1x9.7mm):

| U2 Pin | Name | I/O | MCU Pin / Connection | Notes |
|--------|------|-----|---------------------|-------|
| 1 | GND | - | GND | |
| 2 | TXD | O | PA10 / USART1_RX | NMEA output (9600 default) |
| 3 | RXD | I | PA9 / USART1_TX | Command input |
| 4 | 1PPS | O | PA8 / EXTI | UTC-aligned pulse-per-second |
| 5 | ON/OFF | I | PD14 (GPS_WAKE) | Shutdown control (low=off) |
| 6 | VBAT | I | 3V3 (always-on) | RTC/SRAM backup, NOT switched GPS_VCC |
| 7 | NC | - | NC | |
| 8 | VCC | I | GPS_VCC (switched) | Main power 2.7-3.6V via Q2 P-FET |
| 9 | nRESET | I | PD15 (GPS_RST) | Active low, internal pull-up |
| 10 | GND | - | GND | |
| 11 | RF_IN | I | L1 / J1 (antenna) | Via bias tee from U.FL connector |
| 12 | GND | - | GND | |
| 13 | NC | - | NC | |
| 14 | VCC_RF | O | L1 (bias tee) | 3.3V antenna LNA power output |
| 15 | Reserved | - | NC | Float |
| 16 | SDA | I/O | NC | I2C unused |
| 17 | SCL | O | NC | I2C unused |
| 18 | Reserved | - | NC | Float |

**RF bias tee** (active antenna power injection, per ATGM336H datasheet §2.7.1):
- L1 (47nH): connects VCC_RF (pin 14) to RF_IN (pin 11) — injects DC to power antenna LNA
- J1 (U.FL): connects to RF_IN side of L1 — antenna coax attaches here
- C17 (10uF): GPS_VCC decoupling on GPS pin 8

### SPI2 — ESP32-C3 Super Mini Bridge

PB12-PB15 used for SPI2 — these pins match the STM32U575 ROM bootloader
SPI2 pinout (AN2606 Section 95), enabling OTA firmware recovery of a bricked STM32.

STM32 is SPI master during normal operation, ESP32 is SPI slave.
During OTA flash: ESP32 asserts BOOT0+NRST to enter ROM bootloader,
then becomes SPI master to flash the STM32 using AN4286 protocol.

| ESP32-C3 Pin | Signal | MCU Pin | Notes |
|-------------|--------|---------|-------|
| GPIO4 | SPI2_SCK | PB13 (pin 52) | 2 MHz clock |
| GPIO5 | SPI2_MISO | PB14 (pin 53) | STM32→ESP32 data |
| GPIO6 | SPI2_MOSI | PB15 (pin 54) | ESP32→STM32 data |
| GPIO7 | SPI2_NSS | PB12 (pin 51) | Chip select, active low |
| GPIO2 | STM32_NRST | NRST (pin 14) | Reset for OTA flash (normally hi-Z) |
| GPIO3 | STM32_BOOT0 | PH3 (pin 94) | Boot mode for OTA flash (normally hi-Z) |
| 3V3 | ESP_VCC | U5 output | Switched rail on PD10 |
| GND | GND | GND plane | |

**Antenna keep-out:** 2mm deep × module width, no copper either layer under ceramic antenna.

### SPI1 — SD Card

PA5/PA6/PA7 are the boot-default SPI1 pins. PA4 used as GPIO chip select.
SD card operates in SPI mode. Max SPI clock ~25MHz for SD.
CARD1 VDD on PERIPH_VCC (switched rail) — power cut during Stop 2 sleep.

### I2C1 — SHT30

PB6/PB7 are the boot-default I2C1 pins.
SHT30 address: 0x44 (ADDR pin low).
4.7k pull-ups on SDA and SCL to 3.3V.
SHT30 VDD (H1.1) on PERIPH_VCC (switched rail) — power cut during Stop 2 sleep.

### ADC1 — Battery Monitoring

PC0 (ADC1_IN1) reads battery voltage through a 1M/1M resistive divider.
Divider ratio: 0.5x → ADC reads 1.5V-2.1V for 3.0V-4.2V LiPo.
Divider current: ~2uA continuous (acceptable for battery life).

## Unused Pins on LQFP100 non-SMPS

Available GPIOs not assigned (configure as analog input for lowest leakage):

PE0-PE6, PE7, PE8, PE11-PE15, PA0, PA1, PA2, PA3, PA11, PA12, PA15,
PB2, PB4, PB5, PB8-PB10, PC1, PC2, PC3, PC5-PC9, PC10-PC12,
PD0-PD9, PH0, PH1

Note: PB11 is NOT bonded out on LQFP100 (neither SMPS nor non-SMPS variant).

## EasyEDA Schematic Designators

1. **Power** — Q1 (LDO), Q6 (reverse protection), C1-C16 (decoupling), CN1 (battery)
1a. **Solar Charger** — U7 (CN3791), D1/D2 (Schottky), L2 (22µH), C20/C21, R13-R17, CN3 (solar panel)
2. **MCU** — U1 (STM32U575VGT6), X1 (LSE crystal), R6 (BOOT0 pull-down)
3. **Audio** — CN2 (JST PH 4-pin to mic breakout: CLK, DATA, VDD, GND)
4. **GPS** — U2 (ATGM336H-5N31), U4 (TPS22916 load switch for GPS_VCC), L1 (47nH bias tee), J1 (U.FL antenna), C17 (10uF GPS_VCC decoupling)
4a. **ESP32 Power** — U5 (TPS22916 load switch for ESP_VCC), C19 (1uF output cap)
4b. **Peripheral Power** — U6 (TPS22916 load switch for PERIPH_VCC), C18 (1uF output cap)

5. **Storage** — CARD1 (MicroSD slot), SPI1 on PA4-PA7
6. **ESP32 Bridge** — ESPMOD1 (ESP32-C3 Super Mini), SPI2 on PB12-PB15, NRST+BOOT0 for OTA
7. **Sensor** — H1 (4-pin header to off-board SHT30), R3/R4 (I2C pull-ups)
8. **Debug** — H2 (7-pin header: 3V3, GND, SWDIO, SWCLK, SWO, DBG_TX, DBG_RX)
9. **Battery** — R7/R8 (1M divider to PC0), CN1 (JST 2-pin)
10. **Status** — LED1 (green 0805), R9 (1k current limit)
11. **Buttons** — SW1 (RESET tactile), SW2 (BOOT0 tactile)

## PCB Layout Notes

- 2-layer board, 1.6mm FR4, JLCPCB economic assembly
- Ground pour on bottom layer
- VCAP cap (4.7uF) close to pin 48, short trace to VSS (pin 49)
- ADF PDM traces (PE9/PE10) kept short — mic JST connector near MCU
- ESP32-C3 Super Mini antenna keep-out: 2mm deep × module width, no copper either layer
- SD card slot at board edge
- GPS module (U2) near board edge, U.FL connector (J1) at board edge for antenna cable
- GPS RF traces (RF_IN, bias tee) kept short — L1 and J1 close to U2
- All decoupling caps placed as close as possible to their respective VDD pins
