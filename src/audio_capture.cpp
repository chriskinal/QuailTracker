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

#include "audio_capture.h"
#include "config.h"

#include <Arduino.h>
#include <driver/i2s.h>

#if !USE_PDM_MIC
#include <driver/i2c.h>
#include <Wire.h>
#endif

static RingBuffer* s_audioBuffer = nullptr;
static TaskHandle_t s_audioTask = nullptr;
static volatile bool s_running = false;
static AudioStats s_stats = {0};

// =============================================================================
// ES7243E I2S ADC Functions (only when USE_PDM_MIC=0)
// =============================================================================
#if !USE_PDM_MIC

static bool es7243eWriteReg(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(ES7243E_ADDR);
    Wire.write(reg);
    Wire.write(value);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        Serial.printf("  I2C write failed reg=0x%02X err=%d\n", reg, err);
        return false;
    }
    return true;
}

static uint8_t es7243eReadReg(uint8_t reg)
{
    Wire.beginTransmission(ES7243E_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(ES7243E_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

static bool es7243eInit()
{
    Serial.println("Initializing ES7243E...");

    // Enable internal pull-ups on I2C pins (external pull-ups missing from PCB!)
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delay(10);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);  // 100kHz I2C

    Serial.println("  NOTE: Using internal pull-ups (add 4.7k external pull-ups for reliability!)");

    // Check if device responds
    Wire.beginTransmission(ES7243E_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("  ES7243E not found!");
        return false;
    }
    Serial.printf("  ES7243E found at 0x%02X\n", ES7243E_ADDR);

    // Verify chip identity
    uint8_t chipId1 = es7243eReadReg(0xFD);
    uint8_t chipId2 = es7243eReadReg(0xFE);
    Serial.printf("  Chip ID: 0x%02X 0x%02X", chipId1, chipId2);
    if (chipId1 == 0x7A && chipId2 == 0x43) {
        Serial.println(" (ES7243E confirmed!)");
    } else {
        Serial.println(" (NOT ES7243E - wrong chip ID!)");
        return false;
    }

    // ESP-ADF initialization sequence
    Serial.println("  Configuring ES7243E...");

    // Phase 1: Initial startup
    es7243eWriteReg(0x01, 0x3A);
    es7243eWriteReg(0x00, 0x80);
    es7243eWriteReg(0xF9, 0x00);
    es7243eWriteReg(0x04, 0x02);
    es7243eWriteReg(0x04, 0x01);
    es7243eWriteReg(0xF9, 0x01);
    es7243eWriteReg(0x00, 0x1E);
    es7243eWriteReg(0x01, 0x00);
    delay(10);

    // Phase 2: Clock configuration
    es7243eWriteReg(0x02, 0x00);
    es7243eWriteReg(0x03, 0x20);
    es7243eWriteReg(0x04, 0x01);
    es7243eWriteReg(0x0D, 0x00);
    es7243eWriteReg(0x05, 0x00);
    es7243eWriteReg(0x06, 0x03);
    es7243eWriteReg(0x07, 0x00);
    es7243eWriteReg(0x08, 0xFF);

    // Phase 3: Audio path
    es7243eWriteReg(0x09, 0xCA);
    es7243eWriteReg(0x0A, 0x85);
    es7243eWriteReg(0x0B, 0x00);
    es7243eWriteReg(0x0E, 0xBF);
    es7243eWriteReg(0x0F, 0x80);

    // Phase 4: Analog configuration
    es7243eWriteReg(0x14, 0x0C);
    es7243eWriteReg(0x15, 0x0C);
    es7243eWriteReg(0x17, 0x02);
    es7243eWriteReg(0x18, 0x26);
    es7243eWriteReg(0x19, 0x77);
    es7243eWriteReg(0x1A, 0xF4);
    es7243eWriteReg(0x1B, 0x66);
    es7243eWriteReg(0x1C, 0x44);
    es7243eWriteReg(0x1E, 0x00);
    es7243eWriteReg(0x1F, 0x0C);

    // Phase 5: PGA gain
    es7243eWriteReg(0x20, 0x1A);
    es7243eWriteReg(0x21, 0x1A);

    // Phase 6: Final startup
    es7243eWriteReg(0x00, 0x80);
    es7243eWriteReg(0x01, 0x3A);
    es7243eWriteReg(0x16, 0x3F);
    es7243eWriteReg(0x16, 0x00);
    delay(100);

    // Phase 7: Activate
    es7243eWriteReg(0xF9, 0x00);
    es7243eWriteReg(0x04, 0x01);
    es7243eWriteReg(0x17, 0x01);
    es7243eWriteReg(0x20, 0x1F);
    es7243eWriteReg(0x21, 0x1F);
    es7243eWriteReg(0x00, 0x80);
    es7243eWriteReg(0x01, 0x3A);
    es7243eWriteReg(0x16, 0x3F);
    es7243eWriteReg(0x16, 0x00);
    delay(50);

    Serial.println("  ES7243E configured!");
    return true;
}

static bool i2sInitStandard()
{
    Serial.println("Initializing I2S (standard mode for ES7243E)...");

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = true,
        .tx_desc_auto_clear = false,
        .fixed_mclk = SAMPLE_RATE * 256
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = PIN_I2S_MCLK,
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_LRCK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_DIN
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("  I2S driver install failed: %d\n", err);
        return false;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("  I2S pin config failed: %d\n", err);
        return false;
    }

    Serial.printf("  I2S configured: %d Hz, MCLK=%d Hz\n", SAMPLE_RATE, SAMPLE_RATE * 256);
    return true;
}

#endif // !USE_PDM_MIC

// =============================================================================
// PDM Mic Functions (when USE_PDM_MIC=1)
// =============================================================================
#if USE_PDM_MIC

static bool i2sInitPDM()
{
    Serial.println("Initializing I2S (PDM mode for digital MEMS mic)...");

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // Single mic, SELECT pin to GND
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = true,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    // PDM mode: WS pin is used as PDM clock output, DATA_IN is PDM data
    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_PIN_NO_CHANGE,
        .ws_io_num = PIN_PDM_CLK,       // PDM clock output
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_PDM_DATA     // PDM data input
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("  I2S PDM driver install failed: %d\n", err);
        return false;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("  I2S PDM pin config failed: %d\n", err);
        return false;
    }

    // Set PDM clock - the mic needs a clock in the range of ~1-5 MHz
    // ESP32 PDM RX generates CLK = sample_rate * PDM_decimation
    // Default decimation is 64, so 48000 * 64 = 3.072 MHz (perfect for IM72D128/IM73D122)

    Serial.printf("  PDM configured: %d Hz sample rate\n", SAMPLE_RATE);
    Serial.printf("  PDM CLK: GPIO%d, DATA: GPIO%d\n", PIN_PDM_CLK, PIN_PDM_DATA);
    return true;
}

#endif // USE_PDM_MIC

// =============================================================================
// Common Audio Functions
// =============================================================================

// Static buffers to avoid stack overflow
#if USE_PDM_MIC
static int16_t s_dmaBuffer[DMA_BUF_LEN];  // PDM gives 16-bit directly
#else
static int32_t s_dmaBuffer[DMA_BUF_LEN];  // I2S gives 32-bit frames
static int16_t s_monoBuffer[DMA_BUF_LEN / 2];
#endif

static void audioTaskFunc(void* param)
{
    Serial.printf("Audio task started on core %d\n", xPortGetCoreID());

    size_t bytesRead;

    // Discard first few reads (may contain garbage)
    for (int i = 0; i < 3; i++) {
        i2s_read(I2S_PORT, s_dmaBuffer, sizeof(s_dmaBuffer), &bytesRead, portMAX_DELAY);
    }

    // Debug: print first samples
    i2s_read(I2S_PORT, s_dmaBuffer, sizeof(s_dmaBuffer), &bytesRead, portMAX_DELAY);
    Serial.printf("I2S debug - bytes read: %d\n", bytesRead);

#if USE_PDM_MIC
    Serial.printf("  PDM samples: %d %d %d %d\n",
        s_dmaBuffer[0], s_dmaBuffer[1], s_dmaBuffer[2], s_dmaBuffer[3]);
#else
    Serial.printf("  I2S samples (32-bit): 0x%08lX 0x%08lX\n",
        (unsigned long)s_dmaBuffer[0], (unsigned long)s_dmaBuffer[1]);
#endif

    while (s_running) {
        esp_err_t err = i2s_read(I2S_PORT, s_dmaBuffer, sizeof(s_dmaBuffer),
                                  &bytesRead, pdMS_TO_TICKS(100));

        if (err != ESP_OK) {
            s_stats.dmaErrors++;
            continue;
        }

        if (bytesRead > 0) {
#if USE_PDM_MIC
            // PDM mode: data is already 16-bit mono
            int numSamples = bytesRead / sizeof(int16_t);

            // Track peak level
            for (int i = 0; i < numSamples; i++) {
                int16_t sample = s_dmaBuffer[i];
                int16_t absSample = sample < 0 ? -sample : sample;
                if (absSample > s_stats.peakLevel) {
                    s_stats.peakLevel = absSample;
                }
            }

            // Write directly to ring buffer
            size_t written = s_audioBuffer->write((uint8_t*)s_dmaBuffer, bytesRead);
            s_stats.samplesCaptures += numSamples;

            if (written < bytesRead) {
                s_stats.bufferOverflows++;
            }
#else
            // I2S mode: convert 32-bit stereo to 16-bit mono
            int numStereoSamples = bytesRead / (sizeof(int32_t) * 2);
            int monoIdx = 0;

            for (int i = 0; i < numStereoSamples; i++) {
                int32_t sample32 = s_dmaBuffer[i * 2];  // Left channel
                int16_t sample16 = (int16_t)(sample32 >> 16);
                s_monoBuffer[monoIdx++] = sample16;

                int16_t absSample = sample16 < 0 ? -sample16 : sample16;
                if (absSample > s_stats.peakLevel) {
                    s_stats.peakLevel = absSample;
                }
            }

            size_t bytesToWrite = monoIdx * sizeof(int16_t);
            size_t written = s_audioBuffer->write((uint8_t*)s_monoBuffer, bytesToWrite);
            s_stats.samplesCaptures += monoIdx;

            if (written < bytesToWrite) {
                s_stats.bufferOverflows++;
            }
#endif
        }
    }

    Serial.println("Audio task stopped");
    vTaskDelete(NULL);
}

bool audioInit(RingBuffer* buffer)
{
    s_audioBuffer = buffer;

#if USE_PDM_MIC
    Serial.println("\n=== PDM Digital Mic Mode ===");
    Serial.println("Using IM72D128/IM73D122 digital MEMS microphone");

    if (!i2sInitPDM()) {
        return false;
    }
#else
    Serial.println("\n=== I2S ADC Mode ===");
    Serial.println("Using ES7243E I2S ADC");

    // Start I2S first to get MCLK running
    if (!i2sInitStandard()) {
        return false;
    }

    // Give MCLK time to stabilize
    delay(50);

    // Configure ES7243E
    if (!es7243eInit()) {
        Serial.println("ES7243E init failed - continuing anyway for testing");
    }
#endif

    return true;
}

void audioStart()
{
    if (s_running) return;

    s_running = true;
    audioResetStats();

    xTaskCreatePinnedToCore(
        audioTaskFunc,
        "audio",
        STACK_AUDIO,
        NULL,
        PRIORITY_AUDIO,
        &s_audioTask,
        CORE_AUDIO
    );
}

void audioStop()
{
    s_running = false;
    s_audioTask = nullptr;
}

bool audioIsRunning()
{
    return s_running;
}

AudioStats audioGetStats()
{
    return s_stats;
}

void audioResetStats()
{
    s_stats.samplesCaptures = 0;
    s_stats.bufferOverflows = 0;
    s_stats.dmaErrors = 0;
    s_stats.peakLevel = 0;
}
