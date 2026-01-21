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

#ifndef GPS_H
#define GPS_H

#include <stdint.h>

/**
 * GPS data structure.
 */
struct GPSData {
    // Position
    double latitude;
    double longitude;
    float altitude;

    // Time (UTC)
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;

    // Date
    uint8_t day;
    uint8_t month;
    uint16_t year;

    // Status
    bool valid;
    uint8_t satellites;
    float hdop;

    // PPS timing
    uint32_t lastPpsTime;   // millis() when last PPS received
    bool ppsValid;
};

/**
 * Initialize GPS subsystem.
 * Sets up UART and PPS interrupt.
 *
 * @return true if initialization successful
 */
bool gpsInit();

/**
 * Start the GPS processing task.
 */
void gpsStart();

/**
 * Stop the GPS processing task.
 */
void gpsStop();

/**
 * Get current GPS data.
 */
GPSData gpsGetData();

/**
 * Check if GPS has a valid fix.
 */
bool gpsHasFix();

/**
 * Get Unix timestamp from GPS time.
 * @return Unix timestamp, or 0 if no valid time
 */
uint32_t gpsGetTimestamp();

/**
 * Send command to GPS module.
 * @param cmd PMTK command string (without $ and checksum)
 */
void gpsSendCommand(const char* cmd);

/**
 * Put GPS into standby mode.
 */
void gpsStandby();

/**
 * Wake GPS from standby.
 */
void gpsWake();

#endif // GPS_H
