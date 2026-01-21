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
