# QuailARU EasyEDA Build Guide

**Building the schematic using LCSC parts for JLCPCB assembly**

## Overview

This guide walks through creating the QuailARU schematic in EasyEDA using LCSC library components. Using LCSC parts directly ensures:
- Footprints are already assigned
- Part numbers are linked for BOM export
- Seamless JLCPCB assembly ordering

## LCSC Parts List

Search for these parts in EasyEDA's component library (use LCSC part numbers):

| Ref | LCSC # | Component | Description |
|-----|--------|-----------|-------------|
| U1 | C2929446 | ES7243E | 24-bit I2S ADC, QFN-20 |
| U2 | C603670 | NCP170ASN300T2G | 3.0V 150mA Ultra-Low Iq LDO, TSOP-5 |
| U3 | C2838031 | L76K | Quectel GPS Module, LCC-18 |
| J1 | C113206 | TF-015 | MicroSD Card Socket |
| J3 | C158012 | B2B-XH-A(LF)(SN) | Mic Connector JST XH 2-pin |
| J2 | C295747 | S2B-PH-SM4-TB | JST PH 2-pin Battery Connector |
| R1 | C4190 | 0603WAF2201T5E | 2.2kΩ 0603 Resistor (Mic Bias) |
| C1-C6 | C14663 | CC0603KRX7R9BB104 | 100nF 0603 Capacitor |
| C7-C10 | C89827 | CC0805KKX5R7BB106 | 10µF 0805 Capacitor |
| C11-C12 | C123624 | 0805B475K160CT | 4.7µF 0805 Capacitor |

**Note:** U4 (ESP32S NodeMCU 38-pin module) is hand-soldered (not SMT assembled)

## Step-by-Step Build

### 1. Create New Project

1. Open EasyEDA (https://easyeda.com)
2. File → New → Project
3. Name: "QuailARU"
4. Create new schematic

### 2. Add Components

For each component, use the Library panel:
1. Click "Library" icon (or press `L`)
2. Search by LCSC part number (e.g., "C2929446")
3. Select part with "LCSC" badge
4. Place on schematic

### 3. Power Subsystem

```
Battery (J2: C295747) → 10µF ceramic → NCP170 (U2: C603670) → 3.0V rail
                                              ↓
                                        Decoupling caps:
                                        - 10µF (C89827) input
                                        - 10µF (C89827) + 100nF (C14663) output
```

**NCP170 Connections (TSOP-5):**
| Pin | Connection |
|-----|------------|
| 1 (VIN) | Battery + |
| 2 (GND) | Ground |
| 3 (EN) | Tie to VIN (always enabled) |
| 4 (NC) | No connection |
| 5 (VOUT) | 3.0V Rail |

**Note:** NCP170 has ultra-low 500nA quiescent current for extended battery life.

### 4. Audio Subsystem

```
3.3V → R1 (2.2kΩ) → Mic+ (MK1: C3273706)
                     ↓
                   1µF DC block cap (C11)
                     ↓
                   ES7243E AINLP (U1: C2929446)
```

**ES7243E Connections:**
| Pin | ESP32 GPIO | Function |
|-----|------------|----------|
| MCLK | GPIO0 | Master Clock (12.288 MHz) |
| SCLK | GPIO14 | Bit Clock |
| LRCK | GPIO15 | Word Select |
| SDOUT | GPIO32 | Audio Data |
| SDA | GPIO21 | I2C Data |
| SCL | GPIO22 | I2C Clock |
| AD0 | GND | I2C Address = 0x10 |
| DVDD | 3.3V | Digital Power |
| AVDD | 3.3V | Analog Power |
| GND | GND | Ground |
| AINL | Mic output | Audio Input |
| AINR | GND | Unused (mono) |

**Decoupling:** 100nF on DVDD, 10µF + 100nF on AVDD

### 5. GPS Subsystem

```
3.3V → L76K VCC (U3: C2838031)
       + 100nF decoupling cap
```

**L76K Connections:**
| Pin | ESP32 GPIO | Function |
|-----|------------|----------|
| VCC | 3.3V direct | Always powered |
| GND | GND | Ground |
| TX | GPIO16 | UART RX |
| RX | GPIO17 | UART TX |
| PPS | GPIO4 | Time sync pulse |

**Power Management via PMTK Commands:**

Instead of hardware power gating, the L76K supports firmware-controlled low power modes:

| Mode | Command | Wake Method | Current |
|------|---------|-------------|---------|
| Standby | `$PMTK161,0*28` | Send any byte | ~1mA |
| Periodic | `$PMTK225,2,...` | Automatic | Varies |
| Full Power | `$PMTK225,0*2B` | N/A | ~25mA |

**Standby Mode:**
```
$PMTK161,0*28<CR><LF>
```
Module enters standby until any byte is received on UART.

**Periodic Mode (example: 3s on, 12s sleep):**
```
$PMTK225,2,3000,12000,18000,72000*XX
```
- Type 2 = Periodic standby
- Automatically wakes to update position, then sleeps

### 6. SD Card Subsystem

**TF-015 (J1: C113206) Connections:**
| Pin | ESP32 GPIO | Function |
|-----|------------|----------|
| VCC | 3.3V | Power |
| GND | GND | Ground |
| CS | GPIO5 | Chip Select |
| SCK | GPIO18 | SPI Clock |
| MOSI | GPIO23 | Data In |
| MISO | GPIO19 | Data Out |

### 7. U3 - ESP32 Module Symbol

Since U3 (ESP32S NodeMCU 38-pin) is a through-hole module:

1. Search "NODEMCU-32SLUA" in EasyEDA library for the correct footprint
2. **Important:** Verify pin 1 is at top-left, pin 38 at top-right

⚠️ **Footprint Warning:** Some ESP32 DevKit footprints have the right side pins reversed. The correct footprint (NODEMCU-32SLUA) has:
- Left side: Pin 1 (top) to Pin 19 (bottom)
- Right side: Pin 38 (top) to Pin 20 (bottom)

**Key Pin Assignments:**
| GPIO | Function | Direction |
|------|----------|-----------|
| GPIO0 | I2S MCLK | Output |
| GPIO2 | (Available) | - |
| GPIO4 | PPS Input | Input |
| GPIO5 | SD CS | Output |
| GPIO14 | I2S SCLK | Output |
| GPIO15 | I2S LRCK | Output |
| GPIO16 | UART RX (GPS) | Input |
| GPIO17 | UART TX (GPS) | Output |
| GPIO18 | SPI CLK | Output |
| GPIO19 | SPI MISO | Input |
| GPIO21 | I2C SDA | Bidir |
| GPIO22 | I2C SCL | Output |
| GPIO23 | SPI MOSI | Output |
| GPIO32 | I2S Data | Input |
| 3V3 | Power | - |
| GND | Ground | - |

### 8. Add Net Labels

Use netlabels for cleaner routing:
- `3V3` - 3.3V power rail
- `GND` - Ground
- `VBAT` - Battery voltage
- `MCLK`, `SCLK`, `LRCK`, `SDOUT` - I2S signals
- `SDA`, `SCL` - I2C signals
- `GPS_TX`, `GPS_RX`, `PPS` - GPS signals
- `SD_CS`, `SD_CLK`, `SD_MOSI`, `SD_MISO` - SPI signals

### 9. Design Rule Check

1. Click "Design" → "Check DRC"
2. Fix any unconnected pins or errors
3. Ensure all nets are properly named

## PCB Layout

After completing schematic:

1. Click "Design" → "Convert to PCB"
2. Set board outline (suggested: 80mm × 60mm)
3. Place components with these considerations:
   - Microphone away from ESP32 (RF noise)
   - Short analog traces (mic to ADC)
   - GPS antenna area clear of copper
   - Bulk caps near power input

## BOM Export for JLCPCB

1. In schematic view: "Fabrication" → "BOM"
2. Verify all LCSC part numbers are assigned
3. Export CSV for JLCPCB assembly order
4. Generate CPL (Component Placement List) from PCB

## Files Generated

- `QuailARU.json` - EasyEDA schematic source (this project)
- `QuailARU_BOM.csv` - Bill of Materials for JLCPCB
- `QuailARU_CPL.csv` - Component placement for assembly
- `Gerber_QuailARU.zip` - PCB fabrication files

## Cost Estimate (JLCPCB Assembly)

| Item | Qty | Est. Cost |
|------|-----|-----------|
| PCB (5 pcs) | 1 | $2-5 |
| SMT Assembly Setup | 1 | $8 |
| ES7243E | 1 | $0.24 |
| NCP170 LDO | 1 | $0.19 |
| L76K GPS | 1 | $8.89 |
| TF-015 | 1 | $0.08 |
| Mic (C3273706) | 1 | $1.83 |
| Connector (C295747) | 1 | $0.10 |
| Passives | ~15 | $0.50 |
| **Total per board** | | **~$22-25** |

*ESP32 module ($3-4) and batteries ($12) sourced separately*
