# V3 Schematic Delta — GPS Module Replacement

Changes from V2 schematic to replace Seeed L76K GPS breakout with ATGM336H-5N31 bare SMD module.

---

## Summary of Changes

| Change | What | Why |
|--------|------|-----|
| Replace U2 | L76K XIAO breakout → ATGM336H-5N31 LCC-18 SMD | Direct PCB mount, no breakout/bodge wires, $1.93 vs ~$7 |
| Add L1 | 47nH 0402 inductor | Bias tee for active antenna DC power injection |
| Add J1 | U.FL/IPEX MHF receptacle | GPS active antenna coax connector |
| Add C17 | 10uF 0805 capacitor | GPS VCC decoupling |
| Remove | L76K bodge wires (PPS, VBKP) | All pins directly accessible on ATGM336H |
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
| 8 | VCC | I | Q2.Drain (switched) + C17.+ | GPS_VCC |
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
3. Apply existing net port labels to pins: GPS_TX (pin 2), GPS_RX (pin 3), GPS_PPS (pin 4), GPS_WAKE (pin 5), GPS_RST (pin 9), GPS_VCC (pin 8)
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
GPS_VCC ──┬── U2.pin8 (VCC)
           │
         C17 (10uF)
           │
          GND
```

**EasyEDA steps:**
1. Place C17: 10uF 0805 (LCSC C15850 — same part as C11)
2. Connect C17.+ to GPS_VCC net (same net as Q2.Drain and U2.pin8)
3. Connect C17.− to GND
4. Place close to U2.pin8

---

## 4. Add GPS_VBAT Power Connection

VBAT (pin 6) powers the ATGM336H's RTC and SRAM for hot-start capability. It must connect to the **always-on 3V3 rail**, NOT the switched GPS_VCC — otherwise the module loses time/ephemeris data when GPS power is off.

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

## 6. Net Port Changes

### Existing nets — pin reference updates only (no new wiring)

| Net Port | V2 Connection | V3 Connection |
|----------|---------------|---------------|
| GPS_TX | U2.TX (XIAO pin 14) | U2.pin2 (TXD) |
| GPS_RX | U2.RX (XIAO pin 1) | U2.pin3 (RXD) |
| GPS_PPS | U2.pin3 (XIAO bodge) | U2.pin4 (1PPS) |
| GPS_WAKE | U2.WAKEUP (XIAO pin 7) | U2.pin5 (ON/OFF) |
| GPS_RST | U2.RESET (XIAO pin 11) | U2.pin9 (nRESET) |
| GPS_VCC | Q2.Drain, U2.VCC | Q2.Drain, U2.pin8, C17.+ |
| GND | U2.GND | U2.pin1, U2.pin10, U2.pin12, J1.GND, C17.− |

### New nets

| Net Port | Connected Pins | Description |
|----------|----------------|-------------|
| **GPS_VBAT** | U2.pin6, 3V3 rail | Always-on backup power |
| **GPS_VCC_RF** | U2.pin14, L1.1 | Antenna LNA power from module |
| **GPS_RF_IN** | L1.2, U2.pin11, J1.signal | RF + DC bias to antenna |

---

## 7. New BOM Items

| Designator | Part | Package | LCSC | Mouser | Qty | Notes |
|------------|------|---------|------|--------|-----|-------|
| U2 | ATGM336H-5N31 GPS+BDS module | LCC-18 (10.1x9.7mm) | C90770 | N/A (LCSC only) | 1 | Replaces L76K breakout |
| C17 | 10uF X5R 25V (YAGEO CC0805KFX5R8BB106) | 0805 | C15850 | 603-CC0805KFX5R8BB10 | 1 | GPS VCC decoupling (same part as C11) |
| L1 | 47nH multilayer inductor (Murata LQG15HS47NJ02D) | 0402 | C97998 | 81-LQG15HS47NJ02D | 1 | Bias tee |
| J1 | U.FL receptacle (HRS U-FL-R-SMT-1(80)) | SMD | C88374 | 798-U-FL-R-SMT-1-80 | 1 | GPS antenna connector |

### Removed BOM Items

| Designator | Part | Notes |
|------------|------|-------|
| U2 (old) | Seeed L76K XIAO breakout | Off-board module, no longer used |

**Cost impact:** GPS drops from ~$7.00 (L76K breakout + antenna) to ~$5.50 (ATGM336H + bias tee + U.FL + separate antenna). Net savings ~$1.50/unit.

---

## 8. PCB Layout Notes

- Place U2 (ATGM336H) near board edge — RF trace to J1 should be short
- J1 (U.FL) at board edge for antenna cable routing
- L1 between U2.pin14 and U2.pin11 — keep bias tee traces as short as possible
- C17 close to U2.pin8 (VCC)
- Ground pour under U2 and RF section — connect U2 ground pads (pins 1, 10, 12) to pour with short traces or vias
- No copper pour directly under J1 signal pad (RF clearance)
- Silkscreen: label J1 as "GPS ANT"

---

## 9. MCU Pin Mapping — Unchanged

No firmware changes needed. All MCU GPIO assignments are identical to V2:

| MCU Pin | GPIO | Function | V2 Target | V3 Target |
|---------|------|----------|-----------|-----------|
| 68 | PA9 | USART1_TX | L76K RX | ATGM336H RXD (pin 3) |
| 69 | PA10 | USART1_RX | L76K TX | ATGM336H TXD (pin 2) |
| 67 | PA8 | EXTI (PPS) | L76K PPS (bodge) | ATGM336H 1PPS (pin 4) |
| 61 | PD14 | GPIO out | L76K WAKEUP | ATGM336H ON/OFF (pin 5) |
| 62 | PD15 | GPIO out | L76K RESET | ATGM336H nRESET (pin 9) |
| 59 | PD12 | GPIO out | GPS_EN (Q3 base) | GPS_EN (Q3 base) — unchanged |

NMEA 9600 baud, same UART, same PPS polarity. GPS power switch circuit (Q2/Q3/R1/R2) unchanged.
