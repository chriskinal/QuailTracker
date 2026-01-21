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
