# STM32U575 Production Board — Netlist / Wiring Guide

All components placed in EasyEDA. Wire using **net port labels** — no long wires across the sheet.
Each named net lists every component pin that should carry that net port label.

MCU is non-SMPS variant (C5270988) — use **Figure 15** (LQFP100 pinout), NOT Figure 14 (LQFP100_SMPS).
Cross-reference: `stm32u575_pinout.md` (pin assignments), `stm32u575_bom_lcsc.csv` (BOM)

---

## How to Use This Document

1. Work one section at a time (Power → MCU decoupling → GPS → SD → etc.)
2. For each net, place a net port label on every listed pin
3. Local-only nets use short direct wires instead of net port labels
4. After wiring, run EasyEDA DRC to catch unconnected pins

---

## Power Nets

| Net Port | Description | Connected Pins |
|----------|-------------|----------------|
| **VBAT+** | Battery positive rail | CN1.1, Q1.VIN(1), Q1.CE(3), C16.+, R7.1 |
| **3V3** | 3.3V regulated rail | Q1.VOUT(5), C15.+, C11.+, U1 VDD pins (11, 28, 50, 75, 100), U1.VDDA(22), U1.VREF+(21), U1.VDDUSB(73), U1.VBAT(6), C1.+, C2.+, C3.+, C4.+, C5.+, C6.+, C7.+, C8.+, C9.+, C13.+, C14.+, R1.1, R3.1, R4.1, Q2.Source, COMM1.VCC, CARD1.VDD, CN2.3, H1.1, H2.1, J2.1 — bodge wire from U2 VBKP pad to J2 |
| **GND** | Ground | *(see dedicated GND table below)* |
| **GPS_VCC** | Switched 3.3V to GPS module | Q2.Drain, U2.VCC |
| **VBAT_Sense** | Battery ADC midpoint | R7.2, R8.1, U1.pin15 (PC0/ADC1_IN1) |
| **Q2_GATE** | GPS P-FET gate drive | Q2.Gate, R1.2, Q3.Collector |

### GND Net — All Ground Connections

Place the **GND** net port label on every pin listed below.

| Group | Pins |
|-------|------|
| MCU VSS | U1.pin10, U1.pin19 (VSSA), U1.pin20 (VREF-), U1.pin27, U1.pin49, U1.pin74, U1.pin99 |
| LDO | Q1.VSS(2) |
| Decoupling caps (−) | C1.−, C2.−, C3.−, C4.−, C5.−, C6.−, C7.−, C8.−, C9.−, C10.−, C11.−, C12.−, C13.−, C14.−, C15.−, C16.− |
| Connectors | CN1.2, CN2.4, CARD1.VSS, COMM1.GND, H1.2, H2.2 |
| GPS module | U2.GND |
| Transistors | Q3.Emitter |
| Divider low side | R8.2 |
| BOOT0 pull-down | R6.2 |
| LED cathode | LED1.Cathode |

> **Note:** VREF- (pin 20) ties to VSSA/GND — include it in the GND net.

---

## Signal Nets — GPS Block

| Net Port | Description | Connected Pins |
|----------|-------------|----------------|
| **GPS_TX** | GPS UART data out (GPS→MCU) | U2.TX, U1.pin69 (PA10 / USART1_RX, AF7) |
| **GPS_RX** | GPS UART data in (MCU→GPS) | U2.RX, U1.pin68 (PA9 / USART1_TX, AF7) |
| **GPS_PPS** | Pulse-per-second sync | J1.1, U1.pin17 (PC2 / EXTI) — bodge wire from U2 PPS pad to J1 |
| **GPS_WAKE** | GPS wakeup control | U2.WAKEUP, U1.pin61 (PD14) |
| **GPS_RST** | GPS reset (active low) | U2.RESET, U1.pin62 (PD15) |
| **GPS_EN** | GPS power enable (MCU→Q3) | R2.1, U1.pin59 (PD12, GPIO) |

### GPS Power Switch — How It Works

Q2 = SI2301CDS P-FET, Q3 = MMBT3904 NPN. All pin connections are in the net tables above (Q2_GATE, GPS_VCC, GPS_EN) and local-only wires below (R2→Q3, Q3.Emitter→GND).

- **GPS_EN HIGH** → Q3 ON → Q2 gate pulled LOW → P-FET ON → GPS powered
- **GPS_EN LOW** → Q3 OFF → R1 pulls gate to 3V3 → P-FET OFF → GPS off

---

## Signal Nets — BLE Block

| Net Port | Description | Connected Pins |
|----------|-------------|----------------|
| **BLE_TX** | BLE data out (BLE→MCU) | COMM1.TX, U1.pin26 (PA3 / USART2_RX, AF7) |
| **BLE_RX** | BLE data in (MCU→BLE) | COMM1.RX, U1.pin25 (PA2 / USART2_TX, AF7) |

COMM1 (PB-03F) VCC → 3V3, GND → GND (already in power nets above).

---

## Signal Nets — SD Card Block

| Net Port | Description | Connected Pins |
|----------|-------------|----------------|
| **SD_SCK** | SPI clock | CARD1.CLK, U1.pin30 (PA5 / SPI1_SCK, AF5) |
| **SD_MISO** | SPI data out (SD→MCU) | CARD1.DAT0, U1.pin31 (PA6 / SPI1_MISO, AF5) |
| **SD_MOSI** | SPI data in (MCU→SD) | CARD1.CMD, U1.pin32 (PA7 / SPI1_MOSI, AF5) |
| **SD_CS** | SPI chip select (active low) | CARD1.CD/DAT3, U1.pin29 (PA4, GPIO) |
| **SD_CD** | Card detect (low = inserted) | CARD1.CD_SW, U1.pin33 (PC4, GPIO input) |

CARD1 VDD → 3V3, VSS → GND (already in power nets above).

---

## Signal Nets — Audio (PDM Mic) Block

| Net Port | Description | Connected Pins |
|----------|-------------|----------------|
| **PDM_CLK** | PDM clock to mic breakout | CN2.1, U1.pin40 (PE9 / ADF1_CCK0, AF3) |
| **PDM_DATA** | PDM data from mic breakout | CN2.2, U1.pin41 (PE10 / ADF1_SDI0, AF3) |

CN2 (JST SH 4-pin): pin 1 = CLK, pin 2 = DATA, pin 3 = VDD (3V3), pin 4 = GND.

---

## Signal Nets — I2C (SHT30) Block

| Net Port | Description | Connected Pins |
|----------|-------------|----------------|
| **I2C_SCL** | I2C clock (4.7k pull-up) | H1.3, R3.2, U1.pin92 (PB6 / I2C1_SCL, AF4) |
| **I2C_SDA** | I2C data (4.7k pull-up) | H1.4, R4.2, U1.pin93 (PB7 / I2C1_SDA, AF4) |

R3.1 → 3V3, R4.1 → 3V3 (pull-ups, already in power nets above).
H1 (1x4 header): pin 1 = 3V3, pin 2 = GND, pin 3 = SCL, pin 4 = SDA.

---

## Signal Nets — Debug (SWD) Block

| Net Port | Description | Connected Pins |
|----------|-------------|----------------|
| **SWDIO** | SWD data | H2.3, U1.pin72 (PA13, AF0) |
| **SWCLK** | SWD clock | H2.4, U1.pin76 (PA14, AF0) |

H2 (1x4 header): pin 1 = 3V3, pin 2 = GND, pin 3 = SWDIO, pin 4 = SWCLK.

---

## Signal Nets — Status LED & Crystal

| Net Port | Description | Connected Pins |
|----------|-------------|----------------|
| **LED_OUT** | LED drive (active high) | R9.1, U1.pin60 (PD13, GPIO) |
| **OSC32_IN** | LSE crystal input | X1.1, U1.pin8 (PC14) |
| **OSC32_OUT** | LSE crystal output | X1.2, U1.pin9 (PC15) |

---

## Local-Only Nets (Short Direct Wires)

These connections are short enough to wire directly — no net port label needed.

| From | To | Notes |
|------|----|-------|
| R9.2 | LED1.Anode | LED current limit, short wire |
| LED1.Cathode | GND | Via GND net port on cathode |
| R2.2 | Q3.Base | GPS enable base resistor |
| Q3.Emitter | GND | Via GND net port |
| R6.1 | U1.pin94 (PH3/BOOT0) | BOOT0 pull-down |
| R6.2 | GND | Via GND net port |
| C10.1 | U1.pin14 (NRST) | Reset filter cap |
| C10.2 | GND | Via GND net port |

---

## MCU Pin Quick-Reference (Assigned Pins Only)

Pin numbers from DS13737 Rev 10, **Figure 15 — LQFP100 pinout** (non-SMPS).
Only pins with net port connections are listed — all others are unused (configure as analog input in firmware). Full unused list in `stm32u575_pinout.md`.

### Power & Special Pins

| Pin | Function | Net Port |
|-----|----------|----------|
| 6 | VBAT | 3V3 |
| 8 | PC14 / OSC32_IN | OSC32_IN |
| 9 | PC15 / OSC32_OUT | OSC32_OUT |
| 10 | VSS | GND |
| 11 | VDD | 3V3 (C1) |
| 14 | NRST | local: C10→GND |
| 19 | VSSA | GND |
| 20 | VREF- | GND (tie to VSSA) |
| 21 | VREF+ | 3V3 (C7+C13) |
| 22 | VDDA | 3V3 (C6+C14) |
| 27 | VSS | GND |
| 28 | VDD | 3V3 (C2) |
| 48 | VCAP | local: C12→GND |
| 49 | VSS | GND |
| 50 | VDD | 3V3 (C3) |
| 73 | VDDUSB | 3V3 (C8) |
| 74 | VSS | GND |
| 75 | VDD | 3V3 (C4) |
| 94 | PH3 / BOOT0 | local: R6→GND |
| 99 | VSS | GND |
| 100 | VDD | 3V3 (C5) |

### GPIO Pins

| Pin | GPIO | Net Port | Peripheral |
|-----|------|----------|------------|
| 15 | PC0 | VBAT_Sense | ADC1_IN1 |
| 17 | PC2 | GPS_PPS | EXTI |
| 25 | PA2 | BLE_RX | USART2_TX (AF7) |
| 26 | PA3 | BLE_TX | USART2_RX (AF7) |
| 29 | PA4 | SD_CS | GPIO output |
| 30 | PA5 | SD_SCK | SPI1_SCK (AF5) |
| 31 | PA6 | SD_MISO | SPI1_MISO (AF5) |
| 32 | PA7 | SD_MOSI | SPI1_MOSI (AF5) |
| 33 | PC4 | SD_CD | GPIO input (card detect) |
| 40 | PE9 | PDM_CLK | ADF1_CCK0 (AF3) |
| 41 | PE10 | PDM_DATA | ADF1_SDI0 (AF3) |
| 55 | PD8 | — | USART3_TX (optional debug) |
| 56 | PD9 | — | USART3_RX (optional debug) |
| 59 | PD12 | GPS_EN | GPIO output |
| 60 | PD13 | LED_OUT | GPIO output |
| 61 | PD14 | GPS_WAKE | GPIO output |
| 62 | PD15 | GPS_RST | GPIO output |
| 68 | PA9 | GPS_RX | USART1_TX (AF7) |
| 69 | PA10 | GPS_TX | USART1_RX (AF7) |
| 72 | PA13 | SWDIO | SWD (AF0) |
| 76 | PA14 | SWCLK | SWD (AF0) |
| 92 | PB6 | I2C_SCL | I2C1_SCL (AF4) |
| 93 | PB7 | I2C_SDA | I2C1_SDA (AF4) |

---

## Decoupling Capacitor Assignment

No SMPS components — internal LDO only. Removed L1, old C12/C13 (SMPS caps).
Added VCAP cap (C12) for internal LDO bypass.

| Cap | Value | MCU Pin / Function | Net (+) | Net (−) |
|-----|-------|--------------------|---------|---------|
| C1 | 100nF | VDD pin 11 | 3V3 | GND |
| C2 | 100nF | VDD pin 28 | 3V3 | GND |
| C3 | 100nF | VDD pin 50 | 3V3 | GND |
| C4 | 100nF | VDD pin 75 | 3V3 | GND |
| C5 | 100nF | VDD pin 100 | 3V3 | GND |
| C6 | 100nF | VDDA pin 22 | 3V3 | GND |
| C7 | 100nF | VREF+ pin 21 | 3V3 | GND |
| C8 | 100nF | VDDUSB pin 73 | 3V3 | GND |
| C9 | 100nF | VBAT pin 6 | 3V3 | GND |
| C10 | 100nF | NRST pin 14 | *(local)* | GND |
| C11 | 10uF | VDD bulk (near pin 11) | 3V3 | GND |
| C12 | 4.7uF | VCAP pin 48 | *(local)* | GND |
| C13 | 1uF | VREF+ pin 21 | 3V3 | GND |
| C14 | 1uF | VDDA pin 22 | 3V3 | GND |
| C15 | 1uF | LDO output (Q1.VOUT) | 3V3 | GND |
| C16 | 1uF | LDO input (Q1.VIN) | VBAT+ | GND |

**Totals:** 10x 100nF, 1x 10uF, 1x 4.7uF, 4x 1uF = 16 caps.

---

## Verification Checklist

- [ ] Every U1 pin from `stm32u575_pinout.md` appears in a net or marked unused
- [ ] No MCU pin used in two different nets
- [ ] All 16 decoupling caps connected (10x 100nF + 1x 10uF + 1x 4.7uF + 4x 1uF)
- [ ] VCAP (pin 48): 4.7uF cap to GND (pin 49)
- [ ] VREF- (pin 20): tied to VSSA/GND
- [ ] All component VCC pins on 3V3, all GND pins on GND
- [ ] GPS: Q2 P-FET Source=3V3, Drain=GPS_VCC, Gate=Q2_GATE
- [ ] GPS: U2.VCC on GPS_VCC (switched), U2.VBKP on 3V3 (always on)
- [ ] I2C: R3/R4 pull-ups between 3V3 and I2C_SCL/I2C_SDA
- [ ] Battery divider: R7 (VBAT+→midpoint), R8 (midpoint→GND), midpoint=VBAT_Sense
- [ ] BOOT0 (PH3 pin 94): R6 pull-down to GND
- [ ] NRST (pin 14): C10 to GND
- [ ] LSE crystal: X1 between OSC32_IN and OSC32_OUT (no external load caps — using internal)
- [ ] Pin numbers match DS13737 Figure 15 (LQFP100 non-SMPS), NOT Figure 14
- [ ] EasyEDA DRC: 0 errors, 0 unconnected pins
