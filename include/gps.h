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
 * GPS power modes (L76K hardware control - PMTK not supported)
 */
enum GPSPowerMode {
    GPS_CONTINUOUS,  // Full operation (~25mA)
    GPS_STANDBY,     // WAKEUP pin low (~1mA, hot start 1-5s)
    GPS_BACKUP       // VCC off, V_BCKP on (~7µA, warm start 5-30s)
};

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
 * @param cmd NMEA command string (without $ and checksum)
 */
void gpsSendCommand(const char* cmd);

/**
 * Set GPS power mode (L76K hardware control).
 * @param mode GPS_CONTINUOUS, GPS_STANDBY, or GPS_BACKUP
 */
void gpsSetPowerMode(GPSPowerMode mode);

/**
 * Get current GPS power mode.
 */
GPSPowerMode gpsGetPowerMode();

#endif // GPS_H
