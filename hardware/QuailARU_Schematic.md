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
              3V3 ──┤ 1                    38 ├── GND
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
│    │  │3400 │ │3400 │ │3400 │ │3400 │       │         │      HT7333         │              │
│    │  │ mAh │ │ mAh │ │ mAh │ │ mAh │       │         │    LDO 3.3V         │              │
│    │  └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘       │         │                     │              │
│    │     └───────┴───────┴───────┘          │         │  ┌───┐     ┌───┐   │              │
│    │              │  3.7V, 13.6Ah           │         │  │ 1 ├─────┤ 3 │   │              │
│    └──────────────┼─────────────────────────┘         │  │VIN│     │VOUT│──┼──► 3.3V Rail │
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
│  │    3.3V                                                                               │  │
│  │     │                                                                                 │  │
│  │    ┌┴┐                                                                                │  │
│  │    │ │ 2.2kΩ                                                                          │  │
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
│  │                            │ Pin 1  VDDP  ◄──────────────── 3.3V                 │    │  │
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
│  │    NOTE: I2C requires external 4.7kΩ pull-ups on SDA and SCL to 3.3V                  │  │
│  │                                                                                       │  │
│  └───────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    GPS SUBSYSTEM                                            │
│                                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────────────────────┐  │
│  │                                                                                       │  │
│  │     3.3V ────────┐                                                                    │  │
│  │                  │                                                                    │  │
│  │            ┌─────┴─────┐                                                              │  │
│  │            │    S      │                                                              │  │
│  │            │  ┌───┐    │  ◄───────────────────────────────── GPIO2 (GPS_PWR_EN)       │  │
│  │            │  │FET│    │           (SI2301 P-Ch MOSFET)       ESP32                   │  │
│  │            │  └─┬─┘ D  │           Active LOW to enable                               │  │
│  │            │    G      │                                                              │  │
│  │            └────┼──────┘                                                              │  │
│  │                 │                                                                     │  │
│  │                 │    ┌─────────────────────────────────────────────────────────────┐  │  │
│  │                 │    │                   QUECTEL L76K GPS                          │  │  │
│  │                 │    │                                                             │  │  │
│  │                 └───►│ VCC ◄─── 3.3V (switched)                                    │  │  │
│  │                      │                                                             │  │  │
│  │                      │ GND ◄─── GND                                                │  │  │
│  │                      │                                                             │  │  │
│  │                      │ TX  ───────────────────────────────►  GPIO16 (UART2 RX)     │  │  │
│  │                      │                                        ESP32                │  │  │
│  │                      │                                                             │  │  │
│  │                      │ RX  ◄───────────────────────────────  GPIO17 (UART2 TX)     │  │  │
│  │                      │                                        ESP32                │  │  │
│  │                      │                                                             │  │  │
│  │                      │ PPS ───────────────────────────────►  GPIO4                 │  │  │
│  │                      │       (1Hz pulse, ±10ns accuracy)      ESP32                │  │  │
│  │                      │                                                             │  │  │
│  │                      │ ANT ◄─── [25×25mm Ceramic Patch Antenna]                    │  │  │
│  │                      │                                                             │  │  │
│  │                      └─────────────────────────────────────────────────────────────┘  │  │
│  │                                                                                       │  │
│  │    Note: 10kΩ pull-up on GPIO2 to keep GPS OFF by default during boot                 │  │
│  │          Add 100nF decoupling capacitor near GPS VCC pin                              │  │
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
│  │     3.3V ──────────────►│ VCC (3.3V)                            │                     │  │
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
│  │                         │         SHT30-DIS (DFN-8)             │                     │  │
│  │                         │    Temperature/Humidity Sensor        │                     │  │
│  │                         │                                       │                     │  │
│  │     3.3V ──────────────►│ Pin 5 VDD                             │                     │  │
│  │            │            │                                       │                     │  │
│  │           ─┴─           │                                       │                     │  │
│  │           ───  100nF    │                                       │                     │  │
│  │            │            │                                       │                     │  │
│  │     GND ───┴───────────►│ Pin 4 VSS                             │                     │  │
│  │                         │                                       │                     │  │
│  │     GND ───────────────►│ Pin 2 ADDR  (I2C address 0x44)        │                     │  │
│  │                         │                                       │                     │  │
│  │     GPIO21 ◄───────────►│ Pin 1 SDA   (shared I2C bus)          │                     │  │
│  │     ESP32               │                                       │                     │  │
│  │                         │                                       │                     │  │
│  │     GPIO22 ────────────►│ Pin 6 SCL   (shared I2C bus)          │                     │  │
│  │     ESP32               │                                       │                     │  │
│  │                         │                                       │                     │  │
│  │                      NC │ Pin 3 ALERT (not used)                │                     │  │
│  │                      NC │ Pin 7 nRESET (internal pull-up)       │                     │  │
│  │                      NC │ Pin 8 R (reserved)                    │                     │  │
│  │                         │                                       │                     │  │
│  │                         └───────────────────────────────────────┘                     │  │
│  │                                                                                       │  │
│  │    Specifications:                                                                    │  │
│  │    - Temperature accuracy: ±0.2°C                                                     │  │
│  │    - Humidity accuracy: ±2% RH                                                        │  │
│  │    - Operating range: -40°C to +125°C                                                 │  │
│  │    - Supply current: ~1.5µA average (single shot mode)                                │  │
│  │    - I2C address: 0x44 (ADDR pin to GND)                                              │  │
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
| GPIO2 | 24 | GPS Power Enable | Output | SI2301 Gate (Active LOW) |
| GPIO4 | 26 | PPS Input | Input | L76K PPS |
| GPIO5 | 29 | SPI CS | Output | SD Card CS |
| GPIO14 | 12 | I2S SCLK | Output | ES7243E SCLK |
| GPIO15 | 23 | I2S LRCK/WS | Output | ES7243E LRCK |
| GPIO16 | 27 | UART2 RX | Input | L76K TX |
| GPIO17 | 28 | UART2 TX | Output | L76K RX |
| GPIO18 | 30 | SPI CLK | Output | SD Card CLK |
| GPIO19 | 31 | SPI MISO | Input | SD Card MISO |
| GPIO21 | 33 | I2C SDA | Bidir | ES7243E SDA, SHT30 SDA |
| GPIO22 | 36 | I2C SCL | Output | ES7243E SCL, SHT30 SCL |
| GPIO23 | 37 | SPI MOSI | Output | SD Card MOSI |
| GPIO32 | 7 | I2S Data In | Input | ES7243E SDOUT |
| 3V3 | 1 | Power | - | From HT7333 VOUT |
| GND | 14, 32, 38 | Ground | - | Common Ground |

### 3.2 ES7243E ADC Connections (QFN-20)

⚠️ **CRITICAL**: Differential inputs (AINLN, AINRN, AINRP) must be AC-coupled to AGND via 1µF caps, NOT direct ground!

| ES7243E Pin | Function | Connected To |
|-------------|----------|--------------|
| Pin 1 VDDP | Digital Power Supply | 3.3V Rail |
| Pin 5 VDDD | Digital Power Supply | 3.3V + 100nF to DGND |
| Pin 12 VDDA | Analog Power Supply | 3.3V + 10µF to AGND |
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
| Pin 18 CDATA | I2C Data (SDA) | ESP32 GPIO21 + 4.7kΩ pull-up to 3V3 |
| Pin 19 CCLK | I2C Clock (SCL) | ESP32 GPIO22 + 4.7kΩ pull-up to 3V3 |
| Pin 17 AD0 | I2C Address Select | GND (Address 0x10) |
| Pin 8 AD1 | I2C Address Select | GND |

### 3.3 Quectel L76K GPS Connections

| L76K Pin | Function | Connected To |
|----------|----------|--------------|
| VCC | Power Supply | 3.3V via SI2301 MOSFET |
| GND | Ground | Common GND |
| TX | UART Transmit | ESP32 GPIO16 (RX2) |
| RX | UART Receive | ESP32 GPIO17 (TX2) |
| PPS | Pulse Per Second | ESP32 GPIO4 |
| ANT | Antenna | 25×25mm Ceramic Patch |

### 3.4 MicroSD Module Connections

| SD Pin | Function | Connected To |
|--------|----------|--------------|
| VCC | Power Supply | 3.3V Rail |
| GND | Ground | Common GND |
| CS | Chip Select | ESP32 GPIO5 |
| SCK | SPI Clock | ESP32 GPIO18 |
| MOSI | SPI Data In | ESP32 GPIO23 |
| MISO | SPI Data Out | ESP32 GPIO19 |

### 3.5 SHT30-DIS Temperature/Humidity Sensor Connections

| SHT30 Pin | Function | Connected To |
|-----------|----------|--------------|
| Pin 1 SDA | I2C Data | ESP32 GPIO21 (shared bus) |
| Pin 2 ADDR | Address Select | GND (I2C address 0x44) |
| Pin 3 ALERT | Alert Output | NC (not used) |
| Pin 4 VSS | Ground | Common GND |
| Pin 5 VDD | Power Supply | 3.3V Rail + 100nF decoupling |
| Pin 6 SCL | I2C Clock | ESP32 GPIO22 (shared bus) |
| Pin 7 nRESET | Reset | NC (internal pull-up) |
| Pin 8 R | Reserved | NC |

**I2C Bus Summary:**
- ES7243E: Address 0x10
- SHT30: Address 0x44
- Pull-ups: 4.7kΩ to 3.3V on SDA and SCL (shared)

---

## 4. Power Rail Distribution

```
                                    ┌──────────────────────┐
                                    │                      │
    Battery Pack ────► HT7333 ──────┼───► ESP32 NodeMCU    │
    3.7V nominal       LDO         │      (via VIN or 3V3) │
                       3.3V Out     │                      │
                                    ├───► ES7243E ADC      │
                                    │      (VCC)           │
                                    │                      │
                                    ├───► MicroSD Module   │
                                    │      (VCC)           │
                                    │                      │
                                    ├───► SHT30 Sensor     │
                                    │      (VDD)           │
                                    │                      │
                                    └───► GPS L76K         │
                                           (via MOSFET)    │
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
                    3.3V
                     │
                     │
                    ┌┴┐
                    │ │  R1 = 2.2kΩ
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
    - R1: 2.2kΩ (sets ~1.5mA bias current for electret mic)
    - C11: 1µF (DC blocking / AINLP decoupling per reference design)
    - C15, C16, C17: 1µF (AC-coupling for AINLN, AINRN, AINRP)
    - C8, C12: 10µF (REFQ, REFP bypass)
    - C9: 10µF (VDDA bypass)
    - C5: 100nF (VDDD bypass)
```

---

## 6. GPS Power Switching Circuit

```
                    3.3V Rail
                        │
                        │
                   ┌────┴────┐
                   │    S    │
                   │  ┌───┐  │
                   │  │   │  │ SI2301 P-Channel MOSFET
                   │  │ G ├──┼────────────────────────┐
                   │  │   │  │                        │
                   │  └─┬─┘  │                        │
                   │    D    │                       ┌┴┐
                   └────┬────┘                       │ │ R2 = 10kΩ
                        │                            │ │ (Pull-up)
                        │                            └┬┘
                        │                             │
                        ▼                             │
                  ┌──────────┐                        │
                  │  L76K    │                        │
                  │   VCC    │                        └───── GPIO2 (ESP32)
                  └──────────┘                              (LOW = GPS ON)
                                                            (HIGH = GPS OFF)

    Operation:
    - GPIO2 HIGH (default via pull-up): MOSFET OFF, GPS powered down
    - GPIO2 LOW: MOSFET ON, GPS receives 3.3V power
    - This enables GPS power gating for battery savings during deep sleep
```

---

## 7. Decoupling Capacitors

Add these capacitors for stable operation:

| Location | Capacitor | Type | Purpose |
|----------|-----------|------|---------|
| Battery input to HT7333 | 100µF | Electrolytic | Input smoothing |
| HT7333 output | 10µF | Ceramic | Output stability |
| ESP32 3V3 pin | 0.1µF | Ceramic | HF decoupling |
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
| SDA line (GPIO21) | 4.7kΩ to 3V3 | I2C pull-up |
| SCL line (GPIO22) | 4.7kΩ to 3V3 | I2C pull-up |

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
   │   │  HT7333  │────────────────────────────────────────────┐                         │
   │   │   LDO    │                                            │ 3.3V                    │
   │   └──────────┘                                            │                         │
   │                                                           │                         │
   │   ┌───────────────────────────────────────────────────────┼─────────────────────┐   │
   │   │                                                       │                     │   │
   │   │  ┌─────────────────────────────────────────────────┐  │                     │   │
   │   │  │              ESP32S NodeMCU 38-Pin              │  │                     │   │
   │   │  │                                                 │  │                     │   │
   │   │  │  3V3 ◄──────────────────────────────────────────┼──┘                     │   │
   │   │  │                                                 │                        │   │
   │   │  │  GPIO0  ─────────────────────────────────────────────► ES7243E SCKI      │   │
   │   │  │  GPIO14 ─────────────────────────────────────────────► ES7243E BCK       │   │
   │   │  │  GPIO15 ─────────────────────────────────────────────► ES7243E LRCK      │   │
   │   │  │  GPIO32 ◄────────────────────────────────────────────  ES7243E DOUT      │   │
   │   │  │                                                 │                        │   │
   │   │  │  GPIO16 ◄────────────────────────────────────────────  L76K TX           │   │
   │   │  │  GPIO17 ─────────────────────────────────────────────► L76K RX           │   │
   │   │  │  GPIO4  ◄────────────────────────────────────────────  L76K PPS          │   │
   │   │  │  GPIO2  ─────────────────────────────────────────────► GPS Power FET     │   │
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

1. Apply battery power → HT7333 provides 3.3V
2. ESP32 boots (GPIO2 pulled HIGH by default → GPS OFF)
3. Firmware initializes peripherals
4. Pull GPIO2 LOW to enable GPS when needed

### 10.3 Current Budget Summary

| Subsystem | Active Current | Sleep Current |
|-----------|----------------|---------------|
| ESP32 | 80 mA | 10 µA |
| ES7243E + Mic | 8 mA | 8 mA (always on) |
| L76K GPS | 29 mA | 0 mA (power gated) |
| SD Card | 100 mA (write) | 100 µA |
| **Total Recording** | **~217 mA** | - |
| **Total Sleep** | - | **~0.1 mA** |

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
| SHT30-DIS-B2.5kS | C78592 | Temp/humidity sensor, DFN-8 |
| HT7333-A | C21583 | 3.3V 250mA LDO, SOT-89 |
| L76K | C2838031 | GPS module with PPS |
| SI2301 | C2938372 | P-channel MOSFET, SOT-23 |
| TF-015 | C113206 | MicroSD card socket |
| AOM-5024L-HD-R | C3273706 | Electret mic (80dB SNR) |
| JST PH 2-pin | C295747 | Battery connector SMT |
| 2.2kΩ 0603 | C4190 | Mic bias resistor |
| 4.7kΩ 0603 | C23162 | I2C pull-up resistor |
| 100nF 0603 | C14663 | Decoupling capacitor |
| 1µF 0805 | C28323 | AC-coupling capacitor (AINLN, AINRN, AINRP) - Basic |
| 10µF 0805 | C15850 | Bulk capacitor (REFQ, REFP, VDDA) |
| 4.7µF 0805 | C1779 | Bulk capacitor |
