/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// QuailTracker Configuration
// =============================================================================

// -----------------------------------------------------------------------------
// Version
// -----------------------------------------------------------------------------
#define FIRMWARE_VERSION "0.1.36"

// -----------------------------------------------------------------------------
// Feature Flags
// -----------------------------------------------------------------------------
#define DISABLE_BLE     1   // Set to 1 to disable BLE for noise testing
#define USE_PDM_MIC     1   // Set to 1 for PDM digital mic (IM72D128/IM73D122)
                            // Set to 0 for I2S ADC (ES7243E)

// -----------------------------------------------------------------------------
// Core Assignment
// -----------------------------------------------------------------------------
#define CORE_AUDIO      1   // Dedicated to audio capture
#define CORE_SYSTEM     0   // GPS, SD writer, battery, BLE

// -----------------------------------------------------------------------------
// Task Priorities (higher = more important)
// -----------------------------------------------------------------------------
#define PRIORITY_AUDIO      configMAX_PRIORITIES - 1  // Highest
#define PRIORITY_SD_WRITER  configMAX_PRIORITIES - 2
#define PRIORITY_GPS        configMAX_PRIORITIES - 3
#define PRIORITY_BATTERY    2
#define PRIORITY_TEMP       2
#define PRIORITY_BLE        1

// -----------------------------------------------------------------------------
// Task Stack Sizes (words, not bytes)
// -----------------------------------------------------------------------------
#define STACK_AUDIO         4096
#define STACK_SD_WRITER     8192
#define STACK_GPS           4096
#define STACK_BATTERY       2048
#define STACK_TEMP          2048

// -----------------------------------------------------------------------------
// PDM Mic Pins (IM72D128 / IM73D122 digital MEMS)
// -----------------------------------------------------------------------------
#define PIN_PDM_CLK     14  // PDM clock output to mic (directly drives mic CLOCK pin)
#define PIN_PDM_DATA    32  // PDM data input from mic

// -----------------------------------------------------------------------------
// I2S Pins (ES7243E ADC) - only used if USE_PDM_MIC=0
// -----------------------------------------------------------------------------
#define PIN_I2S_MCLK    0   // Master clock to ES7243E
#define PIN_I2S_BCLK    14  // Bit clock
#define PIN_I2S_LRCK    15  // Word select (L/R clock)
#define PIN_I2S_DIN     32  // Data from ES7243E

// -----------------------------------------------------------------------------
// I2C Pins (ES7243E configuration) - only used if USE_PDM_MIC=0
// -----------------------------------------------------------------------------
#define PIN_I2C_SDA     21
#define PIN_I2C_SCL     22
#define ES7243E_ADDR    0x10  // I2C address (AD0=AD1=GND)

// -----------------------------------------------------------------------------
// SPI / SD Card Pins
// -----------------------------------------------------------------------------
#define PIN_SD_CS       5
#define PIN_SD_MOSI     23
#define PIN_SD_MISO     19
#define PIN_SD_SCK      18
#define PIN_SD_DET      34  // Card detect (LOW = card inserted, external 4.7k pull-up R6)

// -----------------------------------------------------------------------------
// GPS Pins (L76K via UART2)
// -----------------------------------------------------------------------------
#define PIN_GPS_TX      17  // ESP32 TX -> GPS RX
#define PIN_GPS_RX      16  // ESP32 RX <- GPS TX
#define PIN_GPS_PPS     4   // PPS pulse input
#define PIN_GPS_PWR_EN  25  // GPS VCC power control (HIGH = on via Q2 NPN + Q1 P-FET, LOW = off)
#define PIN_GPS_WAKEUP  26  // GPS WAKEUP control (HIGH = standby, LOW = continuous via Q3)
#define GPS_BAUD        9600

// GPS Power Modes (L76K does NOT support PMTK commands)
// - Continuous: PWR_EN=LOW, WAKEUP=LOW  (~25mA, immediate position)
// - Standby:    PWR_EN=LOW, WAKEUP=HIGH (~1mA, hot start 1-5s)
// - Backup:     PWR_EN=HIGH             (~7µA, warm start 5-30s, RTC maintained via V_BCKP)

// -----------------------------------------------------------------------------
// Battery Monitor
// -----------------------------------------------------------------------------
#define PIN_VBAT_ADC    35  // ADC1_CH7, voltage divider input
#define VBAT_DIVIDER    2.0 // 1M/1M divider ratio

// -----------------------------------------------------------------------------
// Temperature/Humidity Sensor (SHT30)
// -----------------------------------------------------------------------------
#define SHT30_ADDR      0x44    // I2C address (ADDR pin to GND)
#define TEMP_CHECK_INTERVAL_MS  30000   // Check every 30 seconds

// -----------------------------------------------------------------------------
// Audio Configuration
// -----------------------------------------------------------------------------
#define SAMPLE_RATE     48000
#define BITS_PER_SAMPLE 16
#define CHANNELS        1       // Mono
#define I2S_PORT        I2S_NUM_0

// I2S DMA buffers
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     1024    // Samples per buffer

// -----------------------------------------------------------------------------
// Ring Buffer Configuration
// -----------------------------------------------------------------------------
// Size in bytes - holds ~0.5 second of audio at 48kHz 16-bit mono
// Keep small enough to fit in available contiguous heap
#define RING_BUFFER_SIZE    (48000)  // ~0.5 seconds

// -----------------------------------------------------------------------------
// SD Card / Recording
// -----------------------------------------------------------------------------
#define SD_WRITE_CHUNK_SIZE 4096    // Bytes per SD write
#define WAV_HEADER_SIZE     44

// Recording file naming: /YYYYMMDD/HHMMSS_<station_id>.wav
#define STATION_ID          "QT001"
#define MAX_FILENAME_LEN    64

// -----------------------------------------------------------------------------
// Power Management
// -----------------------------------------------------------------------------
#define BATTERY_CHECK_INTERVAL_MS   60000   // Check every minute
#define BATTERY_LOW_THRESHOLD       2.8     // Volts (lowered for bench testing)
#define BATTERY_CRITICAL_THRESHOLD  2.6     // Volts (lowered for bench testing)

#endif // CONFIG_H
