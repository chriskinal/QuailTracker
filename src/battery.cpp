#include "battery.h"
#include "config.h"

#include <Arduino.h>

static TaskHandle_t s_batteryTask = nullptr;
static volatile bool s_running = false;
static BatteryData s_batteryData = {0};

// ADC calibration
// ESP32 ADC is 12-bit (0-4095), with 11dB attenuation: 0-3.6V range
// With 1M/1M voltage divider: actual range is 0-7.2V
#define ADC_MAX         4095
#define ADC_VREF        3.3     // ADC reference voltage (approximate)

// Li-ion voltage to percentage lookup (conservative estimates)
// Based on typical 18650 discharge curve
static const float VOLTAGE_TABLE[] = {
    4.20,  // 100%
    4.10,  //  90%
    4.00,  //  80%
    3.90,  //  70%
    3.80,  //  60%
    3.70,  //  50%
    3.60,  //  40%
    3.50,  //  30%
    3.40,  //  20%
    3.30,  //  10%
    3.00   //   0%
};

static void batteryTaskFunc(void* param)
{
    Serial.printf("Battery monitor started on core %d\n", xPortGetCoreID());

    while (s_running) {
        s_batteryData.voltage = batteryReadVoltage();
        s_batteryData.percentage = batteryVoltageToPercent(s_batteryData.voltage);
        s_batteryData.lastReadTime = millis();

        // Determine battery level
        if (s_batteryData.voltage < BATTERY_CRITICAL_THRESHOLD) {
            s_batteryData.level = BATTERY_CRITICAL;
        } else if (s_batteryData.voltage < BATTERY_LOW_THRESHOLD) {
            s_batteryData.level = BATTERY_LOW;
        } else {
            s_batteryData.level = BATTERY_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
    }

    Serial.println("Battery monitor stopped");
    vTaskDelete(NULL);
}

void batteryInit()
{
    Serial.println("Initializing battery monitor...");

    // Configure ADC
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);  // 0-3.6V range

    // Do initial read
    s_batteryData.voltage = batteryReadVoltage();
    s_batteryData.percentage = batteryVoltageToPercent(s_batteryData.voltage);
    s_batteryData.level = BATTERY_OK;

    Serial.printf("  Initial voltage: %.2fV (%d%%)\n",
                  s_batteryData.voltage, s_batteryData.percentage);
}

void batteryStart()
{
    if (s_running) return;

    s_running = true;

    xTaskCreatePinnedToCore(
        batteryTaskFunc,
        "battery",
        STACK_BATTERY,
        NULL,
        PRIORITY_BATTERY,
        &s_batteryTask,
        CORE_SYSTEM
    );
}

void batteryStop()
{
    s_running = false;
    s_batteryTask = nullptr;
}

BatteryData batteryGetData()
{
    return s_batteryData;
}

float batteryReadVoltage()
{
    // Take multiple samples for better accuracy
    const int samples = 16;
    uint32_t sum = 0;

    for (int i = 0; i < samples; i++) {
        sum += analogRead(PIN_VBAT_ADC);
        delayMicroseconds(100);
    }

    float adcValue = (float)sum / samples;

    // Convert ADC reading to voltage
    // ADC voltage = (adcValue / ADC_MAX) * ADC_VREF
    // Battery voltage = ADC voltage * VBAT_DIVIDER
    float voltage = (adcValue / ADC_MAX) * ADC_VREF * VBAT_DIVIDER;

    return voltage;
}

uint8_t batteryVoltageToPercent(float voltage)
{
    // Clamp to valid range
    if (voltage >= VOLTAGE_TABLE[0]) return 100;
    if (voltage <= VOLTAGE_TABLE[10]) return 0;

    // Find position in table and interpolate
    for (int i = 0; i < 10; i++) {
        if (voltage >= VOLTAGE_TABLE[i + 1]) {
            // Linear interpolation between table entries
            float range = VOLTAGE_TABLE[i] - VOLTAGE_TABLE[i + 1];
            float offset = voltage - VOLTAGE_TABLE[i + 1];
            float fraction = offset / range;

            int percentLow = (10 - i - 1) * 10;
            int percentHigh = (10 - i) * 10;

            return percentLow + (uint8_t)(fraction * 10);
        }
    }

    return 0;
}
