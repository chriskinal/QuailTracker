# QuailARU PDM Version - Pin-to-Pin Connections

**Simplified design using Infineon IM72D128/IM73D122 digital PDM microphone**

This version eliminates the ES7243E ADC and analog mic circuit, connecting the digital MEMS mic directly to the ESP32.

## Components

| Ref | Part | LCSC # | Description |
|-----|------|--------|-------------|
| C1 | 10uF 0805 | C15850 | LDO Input Cap (Basic) |
| C2 | 10uF 0805 | C15850 | LDO Output Cap (Basic) |
| C3 | 100nF 0603 | C14663 | LDO Decoupling |
| C6 | 100nF 0603 | C14663 | J4 GPS VCC Decoupling |
| C7 | 100nF 0603 | C14663 | SD Card Decoupling |
| C10 | 4.7uF 0805 | C1779 | Bulk (Basic) |
| C13 | 1uF 0805 | C28323 | VBAT ADC Buffer (Basic) |
| C20 | 100nF 0603 | - | PDM Mic VDD Decoupling (at mic end of cable) |
| J1 | TF-015 | C113206 | MicroSD Card Socket |
| J2 | S2B-PH-SM4-TB | C295747 | Battery Connector, JST PH 2-pin |
| J3 | TYPE-C-31-M-12 | C165948 | USB-C Receptacle for PDM Mic Cable |
| J4 | WAFER-MX1.25-8PZZ | C3029401 | GPS Connector, 1.25mm 8-pin Vertical |
| J5 | 4-pin header | - | SHT30 Module Connector (optional) |
| MIC1 | IM72D128 or IM73D122 | - | Infineon PDM Mic (on USB-C cable end) |
| R2 | 1M 0603 | C22935 | VBAT Divider High |
| R3 | 1M 0603 | C22935 | VBAT Divider Low |
| R4 | 4.7k 0603 | C23162 | I2C SDA Pull-up (only if J5 SHT30 used) |
| R5 | 4.7k 0603 | C23162 | I2C SCL Pull-up (only if J5 SHT30 used) |
| R6 | 4.7k 0603 | C23162 | SD Card Detect Pull-up |
| R7 | 10k 0603 | C25804 | GPS P-FET Gate Pull-up (Basic) |
| R8 | 10k 0603 | C25804 | Q2 Base Resistor (Basic) |
| R9 | 10k 0603 | C25804 | Q3 Base Resistor (Basic) |
| R10 | 100R 0603 | C22775 | PDM DATA Termination (optional) |
| Q1 | SI2301CDS | C10487 | GPS VCC Power Switch (P-FET, SOT-23) (Basic) |
| Q2 | MMBT3904 | C20526 | GPS PWR_EN Control (NPN, SOT-23) (Basic) |
| Q3 | MMBT3904 | C20526 | GPS WAKEUP Control (NPN, SOT-23) (Basic) |
| U2 | NCP170ASN330T2G | C510641 | 3.3V LDO Regulator, TSOP-5, 500nA Iq |
| U3 | NODEMCU-32SLUA | - | ESP32 38-pin module (hand soldered) |

**Changes vs ES7243E design:**
- **Removed:** U1 (ES7243E), R1, C4, C5, C8, C9, C11, C12, C15, C16, C17
- **Changed:** J3 from JST XH mic connector to USB-C receptacle for PDM mic cable
- **Added:** MIC1 + C20 (at cable end, not on PCB)

---

## Net Connections

### Power Rails

**3V3 (3.3V Rail)**
| Ref | Part | LCSC # | Pin | Pin Name |
|-----|------|--------|-----|----------|
| U2 | NCP170 LDO | C510641 | 5 | VOUT |
| J3 | USB-C Mic Conn | C165948 | A5 | CC1 (Mic VDD via cable) |
| J4 | GPS Connector | C3029401 | 2 | VCC (switched via Q1) |
| J4 | GPS Connector | C3029401 | 3 | V_BCKP (always on) |
| J1 | MicroSD Socket | C113206 | 4 | VDD |
| C2 | 10uF LDO Out | C15850 | 1 | + |
| C3 | 100nF LDO Decoup | C14663 | 1 | + |
| C6 | 100nF GPS | C14663 | 1 | + |
| C7 | 100nF SD | C14663 | 1 | + |
| C10 | 4.7uF Bulk | C1779 | 1 | + |
| J5 | SHT30 Module Conn | - | 2 | VCC (optional) |
| U3 | NODEMCU-32SLUA | - | 3V3 | 3.3V |
| R4 | 4.7k I2C Pull-up | C23162 | 1 | SDA pull-up (optional) |
| R5 | 4.7k I2C Pull-up | C23162 | 1 | SCL pull-up (optional) |
| R6 | 4.7k SD Detect Pull-up | C23162 | 1 | SD card detect pull-up |
| R7 | 10k P-FET Pull-up | C25804 | 1 | GPS power gate pull-up |

**GND (Ground)**
| Ref | Part | LCSC # | Pin | Pin Name |
|-----|------|--------|-----|----------|
| J3 | USB-C Mic Conn | C165948 | A1/B12 | GND (Mic GND via cable) |
| J3 | USB-C Mic Conn | C165948 | Shell | Shield (cable shield) |
| J4 | GPS Connector | C3029401 | 1 | GND |
| U2 | NCP170 LDO | C510641 | 2 | GND |
| J1 | MicroSD Socket | C113206 | 6 | VSS (GND) |
| J1 | MicroSD Socket | C113206 | 10-13 | Shield |
| J2 | Battery Conn | C295747 | 2 | GND |
| C1 | 10uF LDO In | C15850 | 2 | - |
| C2 | 10uF LDO Out | C15850 | 2 | - |
| C3 | 100nF LDO Decoup | C14663 | 2 | - |
| C6 | 100nF GPS | C14663 | 2 | - |
| C7 | 100nF SD | C14663 | 2 | - |
| C10 | 4.7uF Bulk | C1779 | 2 | - |
| C13 | 1uF VBAT ADC | C28323 | 2 | - |
| J5 | SHT30 Module Conn | - | 1 | GND (optional) |
| R3 | 1M VBAT Low | C22935 | 2 | VBAT divider low |
| U3 | NODEMCU-32SLUA | - | GND | Ground (multiple pins) |
| Q2 | MMBT3904 | C20526 | 2 | Emitter |
| Q3 | MMBT3904 | C20526 | 2 | Emitter |

**VBAT (Battery Input)**
| Ref | Part | LCSC # | Pin | Pin Name |
|-----|------|--------|-----|----------|
| J2 | Battery Conn | C295747 | 1 | + (battery positive) |
| C1 | 10uF LDO In | C15850 | 1 | + |
| U2 | NCP170 LDO | C510641 | 1 | VIN |
| U2 | NCP170 LDO | C510641 | 3 | EN (tie to VIN) |
| R2 | 1M VBAT High | C22935 | 1 | VBAT divider high |

---

### PDM Microphone via USB-C Cable

The PDM mic connects via a USB-C to USB-C cable (5-7cm). Cut one end off, solder mic to wires.

**USB-C Cable Wiring:**

```
PCB (J3 USB-C)                    Cable (cut end)              MIC1 (IM72D128/IM73D122)
══════════════                    ══════════════               ═════════════════════════

J3 CC1  ─────────────────────────  (wire) ──────┬─────────────  VDD (pin 2)
                                                │
                                               ═╧═ C20 100nF (solder at mic)
                                                │
J3 GND  ─────────────────────────  (wire) ──────┴─────────────  GND (pin 5)
                                                │
                                                └─────────────  SELECT (pin 4) = Left ch
J3 D+   ─────────────────────────  (wire) ────────────────────  CLOCK (pin 3)

J3 D-   ──────[R10 100Ω]─────────  (wire) ────────────────────  DATA (pin 1)

J3 Shield ─── GND (PCB side only, do not connect at mic end)
```

**J3 USB-C Receptacle Pinout (TYPE-C-31-M-12, C165948):**
| J3 Pin | USB-C Signal | QuailARU Function | Connection |
|--------|--------------|-------------------|------------|
| A1/B12 | GND | Ground | GND rail |
| A4/B9 | VBUS | **Not used** | Leave NC (avoid 5V devices) |
| A5 | CC1 | Mic VDD (3.3V) | 3V3 rail |
| A6 | D+ | PDM CLK | GPIO14 |
| A7 | D- | PDM DATA | GPIO32 (via R10) |
| Shell | Shield | Cable shield | GND |

**Why CC1 for power (not VBUS):**
- VBUS expects 5V from USB hosts - wrong voltage
- Using CC1 prevents real USB devices from working if accidentally connected
- CC1 is a low-current pin - perfect for mic's ~1mA draw

**ESP32 to J3 Connections:**
| Signal | U3 (ESP32) | J3 (USB-C) | Direction |
|--------|------------|------------|-----------|
| PDM_CLK | GPIO14 | D+ (A6) | U3 -> MIC1 |
| PDM_DATA | GPIO32 | D- (A7) | MIC1 -> U3 |
| MIC_VDD | 3V3 | CC1 (A5) | Power to mic |
| GND | GND | GND (A1/B12) | Ground |

**Components at Mic End (solder to cable):**
| Ref | Part | Notes |
|-----|------|-------|
| MIC1 | IM72D128/IM73D122 | Solder to cable wires |
| C20 | 100nF 0603 | Between VDD and GND, close to mic |

**Optional DATA termination (on PCB):**
| Ref | Part | LCSC # | Connection |
|-----|------|--------|------------|
| R10 | 100Ω 0603 | C22775 | J3 D- (A7) → U3 GPIO32 |

---

### GPS Connector J4 (unchanged from ES7243E design)

**J4 (WAFER-MX1.25-8PZZ, C3029401) Pinout:**
| J4 Pin | Signal | Connection | Direction | Wire Color |
|--------|--------|------------|-----------|------------|
| 1 | GND | GND | Ground | Brown |
| 2 | VCC | Q1 drain (switched 3V3) | Power | Orange |
| 3 | V_BCKP | 3V3 (always on) | Backup power | White |
| 4 | TX_GPS | GPIO16 (RX2) | J4 -> U3 | Blue |
| 5 | RX_GPS | GPIO17 (TX2) | U3 -> J4 | Green |
| 6 | WAKEUP | Q3 collector (active low) | Standby ctrl | Yellow |
| 7 | PPS | GPIO4 | J4 -> U3 | Black |
| 8 | RESET_N | NC | - | Red |

**GPS Power Management Circuit (unchanged):**
```
                                    VCC Power Switch
3V3 ───┬────────────────────────────────────────────────► J4 Pin 3 (V_BCKP)
       │
       ├──[R7 10k]──┬── Q1 Gate
       │            │      │
       │      Q2 collector │  Q1 (SI2301CDS P-FET)
       │            │      │
       │         [Q2]      Source ── 3V3
       │      MMBT3904     Drain ──┬──[C6]──► J4 Pin 2 (VCC)
       │          │                │
       │       [R8 10k]           GND
       │          │
       │     GPIO25 (GPS_PWR_EN)

                                    WAKEUP Control
       │         [Q3]
       │      MMBT3904 ── collector ───────────► J4 Pin 6 (WAKEUP)
       │          │
       │       [R9 10k]
       │          │
       └──── GPIO26 (GPS_WAKEUP)
```

---

### SD Card SPI (unchanged)

| Signal | U3 (NODEMCU-32SLUA) | J1 (TF-015, C113206) | Direction |
|--------|---------------------|----------------------|-----------|
| SD_CS | GPIO5 | Pin 2 (DAT3/CS) | U3 -> J1 |
| SD_MOSI | GPIO23 | Pin 3 (CMD/DI) | U3 -> J1 |
| SD_CLK | GPIO18 | Pin 5 (CLK) | U3 -> J1 |
| SD_MISO | GPIO19 | Pin 7 (DAT0/DO) | J1 -> U3 |
| SD_DET | GPIO34 | Pin 9 (CD) | J1 -> U3 |

---

### LDO Regulator Circuit (unchanged)

```
VBAT (J2+) ---[C1 10uF]---+--- U2 Pin 1 (VIN)
                          +--- U2 Pin 3 (EN)
                              |
                         [U2 NCP170]
                              |
U2 Pin 5 (VOUT) ---+---[C2 10uF]---+---[C3 100nF]--- 3V3 Rail
                   |               |
                  GND             GND

U2 Pin 2 (GND) --- GND
U2 Pin 4 (NC) --- No connection
```

---

### Battery Voltage Monitor (unchanged)

```
VBAT (J2+) ---[R2 1M]---+--- U3 GPIO35 (ADC1_CH7)
                        |
                       [C13 1uF]
                        |
                       [R3 1M]
                        |
                       GND
```

---

### Temperature/Humidity Sensor J5 (optional, unchanged)

```
J5 (4-pin header to external SHT30 module):

  Pin 1 (GND) --- GND
  Pin 2 (VCC) --- 3V3
  Pin 3 (SDA) --- U3 GPIO21 (I2C SDA) --- R4 4.7k to 3V3
  Pin 4 (SCL) --- U3 GPIO22 (I2C SCL) --- R5 4.7k to 3V3
```

Note: If SHT30 not used, R4 and R5 can be omitted.

---

## U3 (NODEMCU-32SLUA) Pin Summary - PDM Version

| U3 Pin | Function | Connected To |
|--------|----------|--------------|
| GPIO0 | **Not used** | (was I2S MCLK for ES7243E) |
| GPIO4 | PPS Input | J4 (GPS Conn) pin 7 |
| GPIO5 | SD CS | J1 (MicroSD) pin 2 |
| GPIO14 | **PDM CLK** | J3 (USB-C) D+ → MIC1 CLOCK |
| GPIO15 | **Not used** | (was I2S LRCK for ES7243E) |
| GPIO16 | UART RX (GPS) | J4 (GPS Conn) pin 4 (TX_GPS) |
| GPIO17 | UART TX (GPS) | J4 (GPS Conn) pin 5 (RX_GPS) |
| GPIO18 | SPI CLK | J1 (MicroSD) pin 5 |
| GPIO19 | SPI MISO | J1 (MicroSD) pin 7 |
| GPIO21 | I2C SDA | J5 (SHT30 Module) pin 3 (optional) |
| GPIO22 | I2C SCL | J5 (SHT30 Module) pin 4 (optional) |
| GPIO23 | SPI MOSI | J1 (MicroSD) pin 3 |
| GPIO25 | GPS_PWR_EN | Q2 base (controls Q1 P-FET) |
| GPIO26 | GPS_WAKEUP | Q3 base (controls J4 pin 6) |
| GPIO32 | **PDM DATA** | J3 (USB-C) D- via R10 → MIC1 DATA |
| GPIO34 | SD Card Detect | J1 (MicroSD) pin 9 via R6 |
| GPIO35 | VBAT ADC | R2/R3 divider midpoint |
| 3V3 | Power | 3V3 rail, J3 CC1 (mic power) |
| GND | Ground | GND rail |

---

## Component Count Comparison

| Category | ES7243E Design | PDM Design | Saved |
|----------|----------------|------------|-------|
| ICs | 2 (ES7243E + NCP170) | 1 (NCP170) | 1 |
| Microphones | External electret | PDM on USB-C cable | - |
| Capacitors | 17 | 7 (C20 at mic end) | 10 |
| Resistors | 9 | 6-8 | 1-3 |
| Connectors | 5 (J1-J5) | 5 (J1-J5, J3=USB-C) | 0 |
| Transistors | 3 | 3 | 0 |
| **Total PCB** | ~36 | ~22 | ~14 |

**Cost savings:** ~$1-2 per board (ES7243E + passives)
**Additional benefits:**
- USB-C cable: shielded, cheap ($1-2), replaceable
- Mic can be positioned optimally away from PCB
- No analog routing concerns on PCB
- Simpler firmware (no I2C configuration)
