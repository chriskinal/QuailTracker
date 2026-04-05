# Mic Breakout Board — Stereo Design (V5)

Single PCB design used for both primary and secondary mic. Solder jumper selects
L/R channel. Two boards daisy-chained with 10-15cm wire spacing for stereo TDOA
bearing estimation.

Components: IM72D128 PDM mic, 100nF decoupling cap, 3-pad solder jumper.
Cross-reference: `mic_breakout_bom.csv`

---

## Physical Layout

- **Top side:** IM72D128, 100nF cap, 2× wire pad sets (4 pads each), solder jumper
- **Bottom side:** Nothing — bare flat surface for adhesive mounting to enclosure
- **Sound hole:** 0.8mm NPTH through PCB, centered under IM72D128 acoustic port
- **Board size:** ~10×8mm
- **Mounting:** Adhesive (double-sided tape or glue) face-down on enclosure interior, sound hole aligned with drilled hole in case. Optional PTFE vent or acoustic mesh over external hole.

---

## IM72D128V01 Pin Definitions

| Pin | Name | Function |
|-----|------|----------|
| 1 | DATA | PDM data output |
| 2 | VDD | Power supply (1.62–3.6V) |
| 3 | CLK | PDM clock input |
| 4 | SEL | Channel select (GND=left/falling edge, VDD=right/rising edge) |
| 5 | GND | Ground |

---

## Solder Jumper (SJ1)

Three-pad jumper to select L/R channel:

```
  GND ─[pad]─[center]─[pad]─ VDD
                 │
               MIC1.4 (SEL)
```

- **Primary board:** Bridge center to GND pad → left channel (data on falling clock edge)
- **Secondary board:** Bridge center to VDD pad → right channel (data on rising clock edge)

---

## Wire Pad Sets

Two identical 4-pad groups on the top side for daisy-chaining:

| Pad Set | Pads | Purpose |
|---------|------|---------|
| **IN** | CLK, DATA, VDD, GND | Wires from main board (primary) or from primary board (secondary) |
| **OUT** | CLK, DATA, VDD, GND | Wires to secondary board (or unused on last board in chain) |

All IN and OUT pads for the same signal are connected on the PCB — they're just
two solder points on the same net for pass-through wiring.

---

## Net Connections

| Net | Connected Pins |
|-----|----------------|
| **PDM_CLK** | MIC1.3 (CLK), IN.CLK, OUT.CLK |
| **PDM_DATA** | MIC1.1 (DATA), IN.DATA, OUT.DATA |
| **VDD** | MIC1.2 (VDD), C1.+, IN.VDD, OUT.VDD, SJ1.VDD pad |
| **GND** | MIC1.5 (GND), C1.−, IN.GND, OUT.GND, SJ1.GND pad |
| **SEL** | MIC1.4 (SEL), SJ1.center pad |

---

## Wiring — Stereo Pair

```
Main Board CN2              Primary Board (L)         Secondary Board (R)
(CLK,DATA,VDD,GND)         SJ1→GND                   SJ1→VDD
    │                           │                          │
    └── wire 10-30cm ──→ IN pads                           │
                          OUT pads ── wire 10-15cm ──→ IN pads
                                                       OUT pads (unused)
```

Main board CN2 pinout (unchanged from V3):

| CN2 Pin | Signal | STM32 Pin |
|---------|--------|-----------|
| 1 | PDM_CLK | PE9 / ADF1_CCK0 |
| 2 | PDM_DATA | PE10 / ADF1_SDI0 |
| 3 | VDD (3.3V) | 3V3 rail |
| 4 | GND | GND |

---

## Stereo Mic Spacing

The spacing between primary and secondary boards determines the TDOA baseline:

| Spacing | Max TDOA at 48kHz | Suitable Frequency Range |
|---------|-------------------|------------------------|
| 7 cm | ~10 samples | > 2.4 kHz |
| 10 cm | ~14 samples | > 1.7 kHz |
| 15 cm | ~21 samples | > 1.1 kHz |
| 20 cm | ~28 samples | > 860 Hz |

For bobwhite quail calls (~1.5–2.5 kHz), 10–15 cm spacing is ideal.
Mount both boards rigidly (fixed spacing) for consistent TDOA calibration.

---

## Assembly Notes

- **Stencil aperture:** Reduce to 70% for IM72D128 pads (excess paste shorts pads)
- **Reflow:** Hot plate or oven, standard lead-free profile
- **Solder jumper:** Bridge with iron after reflow — choose L or R per board role
- **Wire attachment:** Solder 4-conductor wire to top-side pads after reflow
- **Sound hole:** Verify 0.8mm NPTH is clear after assembly — no solder or flux in hole
- **No copper under sound hole** on either layer — keep acoustic path clear

---

## Key Changes from V3 Mic Breakout

| Feature | V3 | V5 |
|---------|----|----|
| Connector | JST SH 1.0mm 4-pin | None — wire solder pads |
| Channels | Mono (L only) | Stereo (L/R via solder jumper) |
| L/R select | SEL hardwired to GND | 3-pad solder jumper (GND or VDD) |
| Wire pads | None | 2 sets of 4 pads (daisy-chain) |
| Bottom side | Ground pour + components | Bare — flat for adhesive mount |
| All components | Top + bottom | Top only |
