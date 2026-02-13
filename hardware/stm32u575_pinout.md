# STM32U575VGT6 Production Board — Pin Assignments

MCU: STM32U575VGT6, LQFP-100, SMPS variant
Datasheet: DS13737 Rev 10 (July 2024)
LCSC: C5270988

## Pin Assignment Summary

| Function | GPIO | LQFP100 Pin | AF | Peripheral | Notes |
|----------|------|-------------|-----|------------|-------|
| PDM Mic Clock | PE9 | 37 | AF3 | ADF1_CCK0 | Clock out to IM73D122 |
| PDM Mic Data | PE10 | 38 | AF3 | ADF1_SDI0 | Data in from IM73D122 |
| GPS UART TX | PA9 | 68 | AF7 | USART1_TX | To L76K RX |
| GPS UART RX | PA10 | 69 | AF7 | USART1_RX | From L76K TX |
| BLE UART TX | PA2 | 24 | AF7 | USART2_TX | To PB-03F RX |
| BLE UART RX | PA3 | 25 | AF7 | USART2_RX | From PB-03F TX |
| Debug UART TX | PD8 | 55 | AF7 | USART3_TX | Optional debug serial |
| Debug UART RX | PD9 | 56 | AF7 | USART3_RX | Optional debug serial |
| SD Card SCK | PA5 | 29 | AF5 | SPI1_SCK | SPI mode SD |
| SD Card MISO | PA6 | 30 | AF5 | SPI1_MISO | SPI mode SD |
| SD Card MOSI | PA7 | 31 | AF5 | SPI1_MOSI | SPI mode SD |
| SD Card CS | PA4 | 28 | GPIO | Output, active low | Software NSS |
| SHT30 SCL | PB6 | 92 | AF4 | I2C1_SCL | 4.7k pull-up R3 |
| SHT30 SDA | PB7 | 93 | AF4 | I2C1_SDA | 4.7k pull-up R4 |
| Battery ADC | PC0 | 15 | analog | ADC1_IN1 | 1M/1M divider R7/R8 |
| GPS PPS | PC2 | 17 | GPIO | EXTI input, rising | 1ms sync accuracy |
| GPS WAKEUP | PD14 | 61 | GPIO | Output | L76K WAKEUP pin |
| GPS RESET | PD15 | 62 | GPIO | Output, active low | L76K RESET pin |
| GPS Power EN | PD12 | 59 | GPIO | Output | HIGH=GPS on, LOW=GPS off |
| Status LED | PD13 | 60 | GPIO | Output | Via 1k R9 to LED1 |
| SWDIO | PA13 | 72 | AF0 | SWD debug | Default after reset |
| SWCLK | PA14 | 76 | AF0 | SWD debug | Default after reset |
| LSE Crystal | PC14 | 8 | - | OSC32_IN | 32.768kHz |
| LSE Crystal | PC15 | 9 | - | OSC32_OUT | 32.768kHz |
| BOOT0 | PH3 | 94 | - | BOOT0 | 10k pull-down R6 |

## Power Pins (LQFP100 SMPS variant)

| Pin | Function | Decoupling |
|-----|----------|------------|
| 6 | VBAT | 100nF to GND |
| 11 | VDD | 100nF to GND + share 10uF bulk |
| 19 | VSSA | Connect to GND plane |
| 20 | VREF+ | 1uF + 100nF to GND |
| 21 | VDDA | 1uF + 100nF to GND |
| 26 | VSS | GND plane |
| 27 | VDD | 100nF to GND |
| 45 | VLXSMPS | 2.2uH inductor to VDD11 (pin 48) |
| 46 | VDDSMPS | Tie to VDD (3.3V input to SMPS) |
| 47 | VSSSMPS | Connect to GND plane |
| 48 | VDD11 | 2x2.2uF to VSSSMPS (1.1V SMPS output) |
| 49 | VSS | GND plane |
| 50 | VDD | 100nF to GND |
| 51 | VDD | 100nF to GND |
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
| 14 | NRST | 100nF C18 to GND |

## SMPS Configuration

The STM32U575 internal SMPS requires external components (per AN5373 Figure 7):

```
VDD (3.3V) ──── VDDSMPS (pin 46)    [SMPS input = VDD rail]

                 L1 (2.2uH)
VLXSMPS (pin 45) ─────────── VDD11 (pin 48)   [SMPS switch node → output]
                                │
                          ┌─────┴─────┐
                          │2.2uF  2.2uF│
                          └─────┬─────┘
                                │
                          VSSSMPS (pin 47)
```

VDDSMPS is the SMPS input — tie directly to VDD (3.3V rail).
VLXSMPS is the switch node — connect inductor to VDD11.
VDD11 (pin 48) is the 1.1V SMPS output — decouple with 2x2.2uF to VSSSMPS.

## Peripheral Details

### ADF1 — PDM Microphone (IM73D122)

Selected PE9/PE10 over PB3/PB4 because:
- PB3 = JTDO after reset (requires JTAG release in firmware)
- PB4 = NJTRST after reset (same issue)
- PE9/PE10 are clean GPIOs, adjacent pins, no boot-time conflicts

ADF1 provides hardware decimation filtering for the PDM bitstream.
Supports LPBAM for autonomous audio capture in Stop 2 mode.

### USART1 — GPS (L76K breakout)

PA9/PA10 are the boot-default USART1 pins. 3.3V logic levels match L76K.
GPS module (U2, Seeed L76K on XIAO footprint):

| Header Pin | Signal | MCU Pin | Direction |
|------------|--------|---------|-----------|
| 1 | GND | - | - |
| 2 | VCC | 3.3V (switched) | Power |
| 3 | VBKP | 3.3V direct | Backup power |
| 4 | TX (GPS→MCU) | PA10 / USART1_RX | Input |
| 5 | RX (MCU→GPS) | PA9 / USART1_TX | Output |
| 6 | WAKEUP | PD14 | Output |
| 7 | PPS | PC2 | Input (EXTI) |
| 8 | RESET | PD15 | Output |

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

## Unused Pins on LQFP100 SMPS

Available GPIOs not assigned (active-low-power by configuring as analog input):

PE0-PE6, PE7, PE8, PE11-PE15, PA0, PA1, PA8, PA11, PA12, PA15,
PB0-PB5, PB8-PB10, PB13-PB15, PC1, PC3-PC9, PC10-PC12,
PD0-PD7, PD10, PD11, PH0, PH1

Note: PB11/PB12 are NOT available on LQFP100 SMPS variant
(pins occupied by VLXSMPS/VDDSMPS infrastructure).

## EasyEDA Schematic Designators

1. **Power** — Q1 (LDO), L1 (SMPS inductor), C1-C18 (decoupling), CN1 (battery)
2. **MCU** — U1 (STM32U575VGT6), X1 (LSE crystal), R6 (BOOT0 pull-down)
3. **Audio** — CN2 (JST SH 4-pin to mic breakout: CLK, DATA, VDD, GND)
4. **GPS** — U2 (L76K module), Q2 (P-FET switch), Q3/Q4 (level shift), R1/R2/R5 (10k)
5. **Storage** — CARD1 (MicroSD slot), SPI1 on PA4-PA7
6. **BLE** — COMM1 (PB-03F module), USART2 on PA2/PA3
7. **Sensor** — H1 (4-pin header to off-board SHT30), R3/R4 (I2C pull-ups)
8. **Debug** — H2 (SWD 4-pin header: SWDIO, SWCLK, 3.3V, GND)
9. **Battery** — R7/R8 (1M divider to PC0), CN1 (JST 2-pin)
10. **Status** — LED1 (green 0805), R9 (1k current limit)

## PCB Layout Notes

- 2-layer board, 1.6mm FR4, JLCPCB economic assembly
- Ground pour on bottom layer
- SMPS inductor (L1) between pins 45 and 48, short traces (<5mm)
- SMPS output caps (2x2.2uF) adjacent to pin 48/47
- ADF PDM traces (PE9/PE10) kept short — mic JST connector near MCU
- BLE module (PB-03F) antenna area: no copper pour within 5mm of antenna
- SD card slot at board edge
- GPS header at board edge
- All decoupling caps placed as close as possible to their respective VDD pins
