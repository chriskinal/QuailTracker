#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdint.h>

/**
 * Temperature/humidity data from SHT30 sensor.
 */
struct TempHumidityData {
    float temperature;      // Celsius
    float humidity;         // Relative humidity %
    bool valid;             // True if sensor read succeeded
    uint32_t lastReadTime;  // millis() of last read
};

/**
 * Initialize temperature/humidity sensor.
 * @return true if sensor found and initialized
 */
bool tempInit();

/**
 * Start temperature monitoring task.
 */
void tempStart();

/**
 * Stop temperature monitoring task.
 */
void tempStop();

/**
 * Get current temperature/humidity data.
 */
TempHumidityData tempGetData();

/**
 * Read temperature/humidity immediately.
 * @return TempHumidityData with valid=true on success
 */
TempHumidityData tempReadNow();

/**
 * Check if sensor is present and responding.
 */
bool tempIsPresent();

#endif // TEMPERATURE_H
