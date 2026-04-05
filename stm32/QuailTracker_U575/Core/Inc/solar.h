/*
 * solar.h — Sunrise/sunset calculator (NOAA simplified algorithm)
 *
 * Uses latitude, longitude, and date to compute sunrise/sunset times
 * as minutes-since-midnight UTC.  Accuracy: ~±2 minutes.
 */

#ifndef SOLAR_H
#define SOLAR_H

#include <stdint.h>

typedef struct {
    uint16_t sunriseMin;  /* minutes since midnight UTC (0-1439) */
    uint16_t sunsetMin;   /* minutes since midnight UTC (0-1439) */
    uint8_t  valid;       /* 0 = polar day/night (no rise or set) */
} solar_times_t;

/*
 * Compute sunrise and sunset for a given date and position.
 *
 * @param day    Day of month (1-31)
 * @param month  Month (1-12)
 * @param year   Full year (e.g. 2026)
 * @param lat    Latitude in decimal degrees (+ = North)
 * @param lon    Longitude in decimal degrees (+ = East)
 * @param out    Output struct
 */
void solar_compute(uint8_t day, uint8_t month, uint16_t year,
                   float lat, float lon, solar_times_t *out);

#endif /* SOLAR_H */
