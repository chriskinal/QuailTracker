# V5 Schematic Delta — ESP32-C3 Super Mini Replaces PB-03F BLE Module

Changes from V4:
1. Remove PB-03F BLE module and USART2 interface
2. Add ESP32-C3 Super Mini module on SPI2 (PB12-PB15)
3. ESP32 handles BLE beacon, WiFi AP, web UI, and STM32 OTA flashing
4. SPI2 pins chosen to match STM32U575 ROM bootloader for field recovery
5. Rename BLE_VCC → ESP_VCC (same load switch, same pin PD10)

---

## Deleted Items

| Item | Description | Reason |
|------|-------------|--------|
| COMM1 | PB-03F BLE module | Replaced by ESP32-C3 |
| USART2 wiring | PA2 (TX) / PA3 (RX) to PB-03F | No longer needed — ESP32 uses SPI |

---

## New Module: ESP32-C3 Super Mini (ESPMOD1)

Castellated module, mounted as SMD component. Ceramic antenna overhangs board edge or sits over copper keep-out zone (2mm deep × module width, no copper either layer).

### ESP32 ↔ STM32 Wiring

| ESP32 Pin | ESP32 GPIO | Net | STM32 Pin | LQFP100 | Function |
|-----------|-----------|-----|-----------|---------|----------|
| SCK | GPIO4 | **SPI2_SCK** | PB13 | 52 | SPI clock |
| MISO | GPIO5 | **SPI2_MISO** | PB14 | 53 | SPI data (STM32→ESP32) |
| MOSI | GPIO6 | **SPI2_MOSI** | PB15 | 54 | SPI data (ESP32→STM32) |
| SS | GPIO7 | **SPI2_NSS** | PB12 | 51 | SPI chip select |
| GPIO2 | GPIO2 | **STM32_NRST** | NRST | 14 | STM32 reset (for OTA flash) |
| GPIO3 | GPIO3 | **STM32_BOOT0** | PH3 | 94 | STM32 boot mode (for OTA flash) |

### ESP32 Power

| ESP32 Pin | Net | Source |
|-----------|-----|--------|
| 5V / VIN | - | Not used (powered via 3V3) |
| 3V3 | **ESP_VCC** | From U5 (TPS22916 load switch on PD10) |
| GND | **GND** | Ground plane |

**Note:** The ESP32-C3 Super Mini has no exposed RST pin on the headers. Power cycling via PD10 (ESP_VCC load switch) is the only way to hard-reset the ESP32 externally.

### ESP32 Antenna Keep-Out

The ceramic antenna extends past the castellated pads. On the production PCB:
- 2mm × module width copper-free zone under the antenna area
- No copper on either layer (top or bottom)
- No vias in the keep-out zone
- No components within 2mm of the antenna

---

## Modified Nets

| Net | Change | Details |
|-----|--------|---------|
| **ESP_VCC** (was BLE_VCC) | Renamed | Same load switch U5 (TPS22916), same enable pin PD10. Now powers ESP32 instead of PB-03F. |
| **3V3** | Remove: COMM1.VCC | PB-03F no longer present. Add: nothing (ESP32 powered via ESP_VCC) |
| **GND** | Remove: COMM1.GND | Add: ESPMOD1.GND |

---

## New Nets

| Net | Description | Pins |
|-----|-------------|------|
| **SPI2_SCK** | SPI2 clock | U1.pin52 (PB13), ESPMOD1.GPIO4 |
| **SPI2_MISO** | SPI2 master-in slave-out | U1.pin53 (PB14), ESPMOD1.GPIO5 |
| **SPI2_MOSI** | SPI2 master-out slave-in | U1.pin54 (PB15), ESPMOD1.GPIO6 |
| **SPI2_NSS** | SPI2 chip select | U1.pin51 (PB12), ESPMOD1.GPIO7 |
| **STM32_NRST** | STM32 reset (shared with existing NRST net) | U1.pin14 (NRST), ESPMOD1.GPIO2 |
| **STM32_BOOT0** | STM32 boot mode select | U1.pin94 (PH3/BOOT0), ESPMOD1.GPIO3 |

**Note:** STM32_NRST connects to the existing NRST net (which already has C10 100nF decoupling and SW1 reset button). The ESP32 GPIO2 is normally high-impedance (input mode) and only drives low during OTA flash operations.

**Note:** STM32_BOOT0 connects to the existing BOOT0 net (which has R6 10k pull-down). ESP32 GPIO3 is normally high-impedance. During OTA flash, ESP32 drives it HIGH to enter bootloader, then releases to high-Z.

---

## Deleted Nets

| Net | Reason |
|-----|--------|
| **BLE_TX** (PA2 → PB-03F RX) | PB-03F removed |
| **BLE_RX** (PA3 → PB-03F TX) | PB-03F removed |

---

## Freed MCU Pins

These pins were used by V3/V4 for the PB-03F or old SPI2 routing and are now available:

| Pin | GPIO | Previously | Now |
|-----|------|-----------|-----|
| 25 | PA2 | USART2_TX (BLE) | Available |
| 26 | PA3 | USART2_RX (BLE) | Available |

---

## MCU Pin Changes Summary

| LQFP100 Pin | GPIO | V4 Function | V5 Function |
|-------------|------|-------------|-------------|
| 25 | PA2 | USART2_TX (BLE) | Unassigned (available) |
| 26 | PA3 | USART2_RX (BLE) | Unassigned (available) |
| 51 | PB12 | Unassigned | SPI2_NSS (ESP32 CS) |
| 52 | PB13 | Unassigned | SPI2_SCK (ESP32 clock) |
| 53 | PB14 | Unassigned | SPI2_MISO (ESP32 data in) |
| 54 | PB15 | Unassigned | SPI2_MOSI (ESP32 data out) |
| 57 | PD10 | BLE_VCC EN | ESP_VCC EN (renamed) |

---

## Schematic Designator Changes

| Designator | V4 | V5 |
|------------|----|----|
| COMM1 | PB-03F BLE module | **Deleted** |
| ESPMOD1 | - | **New** — ESP32-C3 Super Mini module |
| U5 | TPS22916 (BLE_VCC switch) | TPS22916 (ESP_VCC switch) — same part, renamed net |

---

## Layout Notes

- **ESPMOD1 placement:** Board edge or near edge, antenna side facing outward or over keep-out zone. Castellated pads for SMD assembly.
- **SPI2 traces (PB12-PB15 to ESP32):** Keep reasonably short (<30mm). Match lengths not critical at 2-5 MHz SPI clock.
- **NRST/BOOT0 traces:** Can be longer — only used during OTA flash, not timing-critical.
- **ESP_VCC load switch (U5):** Same placement as V4 BLE_VCC switch. Output cap C19 close to ESP32 3V3 input.
- **Antenna keep-out:** 2mm deep, full module width, no copper either layer. Verified under microscope on actual Super Mini module.
- **Solar charger (L2, M1, D1):** Keep away from ESP32 antenna area — switching noise can degrade WiFi/BLE performance.

---

## BOM Changes

| Action | Designator | Part | LCSC | Notes |
|--------|------------|------|------|-------|
| **Remove** | COMM1 | PB-03F BLE module | - | No longer used |
| **Add** | ESPMOD1 | ESP32-C3 Super Mini | - | Castellated module, hand-solder or custom JLCPCB assembly |

All other BOM items unchanged from V4. The TPS22916 load switch (U5) and its output cap (C19) are retained — only the net name changes from BLE_VCC to ESP_VCC.
