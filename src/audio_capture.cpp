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
#include <driver/i2c.h>
#include <Wire.h>

static RingBuffer* s_audioBuffer = nullptr;
static TaskHandle_t s_audioTask = nullptr;
static volatile bool s_running = false;
static AudioStats s_stats = {0};

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

// ESP-IDF based I2C write with explicit control
static esp_err_t es7243eWriteRegIDF(uint8_t reg, uint8_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES7243E_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static uint8_t es7243eReadReg(uint8_t reg)
{
    Wire.beginTransmission(ES7243E_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(ES7243E_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

static bool es7243eWriteRegVerify(uint8_t reg, uint8_t value)
{
    if (!es7243eWriteReg(reg, value)) {
        return false;
    }
    delayMicroseconds(500);
    uint8_t readback = es7243eReadReg(reg);
    if (readback != value) {
        Serial.printf("  Reg 0x%02X: wrote 0x%02X, read 0x%02X\n", reg, value, readback);
        return false;
    }
    return true;
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

    // Verify chip identity (from datasheet page 26)
    uint8_t chipId1 = es7243eReadReg(0xFD);  // Should be 0x7A
    uint8_t chipId2 = es7243eReadReg(0xFE);  // Should be 0x43
    uint8_t chipVer = es7243eReadReg(0xFF);  // Should be 0x00
    Serial.printf("  Chip ID: 0x%02X 0x%02X ver=0x%02X", chipId1, chipId2, chipVer);
    if (chipId1 == 0x7A && chipId2 == 0x43) {
        Serial.println(" (ES7243E confirmed!)");
    } else {
        Serial.println(" (NOT ES7243E - wrong chip ID!)");
        Serial.println("  Expected: 0x7A 0x43");
        return false;
    }

    // Test write capability
    Serial.println("  Testing register write...");
    Serial.printf("    Before: 0x01=0x%02X\n", es7243eReadReg(0x01));
    es7243eWriteReg(0x01, 0x3A);
    delay(5);
    uint8_t testVal = es7243eReadReg(0x01);
    Serial.printf("    After:  0x01=0x%02X (expect 0x3A)\n", testVal);

    if (testVal != 0x3A) {
        Serial.println("  *** I2C WRITES STILL FAILING ***");
        Serial.println("  Add 4.7k pull-up resistors to SDA and SCL!");
        return false;
    }
    Serial.println("  I2C writes working!");

    // If still failing, the chip may have hardware write protection
    // or there's an issue with the SDA line during writes

    // Use the official ESP-ADF initialization sequence
    Serial.println("\n  Using ESP-ADF init sequence:");

    // Phase 1: Initial startup
    Serial.println("  Phase 1: Initial startup");
    es7243eWriteReg(0x01, 0x3A);
    es7243eWriteReg(0x00, 0x80);
    es7243eWriteReg(0xF9, 0x00);
    es7243eWriteReg(0x04, 0x02);
    es7243eWriteReg(0x04, 0x01);
    es7243eWriteReg(0xF9, 0x01);
    es7243eWriteReg(0x00, 0x1E);
    es7243eWriteReg(0x01, 0x00);
    delay(10);

    // Phase 2: Clock and format configuration
    Serial.println("  Phase 2: Clock configuration");
    es7243eWriteReg(0x02, 0x00);
    es7243eWriteReg(0x03, 0x20);
    es7243eWriteReg(0x04, 0x01);
    es7243eWriteReg(0x0D, 0x00);
    es7243eWriteReg(0x05, 0x00);
    es7243eWriteReg(0x06, 0x03);  // SCLK=MCLK/4
    es7243eWriteReg(0x07, 0x00);  // LRCK divider low
    es7243eWriteReg(0x08, 0xFF);  // LRCK divider high

    // Phase 3: Audio path configuration
    Serial.println("  Phase 3: Audio path");
    es7243eWriteReg(0x09, 0xCA);
    es7243eWriteReg(0x0A, 0x85);
    es7243eWriteReg(0x0B, 0x00);  // I2S format, 24-bit, unmuted
    es7243eWriteReg(0x0E, 0xBF);  // ADC volume 0dB
    es7243eWriteReg(0x0F, 0x80);

    // Phase 4: Analog configuration
    Serial.println("  Phase 4: Analog configuration");
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
    Serial.println("  Phase 5: PGA gain");
    es7243eWriteReg(0x20, 0x1A);  // PGA +30dB
    es7243eWriteReg(0x21, 0x1A);  // PGA +30dB

    // Phase 6: Final startup - enter slave mode
    Serial.println("  Phase 6: Final startup");
    es7243eWriteReg(0x00, 0x80);  // Slave mode
    es7243eWriteReg(0x01, 0x3A);
    es7243eWriteReg(0x16, 0x3F);  // Power sequence
    es7243eWriteReg(0x16, 0x00);  // Power up analog
    delay(100);

    // Phase 7: Activate codec (from ESP-ADF es7243e_adc_ctrl_state_active)
    Serial.println("  Phase 7: Activate codec");
    es7243eWriteReg(0xF9, 0x00);
    es7243eWriteReg(0x04, 0x01);
    es7243eWriteReg(0x17, 0x01);  // Changed from 0x02
    es7243eWriteReg(0x20, 0x1F);  // Max PGA gain (+33.5dB)
    es7243eWriteReg(0x21, 0x1F);  // Max PGA gain
    es7243eWriteReg(0x00, 0x80);
    es7243eWriteReg(0x01, 0x3A);
    es7243eWriteReg(0x16, 0x3F);
    es7243eWriteReg(0x16, 0x00);
    delay(50);

    // Final register check (ESP-ADF expected values after activation)
    Serial.println("\n  Final register state:");
    Serial.printf("    [0x00] = 0x%02X (need 0x80)\n", es7243eReadReg(0x00));
    Serial.printf("    [0x01] = 0x%02X (need 0x3A)\n", es7243eReadReg(0x01));
    Serial.printf("    [0x04] = 0x%02X (need 0x01)\n", es7243eReadReg(0x04));
    Serial.printf("    [0x06] = 0x%02X (need 0x03 SCLK div)\n", es7243eReadReg(0x06));
    Serial.printf("    [0x0B] = 0x%02X (need 0x00 I2S 24-bit)\n", es7243eReadReg(0x0B));
    Serial.printf("    [0x16] = 0x%02X (need 0x00 analog on)\n", es7243eReadReg(0x16));
    Serial.printf("    [0x17] = 0x%02X (need 0x01 after activate)\n", es7243eReadReg(0x17));
    Serial.printf("    [0x20] = 0x%02X (need 0x1F max PGA gain)\n", es7243eReadReg(0x20));
    Serial.printf("    [0xF9] = 0x%02X (need 0x00 after activate)\n", es7243eReadReg(0xF9));
    Serial.printf("    [0xFC] = 0x%02X (CSM: bits5-4=state, bit0=automute)\n", es7243eReadReg(0xFC));

    // Check if writes worked
    if (es7243eReadReg(0x01) == 0x3A && es7243eReadReg(0x00) == 0x80) {
        Serial.println("  ES7243E configured successfully!");
        return true;
    } else {
        Serial.println("\n  *** WRITES FAILED ***");
        Serial.println("  Check hardware: SDA line, pull-ups, connections");
        return false;
    }
}

static bool i2sInit()
{
    Serial.println("Initializing I2S...");

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // ES7243E outputs 24-bit in 32-bit frames
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Capture BOTH channels
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

// Static buffers to avoid stack overflow (moved from task stack to .bss)
static int32_t s_dmaBuffer[DMA_BUF_LEN];  // 32-bit for 24-bit audio in 32-bit frames
static int16_t s_monoBuffer[DMA_BUF_LEN / 2];

static void audioTaskFunc(void* param)
{
    Serial.printf("Audio task started on core %d\n", xPortGetCoreID());

    size_t bytesRead;

    // Discard first few reads (may contain garbage)
    for (int i = 0; i < 3; i++) {
        i2s_read(I2S_PORT, s_dmaBuffer, sizeof(s_dmaBuffer), &bytesRead, portMAX_DELAY);
    }

    // Debug: print first few samples to verify I2S is working
    // With stereo capture, samples are interleaved: L0, R0, L1, R1, L2, R2...
    i2s_read(I2S_PORT, s_dmaBuffer, sizeof(s_dmaBuffer), &bytesRead, portMAX_DELAY);
    Serial.printf("I2S debug - bytes: %d\n", bytesRead);
    Serial.printf("  Left  channel: %ld %ld %ld %ld\n",
        (long)s_dmaBuffer[0], (long)s_dmaBuffer[2], (long)s_dmaBuffer[4], (long)s_dmaBuffer[6]);
    Serial.printf("  Right channel: %ld %ld %ld %ld\n",
        (long)s_dmaBuffer[1], (long)s_dmaBuffer[3], (long)s_dmaBuffer[5], (long)s_dmaBuffer[7]);
    Serial.printf("  Hex: 0x%08lX 0x%08lX 0x%08lX 0x%08lX\n",
        (unsigned long)s_dmaBuffer[0], (unsigned long)s_dmaBuffer[1],
        (unsigned long)s_dmaBuffer[2], (unsigned long)s_dmaBuffer[3]);

    // Check if ANY samples are non-zero
    int nonZeroCount = 0;
    for (int i = 0; i < bytesRead / sizeof(int32_t); i++) {
        if (s_dmaBuffer[i] != 0) nonZeroCount++;
    }
    Serial.printf("  Non-zero samples: %d / %d\n", nonZeroCount, (int)(bytesRead / sizeof(int32_t)));

    static int debugCounter = 0;

    while (s_running) {
        esp_err_t err = i2s_read(I2S_PORT, s_dmaBuffer, sizeof(s_dmaBuffer),
                                  &bytesRead, pdMS_TO_TICKS(100));

        if (err != ESP_OK) {
            s_stats.dmaErrors++;
            continue;
        }

        if (bytesRead > 0) {
            // Convert 32-bit stereo to 16-bit mono (left channel only)
            // ES7243E outputs 24-bit data left-justified in 32-bit frame
            int numStereoSamples = bytesRead / (sizeof(int32_t) * 2);
            int monoIdx = 0;

            // Debug: print samples every ~1 second during recording
            if (debugCounter++ % 100 == 0) {
                // Serial.printf("Recording: raw32[0]=0x%08lX, >>16=%d, samples=%d\n", (unsigned long)s_dmaBuffer[0], (int)(int16_t)(s_dmaBuffer[0] >> 16), numStereoSamples);
            }

            for (int i = 0; i < numStereoSamples; i++) {
                // Left channel is at even indices, extract upper 16 bits of 24-bit data
                int32_t sample32 = s_dmaBuffer[i * 2];  // Left channel
                int16_t sample16 = (int16_t)(sample32 >> 16);  // Take upper 16 bits
                s_monoBuffer[monoIdx++] = sample16;

                // Track peak level
                int16_t absSample = sample16 < 0 ? -sample16 : sample16;
                if (absSample > s_stats.peakLevel) {
                    s_stats.peakLevel = absSample;
                }
            }

            // Write 16-bit mono to ring buffer
            size_t bytesToWrite = monoIdx * sizeof(int16_t);
            size_t written = s_audioBuffer->write((uint8_t*)s_monoBuffer, bytesToWrite);
            s_stats.samplesCaptures += monoIdx;

            if (written < bytesToWrite) {
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
