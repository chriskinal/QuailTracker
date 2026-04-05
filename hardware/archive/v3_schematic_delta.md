# V3 Schematic Delta — GPS Module Replacement + Switched Peripheral Rail

Changes from V2 schematic:
1. Replace Seeed L76K GPS breakout with ATGM336H-5N31 bare SMD module
2. Three independent switched power rails (GPS_VCC, BLE_VCC, PERIPH_VCC) using TPS22916 load switches

---

## Summary of Changes

| Change | What | Why |
|--------|------|-----|
| Replace U2 | L76K XIAO breakout → ATGM336H-5N31 LCC-18 SMD | Direct PCB mount, no breakout/bodge wires, $1.93 vs ~$7 |
| Add L1 | 47nH 0402 inductor | Bias tee for active antenna DC power injection |
| Add J1 | U.FL/IPEX MHF receptacle | GPS active antenna coax connector |
| Add C17 | 10uF 0805 capacitor | GPS_VCC decoupling |
| Remove | L76K bodge wires (PPS, VBKP) | All pins directly accessible on ATGM336H |
| Rename net | GPS_VCC (was SW_VCC) | GPS-only switched rail |
| Add net | **BLE_VCC** | New switched rail for BLE module only |
| Add net | **PERIPH_VCC** | New switched rail for SD + SHT30 |
| Add U4/U5/U6 | TPS22916 load switch x3 | Replace discrete P-FET+NPN circuits |
| Add C18, C19 | 1uF 0402 caps | Load switch output decoupling |
| Move COMM1.VCC | 3V3 → BLE_VCC | BLE on independent switchable rail |
| Move CARD1.VDD, H1.1 | 3V3 → PERIPH_VCC | SD + SHT30 on switchable rail |
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

## 6. Three Switched Power Rails (GPS_VCC + BLE_VCC + PERIPH_VCC)

V2 had only the GPS module on a switched power rail. V3 uses three independent
switched rails so each peripheral group can be powered on/off independently.

**Why three rails?**
- **GPS_VCC**: GPS toggled periodically for time sync during recording without
  interrupting SD card or BLE. GPS control pins (PD14/PD15) stay driven during
  Stop 2 and can back-power GPS VCC through ESD diodes — separate rail prevents
  parasitic powering of other peripherals.
- **BLE_VCC**: PB-03F must stay powered during Stop 2 for BLE advertising
  (AT+SLEEP=0, 275µA). On MCU wake, power-cycle BLE_VCC to recover module from
  sleep mode 0 (the only reliable wake method — GPIO/UART wake doesn't work on
  this firmware). Independent from SD/SHT30 so BLE can advertise while other
  peripherals are off.
- **PERIPH_VCC**: SD card and SHT30 have no reason to stay powered during sleep.
  Grouped together since they're always on/off together.

### Implementation: TPS22916 Load Switch ICs

Each rail uses a TPS22916 (or equivalent) single-channel load switch instead of
the V2 discrete circuit (P-FET + NPN + 2x 10k resistors). Benefits:
- 1 IC + 1 cap per rail vs 4 discrete components
- GPIO-controlled ON pin (active high) — deterministic behavior during Stop 2
  (STM32 GPIOs hold state in Stop 2, no I2C bus or register state to worry about)
- ~1nA quiescent when off, <1µA when on
- SOT-23-5 package, same footprint as the old P-FETs

```
         TPS22916 (per rail)
3V3 ─── VIN ┌──────┐ VOUT ─── GPS_VCC / BLE_VCC / PERIPH_VCC
             │      │
GPIO ─── ON │      │ (internal ~1MΩ pull-down = OFF by default)
             │      │
GND ─── GND └──────┘

+ 1uF ceramic cap on VOUT (per datasheet recommendation)
```

- **ON HIGH** → load switch conducts → rail = 3.3V
- **ON LOW or floating** → load switch off → rail = 0V (internal pull-down)
- Rise time: ~200µs (controlled slew rate, no inrush)

### GPS_VCC — GPS switched rail

Controlled by **PD12** (pin 59). GPS module only — toggled periodically for
clock sync during recording.

| Component | Pin | Net | Notes |
|-----------|-----|-----|-------|
| U2 (ATGM336H) | pin 8 (VCC) | GPS_VCC | GPS main power |
| C17 | 10uF | GPS_VCC | Decoupling |
| U4 (TPS22916) | VOUT | GPS_VCC | Load switch output |

### BLE_VCC — BLE module switched rail

Controlled by **PD10** (pin 57). Stays ON during Stop 2 sleep (BLE advertising).
Power-cycled on MCU wake from RTC to recover module from AT+SLEEP=0.

| Component | Pin | Net | Notes |
|-----------|-----|-----|-------|
| COMM1 (PB-03F) | VCC | BLE_VCC | BLE module power |
| U5 (TPS22916) | VOUT | BLE_VCC | Load switch output |
| C19 | 1uF | BLE_VCC | Output decoupling |

### PERIPH_VCC — SD card + SHT30 switched rail

Controlled by **PD11** (pin 58). On during recording, off during Stop 2 sleep.

| Component | Pin | V2 Net | V3 Net | Notes |
|-----------|-----|--------|--------|-------|
| CARD1 (SD card) | VDD (pin 4) | 3V3 | PERIPH_VCC | Unmount FS before power cut |
| H1 (SHT30 header) | pin 1 | 3V3 | PERIPH_VCC | Stateless, just works on power-up |
| U6 (TPS22916) | VOUT | — | PERIPH_VCC | Load switch output |
| C18 | 1uF | — | PERIPH_VCC | Output decoupling |

### Still on always-on 3V3

| Component | Pin | Why |
|-----------|-----|-----|
| U2 (ATGM336H) | pin 6 (VBAT) | GPS RTC/SRAM for hot-start (~7µA) |
| U1 (STM32U575) | all VDD/VDDA | MCU must stay powered for Stop 2 SRAM retention |
| R3, R4 | I2C pull-ups | On 3V3 — move to PERIPH_VCC in future rev to eliminate leakage |

**PCB layout note:** Do NOT route 3V3 through H1 pin 1 as a via. V1 board had
a hidden bottom-layer 3V3 trace through H1.1 that kept peripherals powered
during Stop 2. H1.1 must connect ONLY to PERIPH_VCC.

### Power modes

| Mode | GPS_VCC | BLE_VCC | PERIPH_VCC | MCU | Current |
|------|---------|---------|------------|-----|---------|
| Active + GPS fix | ON | ON | ON | Run | ~72mA |
| Recording (GPS off) | OFF | ON | ON | Run | ~25mA |
| Recording (GPS sync) | ON | ON | ON | Run | ~72mA |
| Sleep (Stop 2) | OFF | ON (advertising) | OFF | Stop 2 | ~280µA |

Sleep breakdown: BLE AT+SLEEP=0 = 275µA + MCU Stop 2 = ~5µA.

### Firmware sequence for sleep

1. `f_close()` / `f_mount(NULL)` — unmount SD card
2. Send `AT+SLEEP=0` to BLE — module enters light sleep with advertising (275µA)
3. Disable UART interrupts, clear USART ORE errors
4. Set peripheral GPIO pins to analog (hi-Z) except PA3 (EXTI3 wake source)
5. PD12 LOW → GPS_VCC off, PD11 LOW → PERIPH_VCC off
6. BLE_VCC stays ON (PD10 HIGH) — module continues advertising
7. Configure EXTI3 falling edge on PA3 (BLE UART RX start bit = connect event)
8. `enterStop2(seconds)` — MCU enters Stop 2

### Firmware sequence for wake (RTC timer)

1. MCU wakes from EXTI19 (RTC)
2. Restore clocks, SysTick, GPIO modes
3. PD10 LOW → BLE_VCC off (power-cycle to recover from sleep mode 0)
4. osDelay(50) — let module fully power down
5. PD10 HIGH → BLE_VCC on → module reboots (~1s boot banner)
6. PD12 HIGH → GPS_VCC on, PD11 HIGH → PERIPH_VCC on
7. Wait 1s for BLE boot, send ATE0, verify with AT+BLESTATE?
8. Re-init SPI/SD, remount filesystem, GPS hot-start from VBAT

### Firmware sequence for wake (BLE connect)

1. BLE module sends `+EVENT:BLE_CONNECTED` over UART TX
2. Start bit on PA3 → EXTI3 falling edge → MCU wakes from Stop 2
3. Restore clocks, SysTick, GPIO modes
4. BLE module is already awake (connection woke it from sleep 0)
5. PD12 HIGH → GPS_VCC on, PD11 HIGH → PERIPH_VCC on
6. Re-init SPI/SD, remount filesystem
7. BLE task reads connect event, enters command mode

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
| GPS_VCC | Q2.Drain, U2.VCC | Q2.Drain, U2.pin8, C17.+ |
| GND | U2.GND | U2.pin1, U2.pin10, U2.pin12, J1.GND, C17.− |

### New nets

| Net Port | Connected Pins | Description |
|----------|----------------|-------------|
| **GPS_VBAT** | U2.pin6, 3V3 rail | Always-on backup power |
| **GPS_VCC_RF** | U2.pin14, L1.1 | Antenna LNA power from module |
| **GPS_RF_IN** | L1.2, U2.pin11, J1.signal | RF + DC bias to antenna |
| **BLE_VCC** | U5.VOUT, C19.+, COMM1.VCC | BLE module switched rail |
| **BLE_EN** | U5.ON, U1.pin57 (PD10) | BLE power enable |
| **PERIPH_VCC** | U6.VOUT, C18.+, CARD1.VDD, H1.1 | SD + SHT30 switched rail |
| **PERIPH_EN** | U6.ON, U1.pin58 (PD11) | Peripheral power enable |

---

## 8. New BOM Items

| Designator | Part | Package | LCSC | Mouser | Qty | Notes |
|------------|------|---------|------|--------|-----|-------|
| U2 | ATGM336H-5N31 GPS+BDS module | LCC-18 (10.1x9.7mm) | C90770 | N/A (LCSC only) | 1 | Replaces L76K breakout |
| C17 | 10uF X5R 25V (YAGEO CC0805KFX5R8BB106) | 0805 | C15850 | 603-CC0805KFX5R8BB10 | 1 | GPS_VCC decoupling (same part as C11) |
| L1 | 47nH multilayer inductor (Murata LQG15HS47NJ02D) | 0402 | C97998 | 81-LQG15HS47NJ02D | 1 | Bias tee |
| J1 | U.FL receptacle (HRS U-FL-R-SMT-1(80)) | SMD | C88374 | 798-U-FL-R-SMT-1-80 | 1 | GPS antenna connector |
| U4 | TPS22916DCKR load switch (or equivalent) | SOT-23-5 | TBD | 595-TPS22916DCKR | 1 | GPS_VCC load switch |
| U5 | TPS22916DCKR load switch (or equivalent) | SOT-23-5 | TBD | 595-TPS22916DCKR | 1 | BLE_VCC load switch |
| U6 | TPS22916DCKR load switch (or equivalent) | SOT-23-5 | TBD | 595-TPS22916DCKR | 1 | PERIPH_VCC load switch |
| C18 | 1uF X5R 16V | 0402 | C52923 | — | 1 | PERIPH_VCC output cap |
| C19 | 1uF X5R 16V | 0402 | C52923 | — | 1 | BLE_VCC output cap |
| Q6 | SI2301CDS P-FET | SOT-23 | C10487 | 781-SI2301CDS-E3 | 1 | Battery reverse polarity protection |
| R12 | 100k 1% | 0603 | C25803 | 603-RC0603FR-07100KL | 1 | Q6 gate-source tie |

**Note:** TPS22916 LCSC part number TBD — verify stock before ordering. Alternatives:
any single-channel load switch with GPIO enable, SOT-23-5, ≤1µA Iq, 3.3V, ≥200mA.
The discrete SI2301CDS + MMBT3904 circuit from V2 is a proven fallback.

**Removed from V2 BOM** (replaced by load switches):
Q2 (SI2301CDS), Q3 (MMBT3904), Q4 (SI2301CDS), Q5 (MMBT3904), R1/R2/R10/R11 (10k).
Net savings: 8 discrete components replaced by 3 ICs + 2 caps.

### Removed BOM Items

| Designator | Part | Notes |
|------------|------|-------|
| U2 (old) | Seeed L76K XIAO breakout | Off-board module, no longer used |

**Cost impact:** GPS drops from ~$7.00 (L76K breakout + antenna) to ~$5.50 (ATGM336H + bias tee + U.FL + separate antenna). Net savings ~$1.50/unit.

---

## 9. Reverse Polarity Protection (Q6)

Standard 18650 spring holders are symmetrical — cells can be inserted backwards. A reversed cell in the 1S4P pack would be force-discharged by the other three cells, damaging it and potentially the LDO. A P-channel MOSFET on the battery positive rail blocks reverse current with negligible voltage drop and zero quiescent current.

**Circuit:**
```
BATT+ ── Q6.Source (P-FET SI2301CDS)
              │
         Q6.Gate ── R12 (100k) ── GND
              │
         Q6.Drain ── 3V3_IN (to LDO input)
```

- **Correct polarity:** BATT+ is positive. Gate is pulled to Source (BATT+) by R12, but Drain sits at ~0V initially. VGS = Drain−Source ≈ −3.7V (through body diode), which is well below Vth (−1.2V max), so the FET turns fully on. RDSon = 110mΩ → drop = 110mΩ × I. At 72mA active: 8mV drop. Negligible.
- **Reversed polarity:** BATT+ is negative. Body diode is reverse-biased. VGS ≈ 0V (R12 holds gate at source). FET stays off. No current flows.

**Why SI2301CDS:** VDS(max) = −20V, ID(max) = −2.3A, RDSon = 110mΩ @ VGS = −4.5V. SOT-23 package, widely available at LCSC.

**Why 100k gate resistor (not direct tie):** R12 ensures a defined gate-source voltage during transients. Value is non-critical — 10k to 100k all work. 100k chosen to add zero measurable leakage (0.04µA at 4.2V).

**EasyEDA steps:**
1. Place Q6: SI2301CDS (LCSC C10487) — SOT-23, same as Q2/Q4
2. Place R12: 100k 0603 (LCSC C25803)
3. Q6.Source → BATT+ (battery positive terminal)
4. Q6.Gate → R12.1; R12.2 → GND (gate tied to source through R12)
5. Q6.Drain → LDO input (currently labeled 3V3_IN or VBAT on V2 schematic)
6. Move LDO input connection from BATT+ to Q6.Drain

**Connections:**

| From | To | Net |
|------|----|-----|
| Battery + terminal | Q6.Source | BATT+ |
| Q6.Gate | R12.1 | GND |
| R12.2 | Q6.Source | BATT+ |
| Q6.Drain | U3.IN (NCP170 LDO) | VBAT_PROT |
| Q6.Drain | C_IN.+ (LDO input cap) | VBAT_PROT |

---

## 10. PCB Layout Notes

- Place U2 (ATGM336H) near board edge — RF trace to J1 should be short
- J1 (U.FL) at board edge for antenna cable routing
- L1 between U2.pin14 and U2.pin11 — keep bias tee traces as short as possible
- C17 close to U2.pin8 (VCC)
- Ground pour under U2 and RF section — connect U2 ground pads (pins 1, 10, 12) to pour with short traces or vias
- No copper pour directly under J1 signal pad (RF clearance)
- Silkscreen: label J1 as "GPS ANT"

---

## 11. MCU Pin Mapping

| MCU Pin | GPIO | Function | V2 Target | V3 Target |
|---------|------|----------|-----------|-----------|
| 68 | PA9 | USART1_TX | L76K RX | ATGM336H RXD (pin 3) |
| 69 | PA10 | USART1_RX | L76K TX | ATGM336H TXD (pin 2) |
| 67 | PA8 | EXTI (PPS) | L76K PPS (bodge) | ATGM336H 1PPS (pin 4) |
| 61 | PD14 | GPIO out | L76K WAKEUP | ATGM336H ON/OFF (pin 5) |
| 62 | PD15 | GPIO out | L76K RESET | ATGM336H nRESET (pin 9) |
| 59 | PD12 | GPIO out | GPS_EN (Q3 base) | GPS_EN (U4.ON) — GPS only |
| 57 | PD10 | GPIO out | — (new) | BLE_EN (U5.ON) — BLE module only |
| 58 | PD11 | GPIO out | — (new) | PERIPH_EN (U6.ON) — SD + SHT30 |

NMEA 9600 baud, same UART, same PPS polarity. All three power rails use TPS22916
load switch ICs with direct GPIO enable — no NPN gate drivers needed.
