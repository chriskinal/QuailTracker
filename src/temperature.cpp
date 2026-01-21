#include "temperature.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>

static TaskHandle_t s_tempTask = nullptr;
static volatile bool s_running = false;
static TempHumidityData s_data = {0};
static bool s_sensorPresent = false;

// SHT30 Commands (single shot, high repeatability)
#define SHT30_CMD_MEASURE_HIGH  0x2400  // High repeatability, no clock stretch
#define SHT30_CMD_SOFT_RESET    0x30A2
#define SHT30_CMD_STATUS        0xF32D

static bool sht30WriteCmd(uint16_t cmd)
{
    Wire.beginTransmission(SHT30_ADDR);
    Wire.write(cmd >> 8);
    Wire.write(cmd & 0xFF);
    return Wire.endTransmission() == 0;
}

static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    // CRC-8 polynomial 0x31 (x^8 + x^5 + x^4 + 1)
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static void tempTaskFunc(void* param)
{
    Serial.printf("Temperature monitor started on core %d\n", xPortGetCoreID());

    while (s_running) {
        TempHumidityData reading = tempReadNow();
        if (reading.valid) {
            s_data = reading;
        }

        vTaskDelay(pdMS_TO_TICKS(TEMP_CHECK_INTERVAL_MS));
    }

    Serial.println("Temperature monitor stopped");
    vTaskDelete(NULL);
}

bool tempInit()
{
    Serial.println("Initializing SHT30 temperature sensor...");

    // Wire should already be initialized by audio_capture for ES7243E
    // But we'll make sure it's started
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // Try soft reset
    if (!sht30WriteCmd(SHT30_CMD_SOFT_RESET)) {
        Serial.println("  SHT30 not found!");
        s_sensorPresent = false;
        return false;
    }

    delay(10);  // Wait for reset

    // Verify by reading status
    if (!sht30WriteCmd(SHT30_CMD_STATUS)) {
        Serial.println("  SHT30 status read failed!");
        s_sensorPresent = false;
        return false;
    }

    Wire.requestFrom((uint8_t)SHT30_ADDR, (uint8_t)3);
    if (Wire.available() < 3) {
        Serial.println("  SHT30 status response failed!");
        s_sensorPresent = false;
        return false;
    }

    // Read and discard status (just checking communication)
    Wire.read();
    Wire.read();
    Wire.read();

    s_sensorPresent = true;

    // Do initial read
    TempHumidityData reading = tempReadNow();
    if (reading.valid) {
        s_data = reading;
        Serial.printf("  Temperature: %.1f C, Humidity: %.1f%%\n",
                      s_data.temperature, s_data.humidity);
    } else {
        Serial.println("  Initial read failed, sensor may still work");
    }

    Serial.println("SHT30 OK");
    return true;
}

void tempStart()
{
    if (s_running || !s_sensorPresent) return;

    s_running = true;

    xTaskCreatePinnedToCore(
        tempTaskFunc,
        "temp",
        STACK_TEMP,
        NULL,
        PRIORITY_TEMP,
        &s_tempTask,
        CORE_SYSTEM
    );
}

void tempStop()
{
    s_running = false;
    s_tempTask = nullptr;
}

TempHumidityData tempGetData()
{
    return s_data;
}

TempHumidityData tempReadNow()
{
    TempHumidityData result = {0};
    result.valid = false;

    if (!s_sensorPresent) {
        return result;
    }

    // Send measurement command
    if (!sht30WriteCmd(SHT30_CMD_MEASURE_HIGH)) {
        return result;
    }

    // Wait for measurement (high repeatability takes ~15ms)
    delay(20);

    // Read 6 bytes: temp MSB, temp LSB, temp CRC, hum MSB, hum LSB, hum CRC
    Wire.requestFrom((uint8_t)SHT30_ADDR, (uint8_t)6);
    if (Wire.available() < 6) {
        return result;
    }

    uint8_t data[6];
    for (int i = 0; i < 6; i++) {
        data[i] = Wire.read();
    }

    // Verify CRCs
    if (crc8(&data[0], 2) != data[2] || crc8(&data[3], 2) != data[5]) {
        Serial.println("SHT30 CRC error");
        return result;
    }

    // Convert raw values
    uint16_t rawTemp = (data[0] << 8) | data[1];
    uint16_t rawHum = (data[3] << 8) | data[4];

    // Temperature: -45 + 175 * rawTemp / 65535
    result.temperature = -45.0f + 175.0f * (float)rawTemp / 65535.0f;

    // Humidity: 100 * rawHum / 65535
    result.humidity = 100.0f * (float)rawHum / 65535.0f;

    // Clamp humidity to valid range
    if (result.humidity > 100.0f) result.humidity = 100.0f;
    if (result.humidity < 0.0f) result.humidity = 0.0f;

    result.valid = true;
    result.lastReadTime = millis();

    return result;
}

bool tempIsPresent()
{
    return s_sensorPresent;
}
