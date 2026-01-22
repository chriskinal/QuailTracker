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

// ES7243E register addresses (from datasheet)
#define ES7243E_REG_RESET       0x00  // Reset control
#define ES7243E_REG_CLK_MGR1    0x01  // Clock manager 1
#define ES7243E_REG_CLK_MGR2    0x02  // Clock manager 2
#define ES7243E_REG_CLK_MGR3    0x03  // Clock manager 3
#define ES7243E_REG_CLK_MGR4    0x04  // Clock manager 4
#define ES7243E_REG_CLK_MGR5    0x05  // Clock manager 5
#define ES7243E_REG_CLK_MGR6    0x06  // ADC OSR / clock divider
#define ES7243E_REG_CLK_MGR7    0x07  // BCLK divider
#define ES7243E_REG_CLK_MGR8    0x08  // LRCK divider high
#define ES7243E_REG_SDP         0x09  // Serial data port format
#define ES7243E_REG_ADC_CTRL    0x0A  // ADC control
#define ES7243E_REG_ADC_VOL     0x0B  // ADC volume
#define ES7243E_REG_ADC_RAMPRATE 0x0C // Volume ramp rate
#define ES7243E_REG_ANALOG1     0x0D  // Analog control 1
#define ES7243E_REG_ANALOG2     0x0E  // Analog control 2 (PGA gain)
#define ES7243E_REG_ANALOG3     0x0F  // Analog control 3
#define ES7243E_REG_ANALOG4     0x10  // Analog control 4

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

    // ES7243E initialization sequence for:
    // - MCLK = 12.288 MHz (256 * 48kHz)
    // - Sample rate = 48 kHz
    // - I2S slave mode, 16-bit

    // Step 1: Software reset
    es7243eWriteReg(ES7243E_REG_RESET, 0x80);
    delay(10);
    es7243eWriteReg(ES7243E_REG_RESET, 0x00);
    delay(10);

    // Step 2: Clock configuration
    // Clock manager - select MCLK as clock source, slave mode
    es7243eWriteReg(ES7243E_REG_CLK_MGR1, 0x3A);  // Power up analog, slave mode
    es7243eWriteReg(ES7243E_REG_CLK_MGR2, 0x00);  // Single speed mode
    es7243eWriteReg(ES7243E_REG_CLK_MGR3, 0x00);  // MCLK input
    es7243eWriteReg(ES7243E_REG_CLK_MGR4, 0x02);  // ADC clock select
    es7243eWriteReg(ES7243E_REG_CLK_MGR5, 0x00);  // Clock divider
    es7243eWriteReg(ES7243E_REG_CLK_MGR6, 0x00);  // OSR = 256
    es7243eWriteReg(ES7243E_REG_CLK_MGR7, 0x00);  // BCLK from MCLK
    es7243eWriteReg(ES7243E_REG_CLK_MGR8, 0x00);  // LRCK divider

    // Step 3: I2S format configuration
    // SDP: I2S standard, 16-bit word length
    es7243eWriteReg(ES7243E_REG_SDP, 0x0C);  // I2S format, 16-bit

    // Step 4: ADC configuration
    es7243eWriteReg(ES7243E_REG_ADC_CTRL, 0x00);  // ADC normal operation
    es7243eWriteReg(ES7243E_REG_ADC_VOL, 0x00);   // 0dB digital volume

    // Step 5: Analog configuration
    es7243eWriteReg(ES7243E_REG_ANALOG1, 0x00);   // Analog power on
    es7243eWriteReg(ES7243E_REG_ANALOG2, 0x1F);   // PGA gain = +31.5dB (max)
    es7243eWriteReg(ES7243E_REG_ANALOG3, 0x00);   // Normal operation
    es7243eWriteReg(ES7243E_REG_ANALOG4, 0x00);   // Normal operation

    // Step 6: Start ADC
    es7243eWriteReg(ES7243E_REG_RESET, 0x01);     // Start ADC

    // Read back registers to verify configuration
    Serial.println("  ES7243E register dump:");
    for (uint8_t reg = 0; reg <= 0x10; reg++) {
        uint8_t val = es7243eReadReg(reg);
        Serial.printf("    [0x%02X] = 0x%02X\n", reg, val);
    }

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
