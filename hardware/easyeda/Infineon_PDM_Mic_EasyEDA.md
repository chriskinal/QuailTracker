# Infineon PDM MEMS Microphone - EasyEDA Component Guide

**IM72D128V01 and IM73D122V01 Digital PDM Microphones**

## Overview

These Infineon XENSIV MEMS microphones output digital PDM (Pulse Density Modulation) directly, eliminating the need for an external ADC like the ES7243E. They connect directly to the ESP32's I2S peripheral in PDM mode.

| Part | SNR | Sensitivity | AOP | Best For |
|------|-----|-------------|-----|----------|
| IM72D128V01 | 72 dB(A) | -36 dBFS | 128 dBSPL | High SPL environments |
| IM73D122V01 | 73 dB(A) | -26 dBFS | 122 dBSPL | Far-field pickup (preferred) |

Both have:
- IP57 dust/water resistance (no Gore membrane needed)
- Digital PDM output
- Same package and pinout
- 1.62V to 3.6V operation

## Package Information

**Package:** PG-LLGA-5-3 (Bottom port)
**Size:** 4.0mm × 3.0mm × 1.2mm

```
        Top View (component side)
    ┌─────────────────────────────┐
    │                             │
    │      ○ Sound Port           │
    │      (0.6mm hole)           │
    │                             │
    │   Pin 1 marking             │
    │   ●                         │
    └─────────────────────────────┘

        Bottom View (PCB side)
    ┌─────────────────────────────┐
    │  ┌───┐           ┌───┐     │
    │  │ 1 │           │ 3 │     │
    │  │DAT│           │CLK│     │
    │  └───┘   ┌───┐   └───┘     │
    │          │ 5 │             │
    │   ┌───┐  │GND│  ┌───┐      │
    │   │ 2 │  └───┘  │ 4 │      │
    │   │VDD│         │SEL│      │
    │   └───┘         └───┘      │
    │                             │
    │      ○ Sound Port          │
    │      (0.6mm hole)          │
    └─────────────────────────────┘
```

## Pin Configuration

| Pin | Name | Description | Connection |
|-----|------|-------------|------------|
| 1 | DATA | PDM data output | ESP32 GPIO32 |
| 2 | VDD | Power supply (1.62-3.6V) | 3V3 + 100nF cap |
| 3 | CLOCK | PDM clock input | ESP32 GPIO14 |
| 4 | SELECT | L/R channel select | GND (left) or VDD (right) |
| 5 | GND | Ground | GND |

## LCSC Part Numbers

**Note:** These parts may not be in LCSC stock. Check availability or create custom component.

| Part | Manufacturer PN | Mouser/DigiKey | Notes |
|------|-----------------|----------------|-------|
| IM72D128V01 | SP005738480 | Available | Higher AOP |
| IM73D122V01 | SP005675385 | Check stock | Better sensitivity |

## Creating Custom EasyEDA Component

Since these may not be in LCSC library, create a custom component:

### 1. Create Symbol

1. In EasyEDA: File → New → Symbol
2. Name: "IM72D128" or "IM73D122"
3. Add 5 pins:

| Pin # | Name | Electric Type | Position |
|-------|------|---------------|----------|
| 1 | DATA | Output | Left |
| 2 | VDD | Power | Top |
| 3 | CLK | Input | Left |
| 4 | SEL | Input | Left |
| 5 | GND | Power | Bottom |

4. Draw rectangle body, add pin 1 indicator dot
5. Set prefix: "MIC"

### 2. Create Footprint

1. File → New → Footprint
2. Name: "LLGA-5-3_4x3mm_PDM"
3. Add pads per datasheet (page 12):

**Pad Coordinates (origin at center):**

| Pad | X (mm) | Y (mm) | Width | Height | Shape |
|-----|--------|--------|-------|--------|-------|
| 1 (DATA) | -0.85 | +0.8 | 0.65 | 0.4 | Rect |
| 2 (VDD) | -0.85 | -0.8 | 0.65 | 0.4 | Rect |
| 3 (CLK) | +0.85 | +0.8 | 0.65 | 0.4 | Rect |
| 4 (SEL) | +0.85 | -0.8 | 0.65 | 0.4 | Rect |
| 5 (GND) | 0 | 0 | 0.7 | 0.7 | Rect |

**Sound Port Hole:**
- Center: (0, +0.68) relative to package center
- Diameter: 0.8mm (PCB hole, larger than mic's 0.6mm port)
- Type: NPTH (Non-Plated Through Hole)

**Silkscreen:**
- Package outline: 4.0mm × 3.0mm rectangle
- Pin 1 indicator: dot or line at corner

**Courtyard:**
- 4.4mm × 3.4mm (0.2mm clearance)

### 3. Link Symbol to Footprint

1. Open symbol editor
2. Right-click → Modify Symbol
3. Add footprint link: "LLGA-5-3_4x3mm_PDM"

## Schematic Connections

### Minimal Circuit

```
                    ┌──────────────┐
3V3 ────┬──────────┤ VDD (pin 2)  │
        │          │              │
       ═╧═         │   IM72D128   │
       100nF       │      or      │
        │          │   IM73D122   │
GND ────┴──────────┤ GND (pin 5)  │
                   │              │
ESP32 GPIO14 ──────┤ CLK (pin 3)  │
                   │              │
ESP32 GPIO32 ◄─────┤ DATA (pin 1) │
                   │              │
GND ───────────────┤ SEL (pin 4)  │  (Left channel)
                   └──────────────┘
```

### With Optional Termination Resistor

For long traces or high clock rates, add 100Ω termination:

```
ESP32 GPIO32 ◄────[100Ω]────┤ DATA (pin 1) │
```

## BOM Changes vs ES7243E Design

**Removed (ES7243E design):**
| Ref | Part | LCSC # | Notes |
|-----|------|--------|-------|
| U1 | ES7243E | C2929446 | ADC no longer needed |
| J3 | B2B-XH-A | C158012 | Mic connector not needed |
| R1 | 2.0k 0603 | C22975 | Mic bias not needed |
| C11 | 1uF 0805 | C28323 | DC block not needed |
| C15 | 1uF 0805 | C28323 | AINLN AC-couple |
| C16 | 1uF 0805 | C28323 | AINRN AC-couple |
| C17 | 1uF 0805 | C28323 | AINRP AC-couple |
| C4 | 100nF 0603 | C14663 | ES7243E VDDD decoup |
| C5 | 100nF 0603 | C14663 | ES7243E VDDA decoup |
| C8 | 10uF 0805 | C15850 | ES7243E REFQ |
| C9 | 10uF 0805 | C15850 | ES7243E VDDA bulk |
| C12 | 10uF 0805 | C15850 | ES7243E REFP |
| R4 | 4.7k 0603 | C23162 | I2C SDA pull-up (keep if SHT30 used) |
| R5 | 4.7k 0603 | C23162 | I2C SCL pull-up (keep if SHT30 used) |

**Added (PDM mic design):**
| Ref | Part | LCSC # | Notes |
|-----|------|--------|-------|
| MIC1 | IM72D128 or IM73D122 | Custom | PDM digital mic |
| C20 | 100nF 0603 | C14663 | Mic VDD decoupling |
| R10 | 100Ω 0603 | C22775 | DATA termination (optional) |

**Net savings:** ~10 components removed, 2-3 added

## ESP32 Pin Changes

| Signal | ES7243E Design | PDM Mic Design |
|--------|----------------|----------------|
| GPIO0 | I2S MCLK | Not used |
| GPIO14 | I2S BCLK | PDM CLK |
| GPIO15 | I2S LRCK | Not used |
| GPIO32 | I2S DATA | PDM DATA |
| GPIO21 | I2C SDA | Not used (or SHT30 only) |
| GPIO22 | I2C SCL | Not used (or SHT30 only) |

## PCB Layout Guidelines

1. **Sound port hole:** 0.8mm diameter NPTH, aligned with mic port
2. **Decoupling cap:** Place 100nF as close to VDD pin as possible
3. **Ground plane:** Solid ground under mic, connected to pin 5
4. **Keep-out:** No traces under sound port area
5. **Orientation:** Pin 1 indicator must match footprint
6. **Solder paste:** Use stencil apertures per datasheet (page 12)

## Assembly Notes

- MSL1 (no special moisture handling required)
- Reflow: Standard lead-free profile, 260°C peak
- Do NOT vacuum pick from sound port hole
- Do NOT blow compressed air into sound port
- Protect sound port during conformal coating

## Testing

After assembly:
1. Apply power, verify ~1mA current draw at 3V
2. ESP32 firmware with USE_PDM_MIC=1
3. Check for audio response (clap test)
4. Verify no stuck-at-zero or stuck-at-one DATA output
