# V4 Schematic Delta — Solar Charge Controller + 1S2P Battery

Changes from V3:
1. Add CN3791 solar MPPT charge controller circuit
2. Battery changed from 1S4P (13,600 mAh) to 1S2P (6,800 mAh)

---

## Deleted Items

None — all V3 components retained.

---

## New Nets

| Net | Description | Pins |
|-----|-------------|------|
| **SOLAR+** | Solar panel positive | CN3.1, D2.2(anode) |
| **SOLAR_IN** | Post-diode solar input | D2.1(cathode), U7.9(VCC), C20.+, C23.2, R14.1, M1.2(Source) |
| **SOLAR_SW** | Buck switching node | M1.3(Drain), D1.1(cathode), L2.1 |
| **SOLAR_DRV** | P-FET gate drive | U7.10(DRV), M1.1(Gate) |
| **SOLAR_VG** | Gate drive regulator bypass | U7.1(VG), C23.1 |
| **SOLAR_MPPT** | MPPT divider tap | U7.6(MPPT), R14.2, R15.1 |
| **SOLAR_CSP** | Current sense positive | U7.8(CSP), RCS.1, L2.2 |
| **SOLAR_COM** | Loop compensation | U7.5(COM), R18.1 |
| **SOLAR_COMP** | R18/C22 series node | R18.2, C22.1 |
| **CHRG_STATUS** | Charging indicator | U7.3(CHRG), R16.2, U1.35(PB0) |
| **DONE_STATUS** | Charge complete | U7.4(DONE), R17.2, U1.36(PB1) |

**Modified existing nets:**
- **VBAT+** — add: U7.7(BAT), C21.+, RCS.2
- **GND** — add: U7.2, C20.−, C21.−, C22.2, D1.2(anode), R15.2, CN3.2
- **3V3** — add: R16.1, R17.1

---

## New Components

| Designator | Description | LCSC | Connections |
|------------|-------------|------|-------------|
| U7 | CN3791 solar MPPT charger, SSOP-10 | C154992 | 1→SOLAR_VG, 2→GND, 3→CHRG_STATUS, 4→DONE_STATUS, 5→SOLAR_COM, 6→SOLAR_MPPT, 7→VBAT+, 8→SOLAR_CSP, 9→SOLAR_IN, 10→SOLAR_DRV |
| M1 | SI2301CDS P-FET, SOT-23 | C10487 | 1(Gate)→SOLAR_DRV, 2(Source)→SOLAR_IN, 3(Drain)→SOLAR_SW |
| D1 | SS14 Schottky 1A 40V, SMA | C2480 | 1(cathode)→SOLAR_SW, 2(anode)→GND |
| D2 | SS14 Schottky 1A 40V, SMA | C2480 | 1(cathode)→SOLAR_IN, 2(anode)→SOLAR+ |
| L2 | 22µH shielded inductor, 5020 | C329665 | 1→SOLAR_SW, 2→SOLAR_CSP |
| RCS | 0.2Ω 1% 1W current sense, 1206 | C5127778 | 1→SOLAR_CSP, 2→VBAT+ |
| R14 | 360kΩ 1%, 0603 | C2907029| 1→SOLAR_IN, 2→SOLAR_MPPT |
| R15 | 100kΩ 1%, 0603 | C25803 | 1→SOLAR_MPPT, 2→GND |
| R16 | 10kΩ 1%, 0603 | C25804 | 1→3V3, 2→CHRG_STATUS |
| R17 | 10kΩ 1%, 0603 | C25804 | 1→3V3, 2→DONE_STATUS |
| R18 | 120Ω 1%, 0603 | C22787 | 1→SOLAR_COM, 2→SOLAR_COMP |
| C20 | 10µF X5R 25V, 0805 | C15850 | +→SOLAR_IN, −→GND |
| C21 | 10µF X5R 25V, 0805 | C15850 | +→VBAT+, −→GND |
| C22 | 220nF X7R 25V, 0603 | C441745 | 1→SOLAR_COMP, 2→GND |
| C23 | 100nF X7R 16V, 0402 | C1525 | 1→SOLAR_VG, 2→SOLAR_IN |
| CN3 | JST PH 2-pin TH vertical (solar panel) | C131337 | 1→SOLAR+, 2→GND |

**Note:** R18 and C22 are in **series** from COM to GND (COM → R18 → SOLAR_COMP → C22 → GND). They must NOT be wired in parallel.

---

## MCU Pin Assignments (new)

| Pin | GPIO | Net | Function |
|-----|------|-----|----------|
| 35 | PB0 | CHRG_STATUS | Charging indicator (active low, open drain + 10k pull-up) |
| 36 | PB1 | DONE_STATUS | Charge complete (active low, open drain + 10k pull-up) |

---

## Layout Notes

- **Switching loop (critical):** M1 drain → L2 → RCS → C21 → GND → D1 → M1 drain. Keep this loop as tight and short as possible — it carries pulsed current at 300kHz and is the primary source of EMI.
- **C20** (input) close to U7 pin 9 (VCC) and pin 2 (GND) with short, wide traces.
- **C21** (output) close to U7 pin 7 (BAT) and pin 2 (GND).
- **RCS** (current sense): route Kelvin traces from RCS pads to U7 pin 8 (CSP) and pin 7 (BAT). Keep sense traces parallel and away from switching node.
- **D1** (freewheeling diode) close to L2 and M1 — part of the switching loop.
- **M1** gate trace (SOLAR_DRV) short to U7 pin 10. Keep away from MPPT divider and sense traces.
- **C23** (VG bypass) directly across U7 pin 1 (VG) to pin 9 (VCC), as close to IC as possible.
- **R14/R15** (MPPT divider) away from switching node and inductor to avoid noise coupling into the MPPT regulation point.
- **R18 + C22** (compensation) close to U7 pin 5 (COM). Keep traces short — this is a sensitive analog node.
- **Ground plane:** Unbroken copper pour under U7, L2, M1, D1, RCS. Do not route signal traces through this area.
- **CN3** (solar input) at board edge. Route SOLAR+ and GND as a pair with wide traces (≥0.5mm) to handle up to 500mA.
