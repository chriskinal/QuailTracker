# STM32U575VGT6 Production Board — Pin Assignments (V2)

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
| GPS UART TX | PA9 | 68 | AF7 | USART1_TX | To L76K RX |
| GPS UART RX | PA10 | 69 | AF7 | USART1_RX | From L76K TX |
| BLE UART TX | PA2 | 25 | AF7 | USART2_TX | To PB-03F RX |
| BLE UART RX | PA3 | 26 | AF7 | USART2_RX | From PB-03F TX |
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
| GPS PPS | PA8 | 67 | GPIO | EXTI input, rising | 1ms sync accuracy |
| GPS WAKEUP | PD14 | 61 | GPIO | Output | L76K WAKEUP pin |
| GPS RESET | PD15 | 62 | GPIO | Output, active low | L76K RESET pin |
| GPS Power EN | PD12 | 59 | GPIO | Output | HIGH=GPS on, LOW=GPS off |
| Status LED | PD13 | 60 | GPIO | Output | Via 1k R9 to LED1 |
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

### USART1 — GPS (L76K breakout)

PA9/PA10 are the boot-default USART1 pins. 3.3V logic levels match L76K.
GPS module (U2, Seeed L76K on XIAO footprint):

| XIAO Pin | Signal | MCU Pin | Direction | Notes |
|----------|--------|---------|-----------|-------|
| 1 | RXD (MCU→GPS) | PA9 / USART1_TX | Output | |
| 7 | WAKEUP | PD14 | Output | |
| 8 | 5V | NC | - | Not used |
| 9 | GND | - | - | |
| 10 | VCC (3v3) | GPS_VCC (switched) | Power | Via Q2 P-FET |
| 11 | RESET | PD15 | Output | Active low |
| 14 | TXD (GPS→MCU) | PA10 / USART1_RX | Input | |
| 3 | PPS | PA8 / EXTI | Input | Bodge wire on L76K: PPS pad → XIAO pin 3 |

**L76K bodge wires** (soldered on the GPS module, not the main board):
- **PPS**: L76K PPS pad → XIAO header pin 3
- **VBKP**: L76K VBKP pad → XIAO 3V3 pin (keeps RTC/hot-start alive when GPS_VCC off)

### USART2 — BLE Module (PB-03F)

PA2/PA3 are the boot-default USART2 pins. PB-03F uses AT commands over UART.
Default baud rate: 115200. 3.3V logic.

| PB-03F Pin | Signal | MCU Pin |
|------------|--------|---------|
| TX | BLE→MCU | PA3 / USART2_RX |
| RX | MCU→BLE | PA2 / USART2_TX |
| VCC | 3.3V | Power rail |
| GND | Ground | GND |

### SPI1 — SD Card

PA5/PA6/PA7 are the boot-default SPI1 pins. PA4 used as GPIO chip select.
SD card operates in SPI mode. Max SPI clock ~25MHz for SD.

### I2C1 — SHT30

PB6/PB7 are the boot-default I2C1 pins.
SHT30 address: 0x44 (ADDR pin low).
4.7k pull-ups on SDA and SCL to 3.3V.

### ADC1 — Battery Monitoring

PC0 (ADC1_IN1) reads battery voltage through a 1M/1M resistive divider.
Divider ratio: 0.5x → ADC reads 1.5V-2.1V for 3.0V-4.2V LiPo.
Divider current: ~2uA continuous (acceptable for battery life).

## Unused Pins on LQFP100 non-SMPS

Available GPIOs not assigned (configure as analog input for lowest leakage):

PE0-PE6, PE7, PE8, PE11-PE15, PA0, PA1, PA11, PA12, PA15,
PB0-PB2, PB4, PB5, PB8-PB10, PB12-PB15, PC1, PC2, PC3, PC5-PC9, PC10-PC12,
PD0-PD7, PD10, PD11, PH0, PH1

Note: PB11 is NOT bonded out on LQFP100 (neither SMPS nor non-SMPS variant).

## EasyEDA Schematic Designators

1. **Power** — Q1 (LDO), C1-C16 (decoupling), CN1 (battery)
2. **MCU** — U1 (STM32U575VGT6), X1 (LSE crystal), R6 (BOOT0 pull-down)
3. **Audio** — CN2 (JST PH 4-pin to mic breakout: CLK, DATA, VDD, GND)
4. **GPS** — U2 (L76K module), Q2 (P-FET switch), Q3 (gate drive), R1/R2 (10k)
5. **Storage** — CARD1 (MicroSD slot), SPI1 on PA4-PA7
6. **BLE** — COMM1 (PB-03F module), USART2 on PA2/PA3
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
- BLE module (PB-03F) antenna area: no copper pour within 5mm of antenna
- SD card slot at board edge
- GPS header at board edge
- All decoupling caps placed as close as possible to their respective VDD pins
