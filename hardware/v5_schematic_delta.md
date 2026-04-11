# V5 Schematic Delta — ESP32-C3 Super Mini Replaces PB-03F BLE Module

Changes from V4:
1. Remove PB-03F BLE module and USART2 interface
2. Add ESP32-C3 Super Mini module on SPI2 (PB12-PB15)
3. ESP32 handles BLE beacon, WiFi AP, web UI, and STM32 OTA flashing
4. SPI2 pins chosen to match STM32U575 ROM bootloader for field recovery
5. Remove U5 load switch — ESP32 on always-on 3V3 (boot deadlock fix)
6. Switch ADF1 → MDF1 for stereo PDM: PE9 clock (unchanged), PD3 data (was PE10)
7. Replace SHT30 with BME280 (adds pressure for TDOA distance estimation)

---

## Deleted Items

| Item | Description | Reason |
|------|-------------|--------|
| COMM1 | PB-03F BLE module | Replaced by ESP32-C3 |
| U5 | TPS22919 load switch (was BLE_VCC / ESP_VCC) | ESP32 on always-on 3V3 — switched rail causes boot deadlock |
| C19 | 1uF output cap for U5 | U5 deleted |
| USART2 wiring | PA2 (TX) / PA3 (RX) to PB-03F | No longer needed — ESP32 uses SPI |

---

## New Module: ESP32-C3 Super Mini (ESPMOD1)

Castellated module, mounted as SMD component. Ceramic antenna overhangs board edge or sits over copper keep-out zone (2mm deep × module width, no copper either layer).

### ESP32 ↔ STM32 Wiring

| ESP32 Pin | ESP32 GPIO | Net | STM32 Pin | LQFP100 | Function |
|-----------|-----------|-----|-----------|---------|----------|
| GPIO0 | GPIO0 | **LASER_WAKE** | - | - | Phototransistor input (H3 header), deep sleep wake |
| GPIO2 | GPIO2 | **NRST** | NRST | 14 | Existing net — shared with C10, SW1 |
| GPIO3 | GPIO3 | **BOOT0** | PH3 | 94 | Existing net — shared with R6, SW2 |
| SCK | GPIO4 | **SPI2_SCK** | PB13 | 52 | SPI clock |
| MISO | GPIO5 | **SPI2_MISO** | PB14 | 53 | SPI data (STM32→ESP32) |
| MOSI | GPIO6 | **SPI2_MOSI** | PB15 | 54 | SPI data (ESP32→STM32) |
| SS | GPIO7 | **SPI2_NSS** | PB12 | 51 | SPI chip select |

### ESP32 Power

| ESP32 Pin | Net | Source |
|-----------|-----|--------|
| 5V / VIN | - | Not used (powered via 3V3) |
| 3V3 | **3V3** | Always-on 3.3V rail (direct from LDO) |
| GND | **GND** | Ground plane |

**Note:** The ESP32 must be on the always-on 3V3 rail, not a switched rail. When unpowered, the ESP32 GPIO2 (NRST) ESD protection diode clamps the STM32 NRST line low, preventing boot. Series resistors (tested 1k–10k) do not resolve this. The ESP32-C3 Super Mini has no exposed RST/EN pin, so there is no external hard-reset mechanism — the ESP32 stays powered whenever the battery is connected.

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
| **3V3** | Remove: COMM1.VCC (PB-03F). Add: ESPMOD1.3V3 | ESP32 powered directly from always-on 3V3 rail. |
| **GND** | Remove: COMM1.GND | Add: ESPMOD1.GND |

---

## New Nets

| Net | Description | Pins |
|-----|-------------|------|
| **SPI2_SCK** | SPI2 clock | U1.pin52 (PB13), ESPMOD1.GPIO4 |
| **SPI2_MISO** | SPI2 master-in slave-out | U1.pin53 (PB14), ESPMOD1.GPIO5 |
| **SPI2_MOSI** | SPI2 master-out slave-in | U1.pin54 (PB15), ESPMOD1.GPIO6 |
| **SPI2_NSS** | SPI2 chip select | U1.pin51 (PB12), ESPMOD1.GPIO7 |

## Existing Nets — New Connections

These are existing nets from V3/V4. Just add the ESP32 pin to the net label.

| Net | Add Pin | Existing Pins on Net |
|-----|---------|---------------------|
| **NRST** | ESPMOD1.GPIO2 | U1.pin14, C10, SW1. ESP32 normally hi-Z, drives low for OTA reset. |
| **BOOT0** | ESPMOD1.GPIO3 | U1.pin94 (PH3), R6 (10k pull-down), SW2. ESP32 normally hi-Z, drives high for OTA bootloader entry. |
| **3V3** | ESPMOD1.3V3 | Always-on rail. ESP32 must be on this rail (see power note above). |
| **GND** | ESPMOD1.GND | Existing ground plane. |

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
| 38 | PE9 | ADF1_CCK0 (AF3) | MDF1_CCK0 (AF6) — same pin, different peripheral |
| 39 | PE10 | ADF1_SDI0 (AF3) | Unassigned (available) — data moved to PD3 |
| 51 | PB12 | Unassigned | SPI2_NSS (ESP32 CS) |
| 52 | PB13 | Unassigned | SPI2_SCK (ESP32 clock) |
| 53 | PB14 | Unassigned | SPI2_MISO (ESP32 data in) |
| 54 | PB15 | Unassigned | SPI2_MOSI (ESP32 data out) |
| 57 | PD10 | BLE_VCC EN | Unassigned (load switch removed) |
| 84 | PD3 | Unassigned | MDF1_SDI0 (AF6) — stereo PDM data from both mics |

---

## Schematic Designator Changes

| Designator | V4 | V5 |
|------------|----|----|
| COMM1 | PB-03F BLE module | **Deleted** |
| ESPMOD1 | - | **New** — ESP32-C3 Super Mini module |
| U5 | TPS22916 (BLE_VCC switch) | **Deleted** — ESP32 on always-on 3V3 |
| C19 | 1uF (BLE_VCC output cap) | **Deleted** — U5 removed |

---

## Laser Wake Circuit (H3)

Visible-light phototransistor + feedback LED on a 4-pin header (H3) for flexible enclosure mounting. Allows the ESP32 to enter deep sleep (~5µA) and be woken by aiming a laser pointer at the sensor.

### Circuit

```
3V3 (H3.1) ─── Phototransistor collector (external, on header cable)
                Phototransistor emitter ─── H3.3 (GPIO0)

GPIO0 (H3.3) ──┬── R_LW 10K ── GND (on-board)
                └── R_LED 330R ── LED anode (H3.4)
                    LED cathode ── GND (external, on header cable)

H3.2 = GND
```

### Header Pinout (H3, 2.54mm, 4-pin)

| Pin | Net | Function |
|-----|-----|----------|
| 1 | 3V3 | Phototransistor power |
| 2 | GND | Ground |
| 3 | GPIO0 | Sensor signal (to ESP32 GPIO0) |
| 4 | LED_A | Feedback LED anode (through R_LED to GPIO0 net) |

### Notes

- Phototransistor (e.g., PT334-6C) and feedback LED are off-board, connected via header cable
- R_LW (10K pull-down) and R_LED (330R) are on the main PCB
- LED lights when laser hits sensor — provides visual feedback even during deep sleep
- ESP32 GPIO0 supports deep sleep wake (`esp_deep_sleep_enable_gpio_wakeup`)
- Mount sensor facing side/bottom of enclosure to reject ambient sunlight
- Frosted window/diffuser over sensor increases effective target area
- Firmware provides 3-minute boot grace period to prevent lockout

---

## Layout Notes

- **ESPMOD1 placement:** Board edge or near edge, antenna side facing outward or over keep-out zone. Castellated pads for SMD assembly.
- **SPI2 traces (PB12-PB15 to ESP32):** Keep reasonably short (<30mm). Match lengths not critical at 2-5 MHz SPI clock.
- **NRST/BOOT0 traces:** Can be longer — only used during OTA flash, not timing-critical.
- **ESP32 power:** Direct connection to 3V3 rail. Add 100nF decoupling cap close to ESP32 3V3 pin.
- **Antenna keep-out:** 2mm deep, full module width, no copper either layer. Verified under microscope on actual Super Mini module.
- **Solar charger (L2, M1, D1):** Keep away from ESP32 antenna area — switching noise can degrade WiFi/BLE performance.

---

## BOM Changes

| Action | Designator | Part | LCSC | Notes |
|--------|------------|------|------|-------|
| **Remove** | COMM1 | PB-03F BLE module | - | No longer used |
| **Remove** | U5 | TPS22919 load switch | - | ESP32 boot deadlock — must be on always-on 3V3 |
| **Remove** | C19 | 1uF output cap | - | U5 removed |
| **Add** | ESPMOD1 | ESP32-C3 Super Mini | - | Castellated module, hand-solder or custom JLCPCB assembly |
| **Add** | H3 | 4-pin header (laser wake) | - | 2.54mm pin header: 3V3, GND, GPIO0, LED anode |
| **Add** | R_LW | 10K pull-down resistor | - | GPIO0 pull-down (laser wake sensor) |
| **Add** | R_LED | 330R current-limiting resistor | - | Laser wake feedback LED |
