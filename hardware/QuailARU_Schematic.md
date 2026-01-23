# QuailARU Schematic

**ESP32S NodeMCU 38-Pin Module Based Design**

*Version 1.2 — January 22, 2026*

---

## 1. ESP32S NodeMCU 38-Pin Module Pinout Reference

```
                    ┌──────────────────────────┐
                    │       USB Connector      │
                    │          [USB]           │
                    └──────────────────────────┘
                    ┌──────────────────────────┐
              3V0 ──┤ 1                    38 ├── GND
               EN ──┤ 2                    37 ├── GPIO23 ──► SD_MOSI
        SVP/GPIO36 ─┤ 3                    36 ├── GPIO22
        SVN/GPIO39 ─┤ 4                    35 ├── GPIO1 (TX0)
           GPIO34 ──┤ 5                    34 ├── GPIO3 (RX0)
           GPIO35 ──┤ 6                    33 ├── GPIO21
    I2S_DOUT/GPIO32─┤ 7  ◄── ES7243E DOUT 32 ├── GND
           GPIO33 ──┤ 8                    31 ├── GPIO19 ──► SD_MISO
           GPIO25 ──┤ 9                    30 ├── GPIO18 ──► SD_CLK
           GPIO26 ──┤ 10                   29 ├── GPIO5  ──► SD_CS
           GPIO27 ──┤ 11                   28 ├── GPIO17 ──► GPS_RX
    I2S_BCK/GPIO14 ─┤ 12 ──► ES7243E BCK   27 ├── GPIO16 ◄── GPS_TX
           GPIO12 ──┤ 13                   26 ├── GPIO4  ◄── GPS_PPS
              GND ──┤ 14                   25 ├── GPIO0  ──► ES7243E SCKI (MCLK)
           GPIO13 ──┤ 15                   24 ├── GPIO2  ──► GPS_PWR_EN
              SD2 ──┤ 16                   23 ├── GPIO15 ──► ES7243E LRCK
              SD3 ──┤ 17                   22 ├── SD1
              CMD ──┤ 18                   21 ├── SD0
               5V ──┤ 19                   20 ├── CLK
                    └──────────────────────────┘
```

---

## 2. Complete System Schematic

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    POWER SUBSYSTEM                                          │
│                                                                                             │
│    ┌─────────────────────────────────────────┐                                              │
│    │         1S4P BATTERY PACK               │                                              │
│    │  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐       │                                              │
│    │  │18650│ │18650│ │18650│ │18650│       │                                              │
│    │  │3.7V │ │3.7V │ │3.7V │ │3.7V │       │         ┌─────────────────────┐              │
│    │  │3400 │ │3400 │ │3400 │ │3400 │       │         │      NCP170         │              │
│    │  │ mAh │ │ mAh │ │ mAh │ │ mAh │       │         │    LDO 3.0V         │              │
│    │  └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘       │         │                     │              │
│    │     └───────┴───────┴───────┘          │         │  ┌───┐     ┌───┐   │              │
│    │              │  3.7V, 13.6Ah           │         │  │ 1 ├─────┤ 3 │   │              │
│    └──────────────┼─────────────────────────┘         │  │VIN│     │VOUT│──┼──► 3.0V Rail │
│                   │                                    │  └─┬─┘     └───┘   │              │
│                   │    ┌────────────┐                 │    │    ┌───┐      │              │
│                   └────┤ 100µF ─┴─  ├─────────────────┼────┘    │ 2 ├──────┼──► GND       │
│                        │ Electrolytic│                 │         │GND│      │              │
│                        └────────────┘                 │         └───┘      │              │
│                                                       └─────────────────────┘              │
│                                                                │                            │
│                              ┌──────────────────────────────────┤                            │
│                              │                                  │                            │
│                              ▼                                  ▼                            │
│                        ┌──────────┐                       ┌──────────┐                      │
│                        │  0.1µF   │                       │  10µF    │                      │
│                        │  Ceramic │                       │  Ceramic │                      │
│                        └────┬─────┘                       └────┬─────┘                      │
│                             └──────────────┬───────────────────┘                            │
│                                            │                                                │
│                                           GND                                               │
└─────────────────────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    AUDIO SUBSYSTEM                                          │
│                                                                                             │
│  ⚠️ CRITICAL: ES7243E differential inputs MUST be AC-coupled to AGND, NOT direct ground!   │
│                                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────────────────────┐  │
│  │                                                                                       │  │
│  │    3.0V                                                                               │  │
│  │     │                                                                                 │  │
│  │    ┌┴┐                                                                                │  │
│  │    │ │ 2.0kΩ                                                                          │  │
│  │    │ │ Bias Resistor                                                                  │  │
│  │    └┬┘                                                                                │  │
│  │     │                                                                                 │  │
│  │     ├──────────────┐                                                                  │  │
│  │     │              │                                                                  │  │
│  │  ┌──┴──┐           │                                                                  │  │
│  │  │     │           │                                                                  │  │
│  │  │ PUI │       ┌───┴───┐                                                              │  │
│  │  │AOM- │       │       │   ┌─────────────────────────────────────────────────────┐    │  │
│  │  │5024L│       │  1µF  │   │              ES7243E ADC (QFN-20)                    │    │  │
│  │  │-HD-R│       │  DC   │   │                                                     │    │  │
│  │  │     │       │ Block │   │ Pin 9  AINLP ◄────────────────────────────────────────   │  │
│  │  │ (+) ├───────┤ (C11) ├───┤        (Left + input, audio from mic via C11)       │    │  │
│  │  │     │       │       │   │                                                     │    │  │
│  │  │ (-) ├───┐   └───────┘   │ Pin 10 AINLN ─────[1µF C15]───┬─ AGND               │    │  │
│  │  └─────┘   │               │        (Left - input, AC-coupled to AGND)           │    │  │
│  │            │               │                               │                     │    │  │
│  │           GND              │ Pin 15 AINRN ─────[1µF C16]───┤  AGND               │    │  │
│  │                            │        (Right - input, AC-coupled to AGND)          │    │  │
│  │                            │                               │                     │    │  │
│  │                            │ Pin 16 AINRP ─────[1µF C17]───┘  AGND               │    │  │
│  │                            │        (Right + input, AC-coupled to AGND)          │    │  │
│  │                            │                                                     │    │  │
│  │                            │ Pin 11 REFQ  ─────[10µF C8]──── AGND                │    │  │
│  │                            │        (Internal 1.45V reference)                   │    │  │
│  │                            │                                                     │    │  │
│  │                            │ Pin 14 REFP  ─────[10µF C12]─── AGND                │    │  │
│  │                            │        (Internal reference)                         │    │  │
│  │                            │                                                     │    │  │
│  │                            │ Pin 12 VDDA  ─────[10µF C9]──── AGND  + [100nF C5]  │    │  │
│  │                            │ Pin 5  VDDD  ─────[100nF C4]─── DGND                │    │  │
│  │                            │ Pin 1  VDDP  ◄──────────────── 3.0V                 │    │  │
│  │                            │                                                     │    │  │
│  │                            │ Pin 20 MCLK ◄───────────────── GPIO0 (12.288 MHz)   │    │  │
│  │                            │ Pin 6  SCLK ◄───────────────── GPIO14 (3.072 MHz)   │    │  │
│  │                            │ Pin 7  LRCK ◄───────────────── GPIO15 (48 kHz)      │    │  │
│  │                            │ Pin 3  SDOUT ──────────────►   GPIO32 (I2S data)    │    │  │
│  │                            │                                                     │    │  │
│  │                            │ Pin 18 CDATA ◄──────────────── GPIO21 (I2C SDA)     │    │  │
│  │                            │ Pin 19 CCLK  ◄──────────────── GPIO22 (I2C SCL)     │    │  │
│  │                            │                                                     │    │  │
│  │                            │ Pin 17 AD0   ◄──────────────── GND (I2C addr 0x10)  │    │  │
│  │                            │ Pin 8  AD1   ◄──────────────── GND                  │    │  │
│  │                            │ Pin 4  GNDD, Pin 13 GNDA, Pin 21 EP ─► GND          │    │  │
│  │                            └─────────────────────────────────────────────────────┘    │  │
│  │                                                                                       │  │
│  │    NOTE: I2C requires external 4.7kΩ pull-ups on SDA and SCL to 3.0V                  │  │
│  │                                                                                       │  │
│  └───────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    GPS SUBSYSTEM                                            │
│                                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────────────────────┐  │
│  │                                                                                       │  │
│  │                         ┌─────────────────────────────────────────────────────────┐   │  │
│  │                         │   J4 - L76K GPS Module Connector (8-pin)                │   │  │
│  │                         │                                                         │   │  │
│  │     3.0V ──────────────►│ Pin 1  RESET_N (hold high, active low reset)            │   │  │
│  │     3.0V ──[100nF C6]──►│ Pin 2  VCC     (power with decoupling)                  │   │  │
│  │     3.0V ──────────────►│ Pin 3  V_BCKP  (backup power)                           │   │  │
│  │     GPIO16 (RX2) ◄──────│ Pin 4  TX_GPS  (GPS transmits to ESP32)                 │   │  │
│  │     GPIO17 (TX2) ──────►│ Pin 5  RX_GPS  (ESP32 transmits to GPS)                 │   │  │
│  │     3.0V ──────────────►│ Pin 6  WAKEUP  (hold high)                              │   │  │
│  │     GPIO4 ◄─────────────│ Pin 7  PPS     (1Hz pulse, ±10ns accuracy)              │   │  │
│  │     GND ───────────────►│ Pin 8  GND                                              │   │  │
│  │                         │                                                         │   │  │
│  │                         └─────────────────────────────────────────────────────────┘   │  │
│  │                                                                                       │  │
│  │    Power management via PMTK firmware commands (no hardware power gating):            │  │
│  │    - Standby: $PMTK161,0*28 (wake with any UART byte)                                 │  │
│  │    - Full power: $PMTK225,0*2B                                                        │  │
│  │                                                                                       │  │
│  └───────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   SD CARD SUBSYSTEM                                         │
│                                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────────────────────┐  │
│  │                                                                                       │  │
│  │                         ┌───────────────────────────────────────┐                     │  │
│  │                         │         MICROSD MODULE (SPI)          │                     │  │
│  │                         │                                       │                     │  │
│  │     3.0V ──────────────►│ VCC (3.0V)                            │                     │  │
│  │                         │                                       │                     │  │
│  │     GND ───────────────►│ GND                                   │                     │  │
│  │                         │                                       │                     │  │
│  │     GPIO5  ────────────►│ CS  (Chip Select)                     │                     │  │
│  │     ESP32               │                                       │                     │  │
│  │                         │                                       │                     │  │
│  │     GPIO18 ────────────►│ SCK (SPI Clock)                       │                     │  │
│  │     ESP32               │                                       │                     │  │
│  │                         │                                       │                     │  │
│  │     GPIO23 ────────────►│ MOSI (Master Out Slave In)            │                     │  │
│  │     ESP32               │                                       │                     │  │
│  │                         │                                       │                     │  │
│  │     GPIO19 ◄────────────│ MISO (Master In Slave Out)            │                     │  │
│  │     ESP32               │                                       │                     │  │
│  │                         └───────────────────────────────────────┘                     │  │
│  │                                                                                       │  │
│  │    Note: Most MicroSD modules include level shifters and pull-ups.                    │  │
│  │          If using bare SD card socket, add 10kΩ pull-ups on MISO and CS.              │  │
│  │                                                                                       │  │
│  └───────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                              TEMPERATURE/HUMIDITY SUBSYSTEM                                 │
│                                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────────────────────┐  │
│  │                                                                                       │  │
│  │                         ┌───────────────────────────────────────┐                     │  │
│  │                         │   J5 - SHT30 External Module Conn     │                     │  │
│  │                         │         (4-pin header)                │                     │  │
│  │                         │                                       │                     │  │
│  │     GND ───────────────►│ Pin 1 GND                             │                     │  │
│  │                         │                                       │                     │  │
│  │     3.0V ──────────────►│ Pin 2 VCC                             │                     │  │
│  │                         │                                       │                     │  │
│  │     GPIO21 ◄───────────►│ Pin 3 SDA   (shared I2C bus)          │                     │  │
│  │     ESP32               │                                       │                     │  │
│  │                         │                                       │                     │  │
│  │     GPIO22 ────────────►│ Pin 4 SCL   (shared I2C bus)          │                     │  │
│  │     ESP32               │                                       │                     │  │
│  │                         └───────────────────────────────────────┘                     │  │
│  │                                                                                       │  │
│  │    External SHT30 Module Specifications:                                              │  │
│  │    - I2C address: 0x44 (default)                                                      │  │
│  │    - Temperature accuracy: ±0.2°C                                                     │  │
│  │    - Humidity accuracy: ±2% RH                                                        │  │
│  │    - Module has onboard decoupling capacitor                                          │  │
│  │                                                                                       │  │
│  │    Note: Shares I2C bus with ES7243E (0x10). Uses same 4.7kΩ pull-ups.                │  │
│  │                                                                                       │  │
│  └───────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Connection Summary Tables

### 3.1 ESP32 NodeMCU Pin Connections

| ESP32 GPIO | Module Pin | Function | Direction | Connected To |
|------------|------------|----------|-----------|--------------|
| GPIO0 | 25 | I2S MCLK | Output | ES7243E MCLK |
| GPIO34 | 6 | SD Card Detect | Input | SD Card pin 9 via R6 4.7k pull-up |
| GPIO4 | 26 | PPS Input | Input | L76K PPS |
| GPIO5 | 29 | SPI CS | Output | SD Card CS |
| GPIO14 | 12 | I2S SCLK | Output | ES7243E SCLK |
| GPIO15 | 23 | I2S LRCK/WS | Output | ES7243E LRCK |
| GPIO16 | 27 | UART2 RX | Input | L76K TX |
| GPIO17 | 28 | UART2 TX | Output | L76K RX |
| GPIO18 | 30 | SPI CLK | Output | SD Card CLK |
| GPIO19 | 31 | SPI MISO | Input | SD Card MISO |
| GPIO21 | 33 | I2C SDA | Bidir | ES7243E SDA, J5 SHT30 Module pin 3 |
| GPIO22 | 36 | I2C SCL | Output | ES7243E SCL, J5 SHT30 Module pin 4 |
| GPIO23 | 37 | SPI MOSI | Output | SD Card MOSI |
| GPIO32 | 7 | I2S Data In | Input | ES7243E SDOUT |
| 3V0 | 1 | Power | - | From NCP170 VOUT (3.0V) |
| GND | 14, 32, 38 | Ground | - | Common Ground |

### 3.2 ES7243E ADC Connections (QFN-20)

⚠️ **CRITICAL**: Differential inputs (AINLN, AINRN, AINRP) must be AC-coupled to AGND via 1µF caps, NOT direct ground!

| ES7243E Pin | Function | Connected To |
|-------------|----------|--------------|
| Pin 1 VDDP | Digital Power Supply | 3.0V Rail |
| Pin 5 VDDD | Digital Power Supply | 3.0V + 100nF to DGND |
| Pin 12 VDDA | Analog Power Supply | 3.0V + 10µF to AGND |
| Pin 4 GNDD | Digital Ground | Common GND |
| Pin 13 GNDA | Analog Ground | AGND (common GND) |
| Pin 21 EP | Thermal Pad | GND |
| Pin 20 MCLK | Master Clock Input | ESP32 GPIO0 (12.288 MHz) |
| Pin 6 SCLK | Bit Clock | ESP32 GPIO14 |
| Pin 7 LRCK | L/R Word Clock | ESP32 GPIO15 |
| Pin 3 SDOUT | Digital Audio Out | ESP32 GPIO32 |
| Pin 9 AINLP | Left + Analog Input | Microphone via 1µF DC-block cap (C11) |
| Pin 10 AINLN | Left - Analog Input | **1µF cap to AGND (AC-coupled!)** |
| Pin 16 AINRP | Right + Analog Input | **1µF cap to AGND (AC-coupled!)** |
| Pin 15 AINRN | Right - Analog Input | **1µF cap to AGND (AC-coupled!)** |
| Pin 11 REFQ | Internal Reference | 10µF cap to AGND |
| Pin 14 REFP | Internal Reference | 10µF cap to AGND |
| Pin 18 CDATA | I2C Data (SDA) | ESP32 GPIO21 + 4.7kΩ pull-up to 3V0 |
| Pin 19 CCLK | I2C Clock (SCL) | ESP32 GPIO22 + 4.7kΩ pull-up to 3V0 |
| Pin 17 AD0 | I2C Address Select | GND (Address 0x10) |
| Pin 8 AD1 | I2C Address Select | GND |

### 3.3 Quectel L76K GPS Connections (via J4 Connector)

**J4 8-pin connector matches L76K module header pinout:**

| J4 Pin | L76K Signal | Function | Connected To |
|--------|-------------|----------|--------------|
| 1 | RESET_N | Reset (active low) | 3.0V (hold high) |
| 2 | VCC | Power Supply | 3.0V + 100nF decoupling (C6) |
| 3 | V_BCKP | Backup Power | 3.0V |
| 4 | TX_GPS | UART Transmit | ESP32 GPIO16 (RX2) |
| 5 | RX_GPS | UART Receive | ESP32 GPIO17 (TX2) |
| 6 | WAKEUP | Wake input | 3.0V (hold high) |
| 7 | PPS | Pulse Per Second | ESP32 GPIO4 |
| 8 | GND | Ground | Common GND |

### 3.4 MicroSD Module Connections

| SD Pin | Function | Connected To |
|--------|----------|--------------|
| VCC | Power Supply | 3.0V Rail |
| GND | Ground | Common GND |
| CS | Chip Select | ESP32 GPIO5 |
| SCK | SPI Clock | ESP32 GPIO18 |
| MOSI | SPI Data In | ESP32 GPIO23 |
| MISO | SPI Data Out | ESP32 GPIO19 |
| CD | Card Detect | ESP32 GPIO34 via R6 4.7k pull-up (LOW = inserted) |

### 3.5 SHT30 External Module Connector (J5)

The SHT30 is an external 4-wire module with onboard decoupling. No additional capacitors required.

| J5 Pin | Function | Connected To |
|--------|----------|--------------|
| Pin 1 | GND | Common GND |
| Pin 2 | VCC | 3.0V Rail |
| Pin 3 | SDA | ESP32 GPIO21 (shared I2C bus) |
| Pin 4 | SCL | ESP32 GPIO22 (shared I2C bus) |

**External Module Specifications:**
- I2C address: 0x44 (default)
- Temperature accuracy: ±0.2°C
- Humidity accuracy: ±2% RH
- Module has onboard decoupling capacitor

**I2C Bus Summary:**
- ES7243E: Address 0x10
- SHT30 Module: Address 0x44
- Pull-ups: 4.7kΩ to 3.0V on SDA and SCL (shared)

---

## 4. Power Rail Distribution

```
                                    ┌──────────────────────┐
                                    │                      │
    Battery Pack ────► NCP170 ──────┼───► ESP32 NodeMCU    │
    3.7V nominal       LDO         │      (via 3V0 pin)    │
                       3.0V Out     │                      │
                                    ├───► ES7243E ADC      │
                                    │      (VCC)           │
                                    │                      │
                                    ├───► MicroSD Module   │
                                    │      (VCC)           │
                                    │                      │
                                    ├───► J5 SHT30 Module  │
                                    │      (VCC)           │
                                    │                      │
                                    └───► GPS L76K         │
                                           (direct power)  │
                                                           │
                    ┌──────────────────────────────────────┘
                    │
                    ▼
               Common GND ───────────────────────────────────►
                           (All modules share ground)
```

---

## 5. Microphone Bias Circuit Detail

⚠️ **CRITICAL ES7243E REQUIREMENT**: The differential inputs (AINLN, AINRN, AINRP) must be
AC-coupled to analog ground via 1µF capacitors. Direct grounding disrupts the internal
bias circuitry (~1.45V from REFQ) and causes severe signal degradation.

```
                    3.0V
                     │
                     │
                    ┌┴┐
                    │ │  R1 = 2.0kΩ
                    │ │  (Bias Resistor)
                    └┬┘
                     │
         ┌───────────┼───────────┐
         │           │           │
         │      ┌────┴────┐      │
         │      │  (+)    │      │
         │      │         │      │     ┌────────────┐
         │      │  PUI    │      │     │            │
         │      │ AOM-5024├──────┼─────┤  1µF   ────├────► ES7243E Pin 9 (AINLP)
         │      │  L-HD-R │      │     │  DC Block  │
         │      │         │      │     │  (C11)     │
         │      │  (-)    │      │     └────────────┘
         │      └────┬────┘      │
         │           │           │
         │          GND          │
         │                       │
         │    0.1µF Bypass Cap   │
         │      (optional)       │
         └───────────────────────┘
                     │
                    GND


    ES7243E Differential Input AC-Coupling (from datasheet reference design):

    AINLP (Pin 9)  ◄──── Audio signal (from microphone via 1µF DC-block C11)
    AINLN (Pin 10) ────[1µF]──── AGND   ← AC-coupled, NOT direct ground!
    AINRN (Pin 15) ────[1µF]──── AGND   ← AC-coupled, NOT direct ground!
    AINRP (Pin 16) ────[1µF]──── AGND   ← AC-coupled, NOT direct ground!

    The ES7243E provides internal bias (~1.45V from REFQ pin) for the differential
    inputs. Direct grounding fights this internal bias, pulling AINLP down to ~0.7V
    instead of the mid-rail ~1.65V, causing signal attenuation and requiring maximum
    PGA gain which increases noise.


    ES7243E Reference Capacitors:

    REFQ (Pin 11) ────[10µF]──── AGND   (Internal ~1.45V reference)
    REFP (Pin 14) ────[10µF]──── AGND   (Internal reference)
    VDDA (Pin 12) ────[10µF]──── AGND   (Analog supply bypass)
    VDDD (Pin 5)  ────[100nF]─── DGND   (Digital supply bypass)


    Component Values:
    - R1: 2.0kΩ (sets ~1.5mA bias current for electret mic)
    - C11: 1µF (DC blocking / AINLP decoupling per reference design)
    - C15, C16, C17: 1µF (AC-coupling for AINLN, AINRN, AINRP)
    - C8, C12: 10µF (REFQ, REFP bypass)
    - C9: 10µF (VDDA bypass)
    - C5: 100nF (VDDD bypass)
```

---

## 6. GPS Power Management (Firmware-Based)

The L76K GPS module is always powered from 3.0V (no hardware power gating).
Power management uses PMTK firmware commands via UART:

```
    GPS Power States:

    FULL POWER (default):
    - Active tracking, ~25-29mA
    - Command: $PMTK225,0*2B

    STANDBY MODE:
    - Ultra-low power ~1mA
    - Command: $PMTK161,0*28
    - Wake: Send any byte over UART

    PERIODIC MODE (example - 3s on, 12s sleep):
    - Automatic wake/sleep cycling
    - Command: $PMTK225,2,3000,12000,18000,72000*XX
```

**Note:** GPIO2 is now available for other uses (was previously GPS power control).

---

## 7. Decoupling Capacitors

Add these capacitors for stable operation:

| Location | Capacitor | Type | Purpose |
|----------|-----------|------|---------|
| Battery input to HT7333 | 100µF | Electrolytic | Input smoothing |
| HT7333 output | 10µF | Ceramic | Output stability |
| ESP32 3V0 pin | 0.1µF | Ceramic | HF decoupling |
| ES7243E VDDD (Pin 5) | 100nF | Ceramic | Digital power filtering |
| ES7243E VDDA (Pin 12) | 10µF | Ceramic | Analog power filtering |
| ES7243E REFQ (Pin 11) | 10µF | Ceramic | Reference bypass |
| ES7243E REFP (Pin 14) | 10µF | Ceramic | Reference bypass |
| ES7243E AINLN (Pin 10) | 1µF | Ceramic | AC-coupling (NOT direct GND!) |
| ES7243E AINRN (Pin 15) | 1µF | Ceramic | AC-coupling (NOT direct GND!) |
| ES7243E AINRP (Pin 16) | 1µF | Ceramic | AC-coupling (NOT direct GND!) |
| L76K VCC | 0.1µF | Ceramic | HF decoupling |

**I2C Pull-up Resistors (required):**

| Location | Resistor | Purpose |
|----------|----------|---------|
| SDA line (GPIO21) | 4.7kΩ to 3V0 | I2C pull-up |
| SCL line (GPIO22) | 4.7kΩ to 3V0 | I2C pull-up |

---

## 8. I2S Clock Configuration

```
    ┌─────────────────────────────────────────────────────────────────┐
    │                    I2S CLOCK HIERARCHY                          │
    │                                                                 │
    │    MCLK (Master Clock)                                          │
    │    ────────────────────                                         │
    │    Frequency: 12.288 MHz (256 × 48kHz)                          │
    │    Source: ESP32 GPIO0 → ES7243E MCLK                           │
    │                                                                 │
    │              │                                                  │
    │              │ ÷4                                               │
    │              ▼                                                  │
    │                                                                 │
    │    SCLK (Bit Clock)                                             │
    │    ────────────────                                             │
    │    Frequency: 3.072 MHz (64 × 48kHz)                            │
    │    Source: ESP32 GPIO14 → ES7243E SCLK                          │
    │                                                                 │
    │              │                                                  │
    │              │ ÷64                                              │
    │              ▼                                                  │
    │                                                                 │
    │    LRCK/WS (Word Select)                                        │
    │    ────────────────────                                         │
    │    Frequency: 48 kHz (sample rate)                              │
    │    Source: ESP32 GPIO15 → ES7243E LRCK                          │
    │                                                                 │
    │    ES7243E operates in SLAVE mode:                              │
    │    - All I2S clocks derived from ESP32                          │
    │    - Requires I2C initialization before capture                 │
    │    - I2C Address: 0x10 (AD0=GND) or 0x11 (AD0=VDD)             │
    │                                                                 │
    └─────────────────────────────────────────────────────────────────┘
```

---

## 9. Physical Wiring Diagram

```
   ┌─────────────────────────────────────────────────────────────────────────────────────┐
   │                                                                                     │
   │   [BATTERY PACK 1S4P]                                                               │
   │         │                                                                           │
   │         │ 3.7V                                                                      │
   │         ▼                                                                           │
   │   ┌──────────┐                                                                      │
   │   │  NCP170  │────────────────────────────────────────────┐                         │
   │   │   LDO    │                                            │ 3.0V                    │
   │   └──────────┘                                            │                         │
   │                                                           │                         │
   │   ┌───────────────────────────────────────────────────────┼─────────────────────┐   │
   │   │                                                       │                     │   │
   │   │  ┌─────────────────────────────────────────────────┐  │                     │   │
   │   │  │              ESP32S NodeMCU 38-Pin              │  │                     │   │
   │   │  │                                                 │  │                     │   │
   │   │  │  3V0 ◄──────────────────────────────────────────┼──┘                     │   │
   │   │  │                                                 │                        │   │
   │   │  │  GPIO0  ─────────────────────────────────────────────► ES7243E SCKI      │   │
   │   │  │  GPIO14 ─────────────────────────────────────────────► ES7243E BCK       │   │
   │   │  │  GPIO15 ─────────────────────────────────────────────► ES7243E LRCK      │   │
   │   │  │  GPIO32 ◄────────────────────────────────────────────  ES7243E DOUT      │   │
   │   │  │                                                 │                        │   │
   │   │  │  GPIO16 ◄────────────────────────────────────────────  L76K TX           │   │
   │   │  │  GPIO17 ─────────────────────────────────────────────► L76K RX           │   │
   │   │  │  GPIO4  ◄────────────────────────────────────────────  L76K PPS          │   │
   │   │  │                                                 │                        │   │
   │   │  │  GPIO5  ─────────────────────────────────────────────► SD CS             │   │
   │   │  │  GPIO18 ─────────────────────────────────────────────► SD CLK            │   │
   │   │  │  GPIO19 ◄────────────────────────────────────────────  SD MISO           │   │
   │   │  │  GPIO23 ─────────────────────────────────────────────► SD MOSI           │   │
   │   │  │                                                 │                        │   │
   │   │  │  GND ───────────────────────────────────────────┼────► Common GND        │   │
   │   │  │                                                 │                        │   │
   │   │  └─────────────────────────────────────────────────┘                        │   │
   │   │                                                                             │   │
   │   └─────────────────────────────────────────────────────────────────────────────┘   │
   │                                                                                     │
   └─────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 10. Notes and Considerations

### 10.1 GPIO0 Boot Mode

GPIO0 is used for MCLK output. During boot:
- GPIO0 LOW = Download mode (for flashing)
- GPIO0 HIGH = Normal boot

The ES7243E SCKI input has high impedance and won't interfere with boot. However, ensure no strong pull-down is connected.

### 10.2 Power Sequencing

1. Apply battery power → NCP170 provides 3.0V
2. ESP32 boots, all peripherals powered
3. Firmware initializes I2S, I2C, SPI, UART
4. GPS power managed via PMTK standby commands (no GPIO control)

### 10.3 Current Budget Summary

| Subsystem | Active Current | Sleep Current |
|-----------|----------------|---------------|
| ESP32 | 80 mA | 10 µA |
| ES7243E + Mic | 8 mA | 8 mA (always on) |
| L76K GPS | 29 mA | ~1 mA (PMTK standby) |
| SD Card | 100 mA (write) | 100 µA |
| **Total Recording** | **~217 mA** | - |
| **Total Sleep** | - | **~1.1 mA** |

### 10.4 Component Placement Recommendations

- Keep microphone away from ESP32 (WiFi/BLE RF noise)
- Short traces between mic and ES7243E (analog sensitive)
- GPS antenna needs clear sky view (top of enclosure)
- SD card can be anywhere with short SPI traces
- Bulk capacitors near power input

---

## 11. LCSC Parts Reference

All parts sourced from LCSC for JLCPCB assembly:

| Component | LCSC Part # | Description |
|-----------|-------------|-------------|
| ES7243E | C2929446 | 24-bit I2S ADC, QFN-20 |
| NCP170ASN300T2G | C603670 | 3.0V 150mA LDO, TSOP-5, 500nA Iq |
| L76K | C2838031 | GPS module with PPS |
| TF-015 | C113206 | MicroSD card socket |
| AOM-5024L-HD-R | C3273706 | Electret mic (80dB SNR) |
| JST PH 2-pin | C295747 | Battery connector SMT |
| 2.0kΩ 0603 | C22975 | Mic bias resistor |
| 4.7kΩ 0603 | C23162 | I2C pull-up resistor |
| 100nF 0603 | C14663 | Decoupling capacitor |
| 1µF 0805 | C28323 | AC-coupling capacitor (AINLN, AINRN, AINRP) - Basic |
| 10µF 0805 | C15850 | Bulk capacitor (REFQ, REFP, VDDA) |
| 4.7µF 0805 | C1779 | Bulk capacitor |
