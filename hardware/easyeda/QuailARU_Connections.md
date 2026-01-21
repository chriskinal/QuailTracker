# QuailARU Pin-to-Pin Connections

**Updated design using L76K firmware power management (no P-FET)**

## Components

| Ref | Part | LCSC # | Description |
|-----|------|--------|-------------|
| U1 | ES7243E | C2929446 | 24-bit I2S ADC, QFN-20 |
| U2 | NCP170ASN300T2G | C603670 | 3.0V LDO Regulator, TSOP-5, 500nA Iq |
| U3 | SHT30-DIS-B2.5kS/TR | C78592 | Temp/Humidity Sensor, DFN-8, I2C |
| J4 | WAFER-MX1.25-8PZZ | C3029401 | GPS Connector, 1.25mm 8-pin Vertical |
| U4 | ESP32 DevKitC | - | 38-pin module (hand soldered) |
| J1 | TF-015 | C113206 | MicroSD Card Socket |
| J2 | S2B-PH-SM4-TB | C295747 | Battery Connector, JST PH 2-pin |
| J3 | B2B-XH-A | C158012 | Mic Connector, JST XH 2-pin |
| R1 | 2.2k 0603 | C4190 | Mic Bias Resistor |
| R2 | 1M 0603 | C22935 | VBAT Divider High |
| R3 | 1M 0603 | C22935 | VBAT Divider Low |
| C13 | 1uF 0805 | C28323 | VBAT ADC Buffer (Basic) |
| C1 | 10uF 0805 | C15850 | LDO Input Cap (Basic) |
| C2 | 10uF 0805 | C15850 | LDO Output Cap (Basic) |
| C3 | 100nF 0603 | C14663 | LDO Decoupling |
| C4 | 100nF 0603 | C14663 | ES7243E VDDD Decoupling |
| C5 | 100nF 0603 | C14663 | ES7243E VDDA Decoupling |
| C6 | 100nF 0603 | C14663 | J4 GPS VCC Decoupling |
| C7 | 100nF 0603 | C14663 | SD Card Decoupling |
| C8 | 100nF 0603 | C14663 | ES7243E REFQ Bypass |
| C12 | 100nF 0603 | C14663 | ES7243E REFP Bypass |
| C9 | 4.7uF 0805 | C1779 | ES7243E VDDA Bulk (Basic) |
| C10 | 4.7uF 0805 | C1779 | Bulk (Basic) |
| C11 | 10uF 0805 | C15850 | Mic DC Block (Basic) |
| C14 | 100nF 0603 | C14663 | SHT30 Decoupling |


---

## Net Connections

### Power Rails

**3V3 (3.3V Rail)**
| Component | Pin | Pin Name |
|-----------|-----|----------|
| U2 | 5 | VOUT |
| U1 | 1 | VDDP |
| U1 | 5 | VDDD |
| U1 | 12 | VDDA |
| J4 | 2 | VCC (Orange) |
| J4 | 3 | V_BCKP (White) |
| J4 | 6 | WAKE_UP (Yellow) |
| J4 | 8 | RESET_N (Red) |
| J1 | 4 | VDD |
| R1 | 1 | (bias to 3V3) |
| C2 | 1 | + |
| C3 | 1 | + |
| C4 | 1 | + |
| C5 | 1 | + |
| C6 | 1 | + |
| C7 | 1 | + |
| C9 | 1 | + |
| C10 | 1 | + |
| C14 | 1 | + |
| U3 | 5 | VDD |
| U4 | 3V3 | 3.3V |

**GND (Ground)**
| Component | Pin | Pin Name |
|-----------|-----|----------|
| U1 | 4 | GNDD |
| U1 | 13 | GNDA |
| U1 | 21 | EP (thermal pad) |
| U1 | 8 | AD1 (address) |
| U1 | 10 | AINLN |
| U1 | 15 | AINRN |
| U1 | 16 | AINRP |
| U1 | 17 | AD0 (address) |
| U2 | 2 | GND |
| J4 | 1 | GND (Brown) |
| J1 | 6 | VSS (GND) |
| J1 | 10 | Shield |
| J1 | 11 | Shield |
| J1 | 12 | Shield |
| J1 | 13 | Shield |
| J2 | 2 | GND |
| J3 | 2 | GND |
| C1 | 2 | - |
| C2 | 2 | - |
| C3 | 2 | - |
| C4 | 2 | - |
| C5 | 2 | - |
| C6 | 2 | - |
| C7 | 2 | - |
| C8 | 2 | - (REFQ bypass) |
| C12 | 2 | - (REFP bypass) |
| C9 | 2 | - |
| C10 | 2 | - |
| C14 | 2 | - |
| U3 | 2 | ADDR (sets 0x44) |
| U3 | 4 | VSS |
| R3 | 2 | VBAT divider low |
| C13 | 2 | VBAT ADC buffer |
| U4 | GND | Ground (multiple pins) |

**VBAT (Battery Input)**
| Component | Pin | Pin Name |
|-----------|-----|----------|
| J2 | 1 | + (battery positive) |
| C1 | 1 | + |
| U2 | 1 | VIN |
| U2 | 3 | EN (tie to VIN) |
| R2 | 1 | VBAT divider high |

⚠️ **J2 Battery Polarity Warning:** JST PH connectors have no universal polarity standard. Verify your battery's connector polarity matches J2 before connecting. The LCSC footprint pin 1 position determines which pad is VBAT+.

---

### I2S Audio (U4 to U1)

| Signal | U4 Pin | U1 Pin | Direction |
|--------|--------|--------|-----------|
| MCLK | GPIO0 | 20 (MCLK) | U4 -> U1 |
| SCLK | GPIO14 | 6 (SCLK) | U4 -> U1 |
| LRCK | GPIO15 | 7 (LRCK) | U4 -> U1 |
| SDOUT | GPIO32 | 3 (SDOUT) | U1 -> U4 |

### I2C Bus (U4 to U1, U3)

| Signal | U4 Pin | U1 Pin | U3 Pin | Direction |
|--------|--------|--------|--------|-----------|
| SDA | GPIO21 | 18 (CDATA) | 1 (SDA) | Bidirectional |
| SCL | GPIO22 | 19 (CCLK) | 6 (SCL) | U4 -> devices |

**I2C Addresses:**
- ES7243E (U1): Pin 17 (AD0), Pin 8 (AD1) -> GND = Address **0x10**
- SHT30 (U3): Pin 2 (ADDR) -> GND = Address **0x44**

**ES7243E Reference Pins:**
- Pin 11 (REFQ) -> C8 -> GND
- Pin 14 (REFP) -> C12 -> GND

---

### GPS Connector J4 (U4 to L76K Module)

**J4 Pinout (Pin 1 = Brown):**
| J4 Pin | Wire | Signal | U4 Pin | Direction |
|--------|------|--------|--------|-----------|
| 1 | Brown | GND | GND | Ground |
| 2 | Orange | VCC | 3V3 | Power |
| 3 | White | V_BCKP | 3V3 | Power |
| 4 | Blue | TX_GPS | GPIO16 (RX2) | J4 -> U4 |
| 5 | Green | RX_GPS | GPIO17 (TX2) | U4 -> J4 |
| 6 | Yellow | WAKE_UP | 3V3 | (hold high) |
| 7 | Black | PPS | GPIO4 | J4 -> U4 |
| 8 | Red | RESET_N | 3V3 | (hold high) |

**L76K Power Management:** Use PMTK commands via UART (no GPIO needed)
- Standby: `$PMTK161,0*28` (wake with any byte)
- Periodic: `$PMTK225,2,3000,12000,18000,72000*15` (example)

---

### SD Card SPI (U4 to J1)

| Signal | U4 Pin | J1 Pin | Direction |
|--------|--------|--------|-----------|
| SD_CS | GPIO5 | 2 (DAT3/CS) | U4 -> J1 |
| SD_MOSI | GPIO23 | 3 (CMD/DI) | U4 -> J1 |
| SD_CLK | GPIO18 | 5 (CLK) | U4 -> J1 |
| SD_MISO | GPIO19 | 7 (DAT0/DO) | J1 -> U4 |

**J1 Power:** Pin 4 (VDD) -> 3V3, Pin 6 (VSS) -> GND
**J1 Shield:** Pins 10, 11, 12, 13 -> GND
**J1 Card Detect:** Pin 9 (optional - not used in this design)

---

### Microphone Circuit

```
3V3 ---[R1 2.2k]---+--- J3 Pin 1 (Mic+)
                   |
                  [C11 10uF]
                   |
                   +--- U1 Pin 9 (AINLP)

J3 Pin 2 (Mic-) --- GND
U1 Pin 10 (AINLN) --- GND
U1 Pin 15 (AINRN) --- GND (unused, mono input)
U1 Pin 16 (AINRP) --- GND (unused, mono input)
```

| From | To | Notes |
|------|----|-------|
| 3V3 | R1 pin 1 | Bias supply |
| R1 pin 2 | J3 pin 1 | Mic bias point |
| R1 pin 2 | C11 pin 1 | DC block input |
| C11 pin 2 | U1 pin 9 (AINLP) | Audio to ADC |
| J3 pin 2 | GND | Mic ground |
| U1 pin 10 (AINLN) | GND | Left input - |
| U1 pin 15 (AINRN) | GND | Unused right - |
| U1 pin 16 (AINRP) | GND | Unused right + |

---

### LDO Regulator Circuit (NCP170 TSOP-5)

```
                    +--- U2 Pin 3 (EN)
                    |
VBAT (J2+) ---[C1 10uF]---+--- U2 Pin 1 (VIN)
                              |
                         [U2 NCP170]
                              |
U2 Pin 5 (VOUT) ---+---[C2 10uF]---+---[C3 100nF]--- 3V0 Rail
                   |               |
                  GND             GND

U2 Pin 2 (GND) --- GND
U2 Pin 4 (NC) --- No connection
```

**NCP170 Pinout (TSOP-5):**
| Pin | Function |
|-----|----------|
| 1 | VIN |
| 2 | GND |
| 3 | EN (active high, tie to VIN) |
| 4 | NC |
| 5 | VOUT (3.0V) |

---

### Battery Voltage Monitor

```
VBAT (J2+) ---[R2 1M]---+--- U4 GPIO35 (ADC1_CH7)
                        |
                       [C13 1uF]
                        |
                       [R3 1M]
                        |
                       GND
```

| From | To | Notes |
|------|----|-------|
| VBAT | R2 pin 1 | Battery positive |
| R2 pin 2 | R3 pin 1 | Divider midpoint |
| R2 pin 2 | C13 pin 1 | ADC buffer cap |
| R2 pin 2 | U4 GPIO35 | ADC input |
| C13 pin 2 | GND | Buffer cap ground |
| R3 pin 2 | GND | Divider ground |

**Voltage scaling:** VBAT ÷ 2
- 4.2V (full) → 2.1V ADC
- 3.7V (nominal) → 1.85V ADC
- 3.0V (empty) → 1.5V ADC

**Current draw:** ~2.1µA continuous (10x improvement over 100k divider)

**Note:** C13 (1µF) buffers the ADC input, allowing accurate readings despite high-impedance divider. Settling time ~500ms after voltage change.

---

### Temperature/Humidity Sensor (U3 - SHT30)

```
3V3 ---+--- U3 Pin 5 (VDD)
       |
      [C14 100nF]
       |
      GND

U4 GPIO21 (SDA) --- U3 Pin 1 (SDA)
U4 GPIO22 (SCL) --- U3 Pin 6 (SCL)

U3 Pin 2 (ADDR) --- GND (I2C address 0x44)
U3 Pin 3 (ALERT) --- NC (not used)
U3 Pin 4 (VSS) --- GND
U3 Pin 7 (nRESET) --- NC (internal pull-up)
U3 Pin 8 (R) --- NC (reserved)
```

**SHT30-DIS DFN-8 Pinout:**
| Pin | Function | Connection |
|-----|----------|------------|
| 1 | SDA | GPIO21 (I2C bus) |
| 2 | ADDR | GND (address 0x44) |
| 3 | ALERT | NC |
| 4 | VSS | GND |
| 5 | VDD | 3V3 |
| 6 | SCL | GPIO22 (I2C bus) |
| 7 | nRESET | NC (internal pull-up) |
| 8 | R | NC (reserved) |

**Specifications:**
- Temperature accuracy: ±0.2°C
- Humidity accuracy: ±2% RH
- Operating range: -40°C to +125°C
- Supply current: ~1.5µA average (single shot mode)
- I2C speed: Up to 1 MHz

---

## U4 (ESP32 DevKitC) Pin Summary

| U4 Pin | Function | Connected To |
|--------|----------|--------------|
| GPIO0 | I2S MCLK | U1 pin 20 |
| GPIO4 | PPS Input | J4 pin 7 |
| GPIO5 | SD CS | J1 pin 2 |
| GPIO14 | I2S SCLK | U1 pin 6 |
| GPIO15 | I2S LRCK | U1 pin 7 |
| GPIO16 | UART RX (GPS) | J4 pin 4 |
| GPIO17 | UART TX (GPS) | J4 pin 5 |
| GPIO18 | SPI CLK | J1 pin 5 |
| GPIO19 | SPI MISO | J1 pin 7 |
| GPIO21 | I2C SDA | U1 pin 18, U3 pin 1 |
| GPIO22 | I2C SCL | U1 pin 19, U3 pin 6 |
| GPIO23 | SPI MOSI | J1 pin 3 |
| GPIO32 | I2S SDOUT | U1 pin 3 |
| GPIO35 | VBAT ADC | R2/R3 divider midpoint |
| 3V3 | Power | 3V3 rail |
| GND | Ground | GND rail |

**GPIO2 is now FREE** (was used for GPS power control with P-FET)

---

## Removed Components (vs original design)

| Ref | Part | Reason |
|-----|------|--------|
| ~~Q1~~ | ~~SI2301 P-FET~~ | Using L76K firmware standby instead |
| ~~R2~~ | ~~10k pull-up~~ | No longer needed (was gate pull-up) |

---

## L76K Firmware Power Commands

Instead of hardware power gating, use these PMTK commands:

**Enter Standby Mode:**
```
$PMTK161,0*28<CR><LF>
```
Wake by sending any byte over UART.

**Periodic Mode (example - 3s on, 12s sleep):**
```
$PMTK225,2,3000,12000,18000,72000*XX
```
- Type 2 = Periodic standby
- Run time: 3000ms
- Sleep time: 12000ms
- Second run: 18000ms
- Second sleep: 72000ms

**Return to Full Power:**
```
$PMTK225,0*2B<CR><LF>
```
