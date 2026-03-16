# V2 Schematic Delta — Debug & DFU Additions

Changes from V1 schematic to add SWO trace, UART debug, and BOOT0/RESET buttons.

---

## Summary of Changes

| Change | What | Why |
|--------|------|-----|
| Expand H2 | 1x4 → 1x7 header | Add SWO + UART to debug header |
| Add SW1 | RESET tactile button | Hardware reset without debugger |
| Add SW2 | BOOT0 tactile button | DFU mode entry without debugger |
| New net: SWO | PB3 (pin 89) → H2.5 | ITM trace output |
| New net: DBG_TX | PD8 (pin 55) → H2.6 | USART3 TX to debug header |
| New net: DBG_RX | PD9 (pin 56) → H2.7 | USART3 RX to debug header |

---

## 1. Expand Debug Header H2 (1x4 → 1x7)

**Old H2 pinout (V1):**

| H2 Pin | Signal | MCU Pin |
|--------|--------|---------|
| 1 | 3V3 | Power |
| 2 | GND | Ground |
| 3 | SWDIO | PA13 (pin 72) |
| 4 | SWCLK | PA14 (pin 76) |

**New H2 pinout (V2):**

| H2 Pin | Signal | MCU Pin | Net Port |
|--------|--------|---------|----------|
| 1 | 3V3 | Power | 3V3 |
| 2 | GND | Ground | GND |
| 3 | SWDIO | PA13 (pin 72) | SWDIO |
| 4 | SWCLK | PA14 (pin 76) | SWCLK |
| 5 | SWO | PB3 (pin 89) | SWO |
| 6 | DBG_TX | PD8 (pin 55) | DBG_TX |
| 7 | DBG_RX | PD9 (pin 56) | DBG_RX |

**EasyEDA steps:**
1. Delete old H2 (1x4 header)
2. Place new 1x7 Male Header 2.54mm (or cut-to-7 from 1x40 strip — same footprint pitch)
3. Add net port labels: existing SWDIO/SWCLK on pins 3-4, new SWO on pin 5, new DBG_TX on pin 6, new DBG_RX on pin 7
4. Place net port label **SWO** on U1 pin 89 (PB3)
5. Place net port label **DBG_TX** on U1 pin 55 (PD8)
6. Place net port label **DBG_RX** on U1 pin 56 (PD9)

---

## 2. Add RESET Button (SW1)

**Circuit:**
```
         SW1
NRST ────┤  ├──── GND
(pin 14)
```

No external pull-up resistor needed — STM32U575 has an internal pull-up on NRST.
C10 (100nF) already present on NRST for debounce/filtering.

**EasyEDA steps:**
1. Place tactile switch SW1 (3x6mm SMD, C318884)
2. Wire SW1 pin 1 to NRST node (same node as C10.1 and U1.pin14)
3. Wire SW1 pin 2 to GND net port
4. Place near board edge for easy access

**Connections:**

| SW1 Pin | Connects To |
|---------|-------------|
| 1 | U1.pin14 (NRST) — same node as C10.1 |
| 2 | GND |

---

## 3. Add BOOT0 Button (SW2)

**Circuit:**
```
3V3 ────┤  ├──── BOOT0 (PH3, pin 94)
         SW2       │
                  R6 (10k, existing)
                   │
                  GND
```

R6 (10k pull-down) already exists on BOOT0 — keeps BOOT0 low during normal boot.
SW2 overrides R6 by connecting BOOT0 to 3V3 when pressed.

**EasyEDA steps:**
1. Place tactile switch SW2 (3x6mm SMD, C318884)
2. Wire SW2 pin 1 to 3V3 net port
3. Wire SW2 pin 2 to BOOT0 node (same node as R6.1 and U1.pin94)
4. Place near SW1 for easy two-button operation

**Connections:**

| SW2 Pin | Connects To |
|---------|-------------|
| 1 | 3V3 |
| 2 | U1.pin94 (PH3/BOOT0) — same node as R6.1 |

---

## 4. New Net Port Labels

Add these three new net port labels to the schematic:

| Net Port | MCU Pin | LQFP100 Pin | AF | Description |
|----------|---------|-------------|-----|-------------|
| **SWO** | PB3 | 89 | AF0 | TRACESWO — Serial Wire Output |
| **DBG_TX** | PD8 | 55 | AF7 | USART3_TX — debug serial out |
| **DBG_RX** | PD9 | 56 | AF7 | USART3_RX — debug serial in |

PD8/PD9 were previously unnetted ("optional debug") — they now get proper net port labels.

---

## 5. New BOM Items

| Designator | Part | Package | LCSC | Qty | Notes |
|------------|------|---------|------|-----|-------|
| SW1 | Tactile switch 3x6mm | SMD | C318884 | 1 | RESET button |
| SW2 | Tactile switch 3x6mm | SMD | C318884 | 1 | BOOT0 / DFU button |

H2 changes from 1x4 to 1x7 header (same part, longer strip — cut from C124378 1x40 breakaway).

**Cost impact:** ~$0.10 (2x tactile switches). No change to extended parts fee.

---

## 6. PCB Layout Notes

- Place SW1 and SW2 near each other at a board edge for easy two-button access
- SW1 (RESET) on the left, SW2 (BOOT0) on the right (conventional ordering)
- Silkscreen labels: "RST" and "DFU" (or "BOOT0")
- H2 debug header at board edge, silkscreen pin 1 marker and signal names
- SWO trace (PB3 → H2.5) is a high-speed signal — keep trace short, avoid running parallel to noisy signals

---

## 7. DFU Entry Procedure

For reference, document on silkscreen or in user guide:

1. Hold **SW2** (BOOT0/DFU)
2. Tap **SW1** (RESET)
3. Release **SW2**
4. MCU is now in system bootloader
5. Flash via USB DFU (`dfu-util`) or UART (`stm32flash` on DBG_TX/DBG_RX)

---

## 8. Debug Access Summary

Three independent debug/programming paths, no Segger required:

| Method | Hardware Needed | Pins Used | Use Case |
|--------|----------------|-----------|----------|
| **SWD** | ST-Link V2 clone (~$3) | H2.1-4 (3V3, GND, SWDIO, SWCLK) | Full debug: breakpoints, memory, flash |
| **SWO** | ST-Link V2+ or J-Link | H2.5 (+ SWD pins) | ITM printf trace, timestamps, profiling |
| **UART** | Any USB-UART adapter (~$2) | H2.6-7 (DBG_TX, DBG_RX) + H2.2 (GND) | Serial console, logging, UART bootloader |
| **DFU** | USB cable only | SW1 + SW2 buttons | Emergency flash recovery, field updates |
