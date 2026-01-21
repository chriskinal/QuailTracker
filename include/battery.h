#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

/**
 * Battery status levels.
 */
enum BatteryLevel {
    BATTERY_OK,
    BATTERY_LOW,
    BATTERY_CRITICAL
};

/**
 * Battery data structure.
 */
struct BatteryData {
    float voltage;
    uint8_t percentage;
    BatteryLevel level;
    uint32_t lastReadTime;
};

/**
 * Initialize battery monitor.
 */
void batteryInit();

/**
 * Start battery monitoring task.
 */
void batteryStart();

/**
 * Stop battery monitoring task.
 */
void batteryStop();

/**
 * Get current battery data.
 */
BatteryData batteryGetData();

/**
 * Read battery voltage immediately.
 * @return Battery voltage in volts
 */
float batteryReadVoltage();

/**
 * Get battery percentage estimate.
 * @param voltage Battery voltage
 * @return Percentage (0-100)
 */
uint8_t batteryVoltageToPercent(float voltage);

#endif // BATTERY_H
