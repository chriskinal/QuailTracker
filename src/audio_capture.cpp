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
#include <Wire.h>

static RingBuffer* s_audioBuffer = nullptr;
static TaskHandle_t s_audioTask = nullptr;
static volatile bool s_running = false;
static AudioStats s_stats = {0};

// ES7243E initialization table (from ESP-ADF)
// Format: {register, value}
static const uint8_t es7243e_init_table[][2] = {
    {0x01, 0x3A},
    {0x00, 0x80},  // Reset
    {0xF9, 0x00},  // Hidden register
    {0x04, 0x02},
    {0x04, 0x01},
    {0xF9, 0x01},  // Hidden register
    {0x00, 0x1E},
    {0x01, 0x00},
    {0x02, 0x00},
    {0x03, 0x20},
    {0x04, 0x01},
    {0x0D, 0x00},
    {0x05, 0x00},
    {0x06, 0x03},  // SCLK = MCLK/4
    {0x07, 0x00},  // LRCK = MCLK/256
    {0x08, 0xFF},  // LRCK = MCLK/256
    {0x09, 0x00},  // Standard I2S, 16-bit (was 0xCA for DSP mode)
    {0x0A, 0x85},
    {0x0B, 0x00},
    {0x0E, 0xBF},
    {0x0F, 0x80},
    {0x14, 0x0C},
    {0x15, 0x0C},
    {0x17, 0x02},
    {0x18, 0x26},
    {0x19, 0x77},
    {0x1A, 0xF4},
    {0x1B, 0x66},
    {0x1C, 0x44},
    {0x1E, 0x00},
    {0x1F, 0x0C},
    {0x20, 0x1A},  // PGA gain +30dB
    {0x21, 0x1A},  // PGA gain +30dB
    {0x16, 0x3F},
    {0x16, 0x00},
};

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

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);  // 100kHz I2C

    // Check if device responds
    Wire.beginTransmission(ES7243E_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("  ES7243E not found!");
        return false;
    }
    Serial.printf("  ES7243E found at 0x%02X\n", ES7243E_ADDR);

    // Apply initialization sequence from ESP-ADF
    int numRegs = sizeof(es7243e_init_table) / sizeof(es7243e_init_table[0]);
    Serial.printf("  Writing %d registers...\n", numRegs);

    for (int i = 0; i < numRegs; i++) {
        uint8_t reg = es7243e_init_table[i][0];
        uint8_t val = es7243e_init_table[i][1];
        if (!es7243eWriteReg(reg, val)) {
            Serial.printf("  Failed to write reg 0x%02X\n", reg);
        }
        // Small delay between writes
        delayMicroseconds(100);
    }

    // Read back a few key registers to verify
    Serial.println("  ES7243E register check:");
    Serial.printf("    [0x00] = 0x%02X (expect 0x1E)\n", es7243eReadReg(0x00));
    Serial.printf("    [0x06] = 0x%02X (expect 0x03)\n", es7243eReadReg(0x06));
    Serial.printf("    [0x09] = 0x%02X (expect 0x00 I2S)\n", es7243eReadReg(0x09));
    Serial.printf("    [0x20] = 0x%02X (expect 0x1A)\n", es7243eReadReg(0x20));

    Serial.println("  ES7243E configured");
    return true;
}

static bool i2sInit()
{
    Serial.println("Initializing I2S...");

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
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

    Serial.printf("  I2S configured: %d Hz, MCLK=%d Hz\n",
                  SAMPLE_RATE, SAMPLE_RATE * 256);
    return true;
}

static void audioTaskFunc(void* param)
{
    Serial.printf("Audio task started on core %d\n", xPortGetCoreID());

    int16_t dmaBuffer[DMA_BUF_LEN];
    size_t bytesRead;

    // Discard first few reads (may contain garbage)
    for (int i = 0; i < 3; i++) {
        i2s_read(I2S_PORT, dmaBuffer, sizeof(dmaBuffer), &bytesRead, portMAX_DELAY);
    }

    // Debug: print first few samples to verify I2S is working
    i2s_read(I2S_PORT, dmaBuffer, sizeof(dmaBuffer), &bytesRead, portMAX_DELAY);
    Serial.printf("I2S debug - bytes: %d, samples: %d %d %d %d %d %d %d %d\n",
        bytesRead, dmaBuffer[0], dmaBuffer[1], dmaBuffer[2], dmaBuffer[3],
        dmaBuffer[4], dmaBuffer[5], dmaBuffer[6], dmaBuffer[7]);

    while (s_running) {
        esp_err_t err = i2s_read(I2S_PORT, dmaBuffer, sizeof(dmaBuffer),
                                  &bytesRead, pdMS_TO_TICKS(100));

        if (err != ESP_OK) {
            s_stats.dmaErrors++;
            continue;
        }

        if (bytesRead > 0) {
            // Track peak level
            for (int i = 0; i < bytesRead / sizeof(int16_t); i++) {
                int16_t sample = dmaBuffer[i];
                if (sample < 0) sample = -sample;
                if (sample > s_stats.peakLevel) {
                    s_stats.peakLevel = sample;
                }
            }

            // Write to ring buffer
            size_t written = s_audioBuffer->write((uint8_t*)dmaBuffer, bytesRead);
            s_stats.samplesCaptures += bytesRead / sizeof(int16_t);

            if (written < bytesRead) {
                s_stats.bufferOverflows++;
            }
        }
    }

    Serial.println("Audio task stopped");
    vTaskDelete(NULL);
}

bool audioInit(RingBuffer* buffer)
{
    s_audioBuffer = buffer;

    // Start I2S FIRST to get MCLK running - ES7243E needs clock before config
    if (!i2sInit()) {
        return false;
    }

    // Give MCLK time to stabilize
    delay(50);

    // Now configure ES7243E with MCLK running
    if (!es7243eInit()) {
        Serial.println("ES7243E init failed - continuing anyway for testing");
        // Don't return false - allow testing without ES7243E
    }

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
    // Task will delete itself
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
