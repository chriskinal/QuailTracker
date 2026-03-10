# V3 Schematic Delta — GPS Module Replacement + Switched Peripheral Rail

Changes from V2 schematic:
1. Replace Seeed L76K GPS breakout with ATGM336H-5N31 bare SMD module
2. Rename GPS_VCC net to SW_VCC (Switched VCC) — all peripherals on one switched rail for Stop 2 sleep

---

## Summary of Changes

| Change | What | Why |
|--------|------|-----|
| Replace U2 | L76K XIAO breakout → ATGM336H-5N31 LCC-18 SMD | Direct PCB mount, no breakout/bodge wires, $1.93 vs ~$7 |
| Add L1 | 47nH 0402 inductor | Bias tee for active antenna DC power injection |
| Add J1 | U.FL/IPEX MHF receptacle | GPS active antenna coax connector |
| Add C17 | 10uF 0805 capacitor | SW_VCC decoupling |
| Remove | L76K bodge wires (PPS, VBKP) | All pins directly accessible on ATGM336H |
| Rename net | GPS_VCC → **SW_VCC** | GPS-only switched rail |
| Add net | **PERIPH_VCC** | New switched rail for BLE, SD, SHT30 |
| Add Q4 | SI2301CDS P-FET | Peripheral power switch (same part as Q2) |
| Add Q5 | MMBT3904 NPN | Peripheral FET gate drive (same part as Q3) |
| Add R10, R11 | 10k 0603 | Q4 gate pull-up + Q5 base resistor |
| Add C18 | 10uF 0805 | PERIPH_VCC decoupling |
| Move to PERIPH_VCC | CARD1.VDD, COMM1.VCC, H1.1 | SD card, BLE module, SHT30 off 3V3, onto peripheral rail |
| New net: GPS_VBAT | U2.pin6 → 3V3 (always-on) | RTC/hot-start backup power |
| New net: GPS_VCC_RF | U2.pin14 → L1.1 | Antenna LNA power from module |
| New net: GPS_RF_IN | L1.2 → U2.pin11 + J1.signal | RF signal + DC bias path |

---

## 1. Replace GPS Module U2

**Old U2 (V2):** Seeed L76K on XIAO footprint — off-board, through-hole header, required bodge wires for PPS and VBKP.

**New U2 (V3):** ATGM336H-5N31 — 18-pin LCC SMD package (10.1x9.7mm), soldered directly on PCB. JLCPCB assembleable (LCSC C90770).

### ATGM336H-5N31 Pin Connections

| U2 Pin | Name | I/O | Connects To | Net Port |
|--------|------|-----|-------------|----------|
| 1 | GND | - | GND | GND |
| 2 | TXD | O | U1.pin69 (PA10 / USART1_RX) | GPS_TX |
| 3 | RXD | I | U1.pin68 (PA9 / USART1_TX) | GPS_RX |
| 4 | 1PPS | O | U1.pin67 (PA8 / EXTI) | GPS_PPS |
| 5 | ON/OFF | I | U1.pin61 (PD14) | GPS_WAKE |
| 6 | VBAT | I | 3V3 rail (always-on) | GPS_VBAT |
| 7 | NC | - | — | — |
| 8 | VCC | I | Q2.Drain (switched) + C17.+ | SW_VCC |
| 9 | nRESET | I | U1.pin62 (PD15) | GPS_RST |
| 10 | GND | - | GND | GND |
| 11 | RF_IN | I | L1.2 + J1.signal | GPS_RF_IN |
| 12 | GND | - | GND | GND |
| 13 | NC | - | — | — |
| 14 | VCC_RF | O | L1.1 | GPS_VCC_RF |
| 15 | Reserved | - | Float (NC) | — |
| 16 | SDA | I/O | NC | — |
| 17 | SCL | O | NC | — |
| 18 | Reserved | - | Float (NC) | — |

**EasyEDA steps:**
1. Delete old U2 (L76K XIAO footprint / header)
2. Search LCSC for C90770 (ATGM336H-5N31) — place the LCC-18 footprint
3. Apply existing net port labels to pins: GPS_TX (pin 2), GPS_RX (pin 3), GPS_PPS (pin 4), GPS_WAKE (pin 5), GPS_RST (pin 9), SW_VCC (pin 8)
4. Add new net port label **GPS_VBAT** on pin 6, connect to 3V3 rail
5. Add GND net port labels on pins 1, 10, 12
6. Leave pins 7, 13, 15, 16, 17, 18 unconnected

---

## 2. Add RF Bias Tee Circuit (L1 + J1)

Active GPS antennas have a built-in LNA that needs DC power. The ATGM336H provides 3.3V on VCC_RF (pin 14). A bias tee injects this DC into the coax while passing the RF signal through.

**Circuit:**
```
U2.pin14 (VCC_RF) ──── L1 (47nH) ──── U2.pin11 (RF_IN)
                                          │
                                       J1 (U.FL)
                                          │
                                     to antenna
```

L1 blocks RF from entering VCC_RF (high impedance at 1.5 GHz) while passing DC to power the antenna LNA through the coax center conductor.

**EasyEDA steps:**
1. Place L1: 47nH 0402 inductor (LCSC C97998, Murata LQG15HS47NJ02D)
2. Place J1: U.FL receptacle (LCSC C88374, HRS U-FL-R-SMT-1)
3. Add net port label **GPS_VCC_RF** on U2.pin14 and L1.1
4. Add net port label **GPS_RF_IN** on L1.2, U2.pin11, and J1.signal
5. Add GND net port on J1.GND

**Connections:**

| From | To | Net Port |
|------|----|----------|
| U2.pin14 (VCC_RF) | L1.1 | GPS_VCC_RF |
| L1.2 | U2.pin11 (RF_IN) | GPS_RF_IN |
| L1.2 | J1.signal | GPS_RF_IN |
| J1.GND | GND | GND |

---

## 3. Add GPS VCC Decoupling (C17)

**Circuit:**
```
SW_VCC ──┬── U2.pin8 (VCC)
           │
         C17 (10uF)
           │
          GND
```

**EasyEDA steps:**
1. Place C17: 10uF 0805 (LCSC C15850 — same part as C11)
2. Connect C17.+ to SW_VCC net (same net as Q2.Drain and U2.pin8)
3. Connect C17.− to GND
4. Place close to U2.pin8

---

## 4. Add GPS_VBAT Power Connection

VBAT (pin 6) powers the ATGM336H's RTC and SRAM for hot-start capability. It must connect to the **always-on 3V3 rail**, NOT the switched SW_VCC — otherwise the module loses time/ephemeris data when GPS power is off.

**EasyEDA steps:**
1. Add net port label **GPS_VBAT** on U2.pin6
2. Connect GPS_VBAT to the 3V3 net (add GPS_VBAT to the 3V3 power net, or place a short wire from U2.pin6 to a 3V3 net port)

This replaces the V2 bodge wire that routed L76K VBKP to the 3V3 rail.

---

## 5. Removed Items

Delete these from the V2 schematic:

| Item | Reason |
|------|--------|
| L76K XIAO header footprint | Replaced by ATGM336H-5N31 SMD footprint |
| PPS bodge wire note | ATGM336H pin 4 (1PPS) directly accessible |
| VBKP bodge wire note | ATGM336H pin 6 (VBAT) directly accessible |

---

## 6. Two Switched Power Rails (SW_VCC + PERIPH_VCC)

V2 had only the GPS module on a switched power rail (GPS_VCC). V3 splits peripherals across two independent switched rails so GPS can be powered on/off for periodic clock sync without interrupting SD card recording.

**Why two rails?** During recording, SD card and BLE must stay powered while GPS is toggled periodically for time sync. A single shared rail would kill recording every time GPS powers down. Additionally, GPS control pins (PD14/PD15) must stay driven during Stop 2 and can back-power GPS VCC through ESD diodes — separate rails prevent this from parasitically powering other peripherals.

### SW_VCC — GPS switched rail (existing circuit)

Controlled by **PD12** (pin 59) via Q3 (NPN) + Q2 (P-FET). GPS module only — toggled periodically for clock sync during recording.

| Component | Pin | Net | Notes |
|-----------|-----|-----|-------|
| U2 (ATGM336H) | pin 8 (VCC) | SW_VCC | GPS main power |
| C17 | 10uF | SW_VCC | Decoupling |

### PERIPH_VCC — Peripheral switched rail (new circuit)

Controlled by **PD11** (pin 58) via Q5 (NPN) + Q4 (P-FET). Same circuit topology as GPS switch. Stays on during recording, off during Stop 2 sleep.

| Component | Pin | V2 Net | V3 Net | Notes |
|-----------|-----|--------|--------|-------|
| CARD1 (SD card) | VDD (pin 4) | 3V3 | PERIPH_VCC | Unmount FS before power cut |
| COMM1 (PB-03F BLE) | VCC | 3V3 | PERIPH_VCC | Re-init AT config on wake |
| H1 (SHT30 header) | pin 1 | 3V3 | PERIPH_VCC | Stateless, just works on power-up |

### Peripheral power switch circuit (Q4/Q5 — identical to Q2/Q3)

```
3V3 ──── R10 (10k) ──┬── Q4.Gate (P-FET SI2301CDS)
                      │
                Q5.Collector (NPN MMBT3904)
                      │
PD11 ── R11 (10k) ── Q5.Base
                      │
                Q5.Emitter ── GND

Q4.Source ── 3V3
Q4.Drain ── PERIPH_VCC ── C18 (10uF) ── GND
```

- **PERIPH_EN HIGH** (PD11) → Q5 ON → Q4 gate LOW → P-FET ON → PERIPH_VCC = 3.3V
- **PERIPH_EN LOW** (PD11) → Q5 OFF → R10 pulls gate HIGH → P-FET OFF → PERIPH_VCC = 0V

### Still on always-on 3V3

| Component | Pin | Why |
|-----------|-----|-----|
| U2 (ATGM336H) | pin 6 (VBAT) | GPS RTC/SRAM for hot-start (~7µA) |
| U1 (STM32U575) | all VDD/VDDA | MCU must stay powered for Stop 2 SRAM retention |
| R3, R4 | I2C pull-ups | On 3V3 — move to PERIPH_VCC in future rev to eliminate leakage |

**PCB layout note:** Do NOT route 3V3 through H1 pin 1 as a via. V1 board had a hidden bottom-layer 3V3 trace through H1.1 that kept peripherals powered during Stop 2. H1.1 must connect ONLY to PERIPH_VCC.

### Power modes

| Mode | SW_VCC (GPS) | PERIPH_VCC | MCU | Current |
|------|-------------|------------|-----|---------|
| Active + GPS fix | ON | ON | Run | ~72mA |
| Recording (GPS off) | OFF | ON | Run | ~25mA |
| Recording (GPS sync) | ON | ON | Run | ~72mA |
| Deep sleep (Stop 2) | OFF | OFF | Stop 2 | <1mA |

### Firmware sequence for sleep

1. `f_close()` / `f_mount(NULL)` — unmount SD card
2. Flush BLE, disable UART interrupts
3. Set peripheral GPIO pins to analog (hi-Z) — prevents ESD back-powering
4. PD12 LOW → SW_VCC off (GPS)
5. PD11 LOW → PERIPH_VCC off (BLE, SD, SHT30)
6. `enterStop2(seconds)` — MCU enters Stop 2
7. On wake: PD11 HIGH → PERIPH_VCC on, PD12 HIGH → SW_VCC on, delay for stabilization
8. Restore GPIO modes, re-init SPI/SD, re-init BLE AT config, GPS hot-start from VBAT

---

## 7. Net Port Changes

### Existing nets — pin reference updates only (no new wiring)

| Net Port | V2 Connection | V3 Connection |
|----------|---------------|---------------|
| GPS_TX | U2.TX (XIAO pin 14) | U2.pin2 (TXD) |
| GPS_RX | U2.RX (XIAO pin 1) | U2.pin3 (RXD) |
| GPS_PPS | U2.pin3 (XIAO bodge) | U2.pin4 (1PPS) |
| GPS_WAKE | U2.WAKEUP (XIAO pin 7) | U2.pin5 (ON/OFF) |
| GPS_RST | U2.RESET (XIAO pin 11) | U2.pin9 (nRESET) |
| SW_VCC | Q2.Drain, U2.VCC | Q2.Drain, U2.pin8, C17.+ |
| GND | U2.GND | U2.pin1, U2.pin10, U2.pin12, J1.GND, C17.− |

### New nets

| Net Port | Connected Pins | Description |
|----------|----------------|-------------|
| **GPS_VBAT** | U2.pin6, 3V3 rail | Always-on backup power |
| **GPS_VCC_RF** | U2.pin14, L1.1 | Antenna LNA power from module |
| **GPS_RF_IN** | L1.2, U2.pin11, J1.signal | RF + DC bias to antenna |
| **PERIPH_VCC** | Q4.Drain, C18.+, CARD1.VDD, COMM1.VCC, H1.1 | Peripheral switched rail |
| **PERIPH_EN** | R11.1, U1.pin58 (PD11) | Peripheral power enable |
| **Q4_GATE** | Q4.Gate, R10.2, Q5.Collector | Peripheral P-FET gate drive |

---

## 8. New BOM Items

| Designator | Part | Package | LCSC | Mouser | Qty | Notes |
|------------|------|---------|------|--------|-----|-------|
| U2 | ATGM336H-5N31 GPS+BDS module | LCC-18 (10.1x9.7mm) | C90770 | N/A (LCSC only) | 1 | Replaces L76K breakout |
| C17 | 10uF X5R 25V (YAGEO CC0805KFX5R8BB106) | 0805 | C15850 | 603-CC0805KFX5R8BB10 | 1 | SW_VCC decoupling (same part as C11) |
| L1 | 47nH multilayer inductor (Murata LQG15HS47NJ02D) | 0402 | C97998 | 81-LQG15HS47NJ02D | 1 | Bias tee |
| J1 | U.FL receptacle (HRS U-FL-R-SMT-1(80)) | SMD | C88374 | 798-U-FL-R-SMT-1-80 | 1 | GPS antenna connector |
| Q4 | SI2301CDS P-FET | SOT-23 | C10487 | 781-SI2301CDS-E3 | 1 | PERIPH_VCC power switch (same as Q2) |
| Q5 | MMBT3904 NPN | SOT-23 | C20526 | 637-MMBT3904 | 1 | Peripheral FET gate drive (same as Q3) |
| R10 | 10k 1% | 0603 | C25804 | 603-RC0603FR-0710KL | 1 | Q4 gate pull-up |
| R11 | 10k 1% | 0603 | C25804 | 603-RC0603FR-0710KL | 1 | Q5 base resistor |
| C18 | 10uF X5R 25V (same as C11/C17) | 0805 | C15850 | 603-CC0805KFX5R8BB10 | 1 | PERIPH_VCC decoupling |

### Removed BOM Items

| Designator | Part | Notes |
|------------|------|-------|
| U2 (old) | Seeed L76K XIAO breakout | Off-board module, no longer used |

**Cost impact:** GPS drops from ~$7.00 (L76K breakout + antenna) to ~$5.50 (ATGM336H + bias tee + U.FL + separate antenna). Net savings ~$1.50/unit.

---

## 9. PCB Layout Notes

- Place U2 (ATGM336H) near board edge — RF trace to J1 should be short
- J1 (U.FL) at board edge for antenna cable routing
- L1 between U2.pin14 and U2.pin11 — keep bias tee traces as short as possible
- C17 close to U2.pin8 (VCC)
- Ground pour under U2 and RF section — connect U2 ground pads (pins 1, 10, 12) to pour with short traces or vias
- No copper pour directly under J1 signal pad (RF clearance)
- Silkscreen: label J1 as "GPS ANT"

---

## 10. MCU Pin Mapping

| MCU Pin | GPIO | Function | V2 Target | V3 Target |
|---------|------|----------|-----------|-----------|
| 68 | PA9 | USART1_TX | L76K RX | ATGM336H RXD (pin 3) |
| 69 | PA10 | USART1_RX | L76K TX | ATGM336H TXD (pin 2) |
| 67 | PA8 | EXTI (PPS) | L76K PPS (bodge) | ATGM336H 1PPS (pin 4) |
| 61 | PD14 | GPIO out | L76K WAKEUP | ATGM336H ON/OFF (pin 5) |
| 62 | PD15 | GPIO out | L76K RESET | ATGM336H nRESET (pin 9) |
| 59 | PD12 | GPIO out | GPS_EN (Q3 base) | SW_VCC_EN (Q3 base) — GPS only |
| 58 | PD11 | GPIO out | — (new) | PERIPH_EN (Q5 base) — BLE, SD, SHT30 |

NMEA 9600 baud, same UART, same PPS polarity. GPS power switch circuit (Q2/Q3/R1/R2) unchanged — switches SW_VCC (GPS only). New Q4/Q5/R10/R11 circuit on PD11 switches PERIPH_VCC (BLE + SD + SHT30).
