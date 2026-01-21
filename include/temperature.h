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
