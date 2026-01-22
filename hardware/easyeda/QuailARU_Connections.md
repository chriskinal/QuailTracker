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
| R4 | 4.7k 0603 | C23162 | I2C SDA Pull-up |
| R5 | 4.7k 0603 | C23162 | I2C SCL Pull-up |
| C1 | 10uF 0805 | C15850 | LDO Input Cap (Basic) |
| C2 | 10uF 0805 | C15850 | LDO Output Cap (Basic) |
| C3 | 100nF 0603 | C14663 | LDO Decoupling |
| C4 | 100nF 0603 | C14663 | ES7243E VDDD Decoupling |
| C5 | 100nF 0603 | C14663 | ES7243E VDDA Decoupling |
| C6 | 100nF 0603 | C14663 | J4 GPS VCC Decoupling |
| C7 | 100nF 0603 | C14663 | SD Card Decoupling |
| C8 | 10uF 0805 | C15850 | ES7243E REFQ Bypass (Basic) |
| C9 | 10uF 0805 | C15850 | ES7243E VDDA Bulk (Basic) |
| C10 | 4.7uF 0805 | C1779 | Bulk (Basic) |
| C11 | 1uF 0805 | C28323 | Mic DC Block / AINLP decoupling (Basic) |
| C12 | 10uF 0805 | C15850 | ES7243E REFP Bypass (Basic) |
| C13 | 1uF 0805 | C28323 | VBAT ADC Buffer (Basic) |
| C14 | 100nF 0603 | C14663 | SHT30 Decoupling |
| C15 | 1uF 0805 | C28323 | ES7243E AINLN AC-coupling (Basic) |
| C16 | 1uF 0805 | C28323 | ES7243E AINRN AC-coupling (Basic) |
| C17 | 1uF 0805 | C28323 | ES7243E AINRP AC-coupling (Basic) |


---

## Net Connections

### Power Rails

**3V3 (3.3V Rail)**
| Ref | Part | LCSC # | Pin | Pin Name |
|-----|------|--------|-----|----------|
| U2 | NCP170 LDO | C603670 | 5 | VOUT |
| U1 | ES7243E ADC | C2929446 | 1 | VDDP |
| U1 | ES7243E ADC | C2929446 | 5 | VDDD |
| U1 | ES7243E ADC | C2929446 | 12 | VDDA |
| J4 | GPS Connector | C3029401 | 2 | VCC (Orange) |
| J4 | GPS Connector | C3029401 | 3 | V_BCKP (White) |
| J4 | GPS Connector | C3029401 | 6 | WAKE_UP (Yellow) |
| J4 | GPS Connector | C3029401 | 8 | RESET_N (Red) |
| J1 | MicroSD Socket | C113206 | 4 | VDD |
| R1 | 2.2k Mic Bias | C4190 | 1 | (bias to 3V3) |
| C2 | 10uF LDO Out | C15850 | 1 | + |
| C3 | 100nF LDO Decoup | C14663 | 1 | + |
| C4 | 100nF VDDD | C14663 | 1 | + |
| C5 | 100nF VDDA | C14663 | 1 | + |
| C6 | 100nF GPS | C14663 | 1 | + |
| C7 | 100nF SD | C14663 | 1 | + |
| C9 | 10uF VDDA Bulk | C15850 | 1 | + |
| C10 | 4.7uF Bulk | C1779 | 1 | + |
| C14 | 100nF SHT30 | C14663 | 1 | + |
| U3 | SHT30 Sensor | C78592 | 5 | VDD |
| U4 | ESP32 DevKitC | - | 3V3 | 3.3V |
| R4 | 4.7k I2C Pull-up | C23162 | 1 | SDA pull-up |
| R5 | 4.7k I2C Pull-up | C23162 | 1 | SCL pull-up |

**GND (Ground)**
| Ref | Part | LCSC # | Pin | Pin Name |
|-----|------|--------|-----|----------|
| U1 | ES7243E ADC | C2929446 | 4 | GNDD |
| U1 | ES7243E ADC | C2929446 | 13 | GNDA |
| U1 | ES7243E ADC | C2929446 | 21 | EP (thermal pad) |
| U1 | ES7243E ADC | C2929446 | 8 | AD1 (address) |
| U1 | ES7243E ADC | C2929446 | 17 | AD0 (address) |
| U2 | NCP170 LDO | C603670 | 2 | GND |
| J4 | GPS Connector | C3029401 | 1 | GND (Brown) |
| J1 | MicroSD Socket | C113206 | 6 | VSS (GND) |
| J1 | MicroSD Socket | C113206 | 10 | Shield |
| J1 | MicroSD Socket | C113206 | 11 | Shield |
| J1 | MicroSD Socket | C113206 | 12 | Shield |
| J1 | MicroSD Socket | C113206 | 13 | Shield |
| J2 | Battery Conn | C295747 | 2 | GND |
| J3 | Mic Connector | C158012 | 2 | GND |
| C1 | 10uF LDO In | C15850 | 2 | - |
| C2 | 10uF LDO Out | C15850 | 2 | - |
| C3 | 100nF LDO Decoup | C14663 | 2 | - |
| C4 | 100nF VDDD | C14663 | 2 | - |
| C5 | 100nF VDDA | C14663 | 2 | - |
| C6 | 100nF GPS | C14663 | 2 | - |
| C7 | 100nF SD | C14663 | 2 | - |
| C8 | 10uF REFQ | C15850 | 2 | - (REFQ bypass) |
| C9 | 10uF VDDA Bulk | C15850 | 2 | - |
| C10 | 4.7uF Bulk | C1779 | 2 | - |
| C12 | 10uF REFP | C15850 | 2 | - (REFP bypass) |
| C14 | 100nF SHT30 | C14663 | 2 | - |
| C15 | 1uF AINLN | C28323 | 2 | - (AINLN AC-couple) |
| C16 | 1uF AINRN | C28323 | 2 | - (AINRN AC-couple) |
| C17 | 1uF AINRP | C28323 | 2 | - (AINRP AC-couple) |
| U3 | SHT30 Sensor | C78592 | 2 | ADDR (sets 0x44) |
| U3 | SHT30 Sensor | C78592 | 4 | VSS |
| R3 | 1M VBAT Low | C22935 | 2 | VBAT divider low |
| C13 | 1uF VBAT ADC | C28323 | 2 | VBAT ADC buffer |
| U4 | ESP32 DevKitC | - | GND | Ground (multiple pins) |

**VBAT (Battery Input)**
| Ref | Part | LCSC # | Pin | Pin Name |
|-----|------|--------|-----|----------|
| J2 | Battery Conn | C295747 | 1 | + (battery positive) |
| C1 | 10uF LDO In | C15850 | 1 | + |
| U2 | NCP170 LDO | C603670 | 1 | VIN |
| U2 | NCP170 LDO | C603670 | 3 | EN (tie to VIN) |
| R2 | 1M VBAT High | C22935 | 1 | VBAT divider high |

⚠️ **J2 Battery Polarity Warning:** JST PH connectors have no universal polarity standard. Verify your battery's connector polarity matches J2 before connecting. The LCSC footprint pin 1 position determines which pad is VBAT+.

---

### I2S Audio (U4 ESP32 to U1 ES7243E)

| Signal | U4 (ESP32 DevKitC) | U1 (ES7243E, C2929446) | Direction |
|--------|---------------------|------------------------|-----------|
| MCLK | GPIO0 | Pin 20 (MCLK) | U4 -> U1 |
| SCLK | GPIO14 | Pin 6 (SCLK) | U4 -> U1 |
| LRCK | GPIO15 | Pin 7 (LRCK) | U4 -> U1 |
| SDOUT | GPIO32 | Pin 3 (SDOUT) | U1 -> U4 |

### I2C Bus (U4 ESP32 to U1 ES7243E, U3 SHT30)

| Signal | U4 (ESP32 DevKitC) | U1 (ES7243E, C2929446) | U3 (SHT30, C78592) | Direction |
|--------|---------------------|------------------------|---------------------|-----------|
| SDA | GPIO21 | Pin 18 (CDATA) | Pin 1 (SDA) | Bidirectional |
| SCL | GPIO22 | Pin 19 (CCLK) | Pin 6 (SCL) | U4 -> devices |

**I2C Pull-up Resistors (required):**
| Ref | Part | LCSC # | Connection |
|-----|------|--------|------------|
| R4 | 4.7k 0603 | C23162 | 3V3 → SDA line (GPIO21) |
| R5 | 4.7k 0603 | C23162 | 3V3 → SCL line (GPIO22) |

**I2C Addresses:**
- U1 ES7243E (C2929446): Pin 17 (AD0), Pin 8 (AD1) -> GND = Address **0x10**
- U3 SHT30 (C78592): Pin 2 (ADDR) -> GND = Address **0x44**

**ES7243E Reference Pins:**
| Ref | Part | LCSC # | U1 Pin | Connection |
|-----|------|--------|--------|------------|
| C8 | 10uF 0805 | C15850 | Pin 11 (REFQ) | -> AGND |
| C12 | 10uF 0805 | C15850 | Pin 14 (REFP) | -> AGND |

**ES7243E Analog Input AC-Coupling (CRITICAL):**
| Ref | Part | LCSC # | U1 Pin | Connection |
|-----|------|--------|--------|------------|
| C15 | 1uF 0805 | C28323 | Pin 10 (AINLN) | -> AGND |
| C16 | 1uF 0805 | C28323 | Pin 15 (AINRN) | -> AGND |
| C17 | 1uF 0805 | C28323 | Pin 16 (AINRP) | -> AGND |

---

### GPS Connector J4 (U4 ESP32 to L76K Module)

**J4 (WAFER-MX1.25-8PZZ, C3029401) Pinout - Pin 1 = Brown:**
| J4 Pin | Wire | Signal | U4 (ESP32 DevKitC) | Direction |
|--------|------|--------|---------------------|-----------|
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

### SD Card SPI (U4 ESP32 to J1 MicroSD)

| Signal | U4 (ESP32 DevKitC) | J1 (TF-015, C113206) | Direction |
|--------|---------------------|----------------------|-----------|
| SD_CS | GPIO5 | Pin 2 (DAT3/CS) | U4 -> J1 |
| SD_MOSI | GPIO23 | Pin 3 (CMD/DI) | U4 -> J1 |
| SD_CLK | GPIO18 | Pin 5 (CLK) | U4 -> J1 |
| SD_MISO | GPIO19 | Pin 7 (DAT0/DO) | J1 -> U4 |

**J1 (TF-015, C113206) Power:** Pin 4 (VDD) -> 3V3, Pin 6 (VSS) -> GND
**J1 Shield:** Pins 10, 11, 12, 13 -> GND
**J1 Card Detect:** Pin 9 (optional - not used in this design)

---

### Microphone Circuit

**⚠️ CRITICAL: ES7243E Analog Input Requirements (from datasheet reference design)**

The ES7243E differential inputs MUST be AC-coupled to ground, NOT directly grounded.
Direct grounding disrupts the internal bias circuitry and causes signal degradation.

```
3V3 ---[R1 2.2k]---+--- J3 Pin 1 (Mic+)
                   |
                  [C11 1uF]
                   |
                   +--- U1 Pin 9 (AINLP)

J3 Pin 2 (Mic-) --- GND

U1 Pin 10 (AINLN) ---[C15 1uF]--- AGND   ← AC-coupled, NOT direct!
U1 Pin 15 (AINRN) ---[C16 1uF]--- AGND   ← AC-coupled, NOT direct!
U1 Pin 16 (AINRP) ---[C17 1uF]--- AGND   ← AC-coupled, NOT direct!
```

| From | Part | LCSC # | To | Notes |
|------|------|--------|----|-------|
| 3V3 | - | - | R1 pin 1 | Bias supply |
| R1 pin 2 | 2.2k 0603 | C4190 | J3 pin 1 | Mic bias point |
| R1 pin 2 | 2.2k 0603 | C4190 | C11 pin 1 | DC block input |
| C11 pin 2 | 1uF 0805 | C28323 | U1 pin 9 (AINLP) | Audio to ADC |
| J3 pin 2 | Mic Conn | C158012 | GND | Mic ground |
| U1 pin 10 | ES7243E | C2929446 | C15 (1µF) -> AGND | **AC-couple** |
| U1 pin 15 | ES7243E | C2929446 | C16 (1µF) -> AGND | **AC-couple** |
| U1 pin 16 | ES7243E | C2929446 | C17 (1µF) -> AGND | **AC-couple** |

**Why AC-coupling is required:**
- The ES7243E provides internal bias (~1.45V from REFQ) for the differential inputs
- Direct grounding fights the internal bias, pulling AINLP down to ~0.7V instead of mid-rail
- This causes signal attenuation, requiring maximum PGA gain and causing noise
- With proper AC-coupling, no external bias resistors are needed on AINLP

---

### LDO Regulator Circuit (U2 NCP170, C603670, TSOP-5)

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

**U2 NCP170 (C603670) Pinout (TSOP-5):**
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

| From | Part | LCSC # | To | Notes |
|------|------|--------|----|-------|
| VBAT | - | - | R2 pin 1 | Battery positive |
| R2 pin 2 | 1M 0603 | C22935 | R3 pin 1 | Divider midpoint |
| R2 pin 2 | 1M 0603 | C22935 | C13 pin 1 | ADC buffer cap |
| R2 pin 2 | 1M 0603 | C22935 | U4 GPIO35 | ADC input |
| C13 pin 2 | 1uF 0805 | C28323 | GND | Buffer cap ground |
| R3 pin 2 | 1M 0603 | C22935 | GND | Divider ground |

**Voltage scaling:** VBAT ÷ 2
- 4.2V (full) → 2.1V ADC
- 3.7V (nominal) → 1.85V ADC
- 3.0V (empty) → 1.5V ADC

**Current draw:** ~2.1µA continuous (10x improvement over 100k divider)

**Note:** C13 (1µF) buffers the ADC input, allowing accurate readings despite high-impedance divider. Settling time ~500ms after voltage change.

---

### Temperature/Humidity Sensor (U3 SHT30, C78592)

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

**U3 SHT30-DIS (C78592) DFN-8 Pinout:**
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
| GPIO0 | I2S MCLK | U1 (ES7243E, C2929446) pin 20 |
| GPIO4 | PPS Input | J4 (GPS Conn, C3029401) pin 7 |
| GPIO5 | SD CS | J1 (MicroSD, C113206) pin 2 |
| GPIO14 | I2S SCLK | U1 (ES7243E, C2929446) pin 6 |
| GPIO15 | I2S LRCK | U1 (ES7243E, C2929446) pin 7 |
| GPIO16 | UART RX (GPS) | J4 (GPS Conn, C3029401) pin 4 |
| GPIO17 | UART TX (GPS) | J4 (GPS Conn, C3029401) pin 5 |
| GPIO18 | SPI CLK | J1 (MicroSD, C113206) pin 5 |
| GPIO19 | SPI MISO | J1 (MicroSD, C113206) pin 7 |
| GPIO21 | I2C SDA | U1 (ES7243E) pin 18, U3 (SHT30) pin 1 |
| GPIO22 | I2C SCL | U1 (ES7243E) pin 19, U3 (SHT30) pin 6 |
| GPIO23 | SPI MOSI | J1 (MicroSD, C113206) pin 3 |
| GPIO32 | I2S SDOUT | U1 (ES7243E, C2929446) pin 3 |
| GPIO35 | VBAT ADC | R2/R3 (1M, C22935) divider midpoint |
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
