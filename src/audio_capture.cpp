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

// ES7243E register addresses
#define ES7243E_REG_CHIP_POWER  0x00
#define ES7243E_REG_MODULATOR   0x01
#define ES7243E_REG_ADC_CTRL    0x02
#define ES7243E_REG_ANALOG      0x03
#define ES7243E_REG_CLOCK       0x04
#define ES7243E_REG_SDP         0x05  // Serial Data Port
#define ES7243E_REG_BCLK_DIV    0x06
#define ES7243E_REG_MCLK_DIV    0x07
#define ES7243E_REG_ADC_VOL     0x08
#define ES7243E_REG_PGA_GAIN    0x09

static bool es7243eWriteReg(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(ES7243E_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
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
    Serial.println("  ES7243E found at 0x10");

    // Reset and configure ES7243E
    // These register values may need adjustment based on testing
    es7243eWriteReg(ES7243E_REG_CHIP_POWER, 0x01);  // Power up
    delay(10);
    es7243eWriteReg(ES7243E_REG_CLOCK, 0x00);       // MCLK from external
    es7243eWriteReg(ES7243E_REG_SDP, 0x00);         // I2S, 16-bit, slave mode
    es7243eWriteReg(ES7243E_REG_MODULATOR, 0x00);   // Normal operation
    es7243eWriteReg(ES7243E_REG_ADC_CTRL, 0x00);    // Normal ADC operation
    es7243eWriteReg(ES7243E_REG_PGA_GAIN, 0x1F);    // PGA gain (adjust as needed)
    es7243eWriteReg(ES7243E_REG_ADC_VOL, 0x00);     // 0dB digital volume

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

    if (!es7243eInit()) {
        Serial.println("ES7243E init failed - continuing anyway for testing");
        // Don't return false - allow testing without ES7243E
    }

    if (!i2sInit()) {
        return false;
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
