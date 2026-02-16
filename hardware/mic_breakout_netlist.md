# Mic Breakout Board — Netlist / Connections

3 components: IM73D122 PDM mic, 100nF decoupling cap, JST PH 4-pin connector.
Cross-reference: `mic_breakout_bom.csv`

---

## IM73D122V01 Pin Definitions

| Pin | Name | Function |
|-----|------|----------|
| 1 | DATA | PDM data output |
| 2 | VDD | Power supply (1.62–3.6V) |
| 3 | CLK | PDM clock input |
| 4 | SEL | Channel select (GND=left/rising, VDD=right/falling) |
| 5 | GND | Ground |

---

## Net Connections

| Net | Connected Pins |
|-----|----------------|
| **PDM_CLK** | MIC1.3 (CLK), CN1.1 |
| **PDM_DATA** | MIC1.1 (DATA), CN1.2 |
| **VDD** | MIC1.2 (VDD), C1.+, CN1.3 |
| **GND** | MIC1.4 (SEL), MIC1.5 (GND), C1.−, CN1.4, CN1.5 (mount tab), CN1.6 (mount tab) |

---

## JST PH Connector Pinout (CN1)

| CN1 Pin | Signal | Main Board CN2 Pin |
|---------|--------|-------------------|
| 1 | PDM_CLK | 1 (→ PE9 / ADF1_CCK0) |
| 2 | PDM_DATA | 2 (→ PE10 / ADF1_SDI0) |
| 3 | VDD (3.3V) | 3 |
| 4 | GND | 4 |

Cable: JST PH 4-wire, straight-through (pin 1 to pin 1).

---

## Key Notes

- **SEL pin tied to GND** — selects left channel (data valid on rising clock edge)
- **C1 (100nF)** placed as close as possible to MIC1 pins 2 (VDD) and 5 (GND)
- **Sound hole** (0.8mm NPTH) centered under MIC1 acoustic port on bottom side
- **No copper** under the sound hole — keep the acoustic path clear
- **Ground pour** on bottom layer under mic, but broken around the sound hole
