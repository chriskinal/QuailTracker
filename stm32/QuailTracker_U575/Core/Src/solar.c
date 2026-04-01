/*
 * solar.c — Sunrise/sunset calculator (NOAA simplified algorithm)
 *
 * Adapted from NOAA Solar Calculator spreadsheet.
 * Uses day-of-year + equation of time to get solar noon,
 * then hour angle for sunrise/sunset at standard zenith (90.833°).
 */

#include "solar.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define DEG2RAD (M_PI / 180.0f)
#define RAD2DEG (180.0f / M_PI)

/* Day of year (1-366) from date */
static int day_of_year(uint8_t day, uint8_t month, uint16_t year)
{
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int doy = cum[month - 1] + day;
    /* Leap year adjustment */
    if (month > 2 && (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0))
        doy++;
    return doy;
}

void solar_compute(uint8_t day, uint8_t month, uint16_t year,
                   float lat, float lon, solar_times_t *out)
{
    out->valid = 0;
    out->sunriseMin = 0;
    out->sunsetMin = 0;

    int doy = day_of_year(day, month, year);

    /* Fractional year (radians) */
    float gamma = 2.0f * M_PI * (doy - 1) / 365.0f;

    /* Equation of time (minutes) */
    float eqtime = 229.18f * (0.000075f
        + 0.001868f * cosf(gamma) - 0.032077f * sinf(gamma)
        - 0.014615f * cosf(2.0f * gamma) - 0.040849f * sinf(2.0f * gamma));

    /* Solar declination (radians) */
    float decl = 0.006918f
        - 0.399912f * cosf(gamma) + 0.070257f * sinf(gamma)
        - 0.006758f * cosf(2.0f * gamma) + 0.000907f * sinf(2.0f * gamma)
        - 0.002697f * cosf(3.0f * gamma) + 0.00148f * sinf(3.0f * gamma);

    /* Hour angle at sunrise/sunset (zenith = 90.833°) */
    float latRad = lat * DEG2RAD;
    float zenith = 90.833f * DEG2RAD;

    float cosHA = (cosf(zenith) / (cosf(latRad) * cosf(decl)))
                  - tanf(latRad) * tanf(decl);

    /* Polar day/night check */
    if (cosHA < -1.0f || cosHA > 1.0f)
        return;

    float ha = acosf(cosHA) * RAD2DEG;  /* hour angle in degrees */

    /* Solar noon (minutes UTC) */
    float noon = 720.0f - 4.0f * lon - eqtime;

    /* Sunrise and sunset (minutes UTC) */
    float rise = noon - ha * 4.0f;
    float set  = noon + ha * 4.0f;

    /* Wrap to 0-1440 range */
    if (rise < 0.0f) rise += 1440.0f;
    if (rise >= 1440.0f) rise -= 1440.0f;
    if (set < 0.0f) set += 1440.0f;
    if (set >= 1440.0f) set -= 1440.0f;

    out->sunriseMin = (uint16_t)(rise + 0.5f);
    out->sunsetMin  = (uint16_t)(set + 0.5f);
    out->valid = 1;
}
