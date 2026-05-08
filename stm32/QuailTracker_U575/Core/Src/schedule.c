/*
 * schedule.c — Recording schedule evaluator
 *
 * Builds a flat list of time windows from:
 *   1. Sunrise ± offset (if enabled and lat/lon available)
 *   2. Sunset ± offset (if enabled and lat/lon available)
 *   3. Freeform HHMM pairs from config
 *
 * Checks if current UTC time is inside any window.
 * Handles midnight-spanning windows correctly.
 */

#include "schedule.h"
#include "solar.h"

#define MINS_PER_DAY 1440

/* A time window as start/end in minutes-since-midnight UTC */
typedef struct {
    uint16_t start;  /* 0-1439 */
    uint16_t end;    /* 0-1439 */
} time_window_t;

#define MAX_WINDOWS 10  /* 1 sunrise + 1 sunset + 8 freeform */

/* Check if nowMin is inside [start, end), handling midnight wrap.
 * Returns 1 if inside window. */
static uint8_t in_window(uint16_t nowMin, uint16_t start, uint16_t end)
{
    if (start <= end) {
        /* Normal: e.g. 05:00 - 08:00 */
        return (nowMin >= start && nowMin < end) ? 1 : 0;
    } else {
        /* Midnight wrap: e.g. 23:00 - 02:00 */
        return (nowMin >= start || nowMin < end) ? 1 : 0;
    }
}

/* Minutes from nowMin to target (forward), wrapping at midnight */
static uint32_t mins_until(uint16_t nowMin, uint16_t target)
{
    if (target >= nowMin)
        return target - nowMin;
    else
        return MINS_PER_DAY - nowMin + target;
}

uint8_t schedule_has_windows(const device_config_t *cfg)
{
    if (cfg->sunriseEnabled) return 1;
    if (cfg->sunsetEnabled) return 1;
    if (cfg->numWindows > 0) return 1;
    return 0;
}

schedule_result_t schedule_evaluate(const device_config_t *cfg,
                                    uint16_t nowMinUTC,
                                    uint8_t day, uint8_t month, uint16_t year,
                                    float lat, float lon)
{
    schedule_result_t result = {0, 0, 0};
    time_window_t windows[MAX_WINDOWS];
    int nwin = 0;

    /* If no windows configured, default to always record */
    if (!schedule_has_windows(cfg)) {
        result.shouldRecord = 1;
        return result;
    }

    /* Compute solar times if we have position and sunrise/sunset is enabled */
    solar_times_t solar = {0};
    uint8_t haveSolar = 0;
    if ((cfg->sunriseEnabled || cfg->sunsetEnabled) &&
        (lat != 0.0f || lon != 0.0f)) {
        solar_compute(day, month, year, lat, lon, &solar);
        haveSolar = solar.valid;
    }

    /* Build sunrise window */
    if (cfg->sunriseEnabled && haveSolar) {
        int32_t start = (int32_t)solar.sunriseMin - (int32_t)cfg->sunriseBefore;
        int32_t end   = (int32_t)solar.sunriseMin + (int32_t)cfg->sunriseAfter;
        /* Wrap to [0, 1440) */
        while (start < 0) start += MINS_PER_DAY;
        while (start >= MINS_PER_DAY) start -= MINS_PER_DAY;
        while (end < 0) end += MINS_PER_DAY;
        while (end >= MINS_PER_DAY) end -= MINS_PER_DAY;
        windows[nwin].start = (uint16_t)start;
        windows[nwin].end   = (uint16_t)end;
        nwin++;
    }

    /* Build sunset window */
    if (cfg->sunsetEnabled && haveSolar) {
        int32_t start = (int32_t)solar.sunsetMin - (int32_t)cfg->sunsetBefore;
        int32_t end   = (int32_t)solar.sunsetMin + (int32_t)cfg->sunsetAfter;
        while (start < 0) start += MINS_PER_DAY;
        while (start >= MINS_PER_DAY) start -= MINS_PER_DAY;
        while (end < 0) end += MINS_PER_DAY;
        while (end >= MINS_PER_DAY) end -= MINS_PER_DAY;
        windows[nwin].start = (uint16_t)start;
        windows[nwin].end   = (uint16_t)end;
        nwin++;
    }

    /* Build freeform windows (HHMM pairs) */
    uint8_t nfree = cfg->numWindows;
    if (nfree > 8) nfree = 8;
    for (uint8_t i = 0; i < nfree && nwin < MAX_WINDOWS; i++) {
        uint16_t startHHMM = cfg->windows[i * 2];
        uint16_t endHHMM   = cfg->windows[i * 2 + 1];
        uint16_t startMin = (startHHMM / 100) * 60 + (startHHMM % 100);
        uint16_t endMin   = (endHHMM / 100) * 60 + (endHHMM % 100);
        if (startMin < MINS_PER_DAY && endMin < MINS_PER_DAY) {
            windows[nwin].start = startMin;
            windows[nwin].end   = endMin;
            nwin++;
        }
    }

    /* Check if we're inside any window */
    uint32_t minSecsToEnd  = UINT32_MAX;
    uint32_t minSecsToNext = UINT32_MAX;

    for (int i = 0; i < nwin; i++) {
        if (in_window(nowMinUTC, windows[i].start, windows[i].end)) {
            result.shouldRecord = 1;
            uint32_t toEnd = mins_until(nowMinUTC, windows[i].end) * 60;
            if (toEnd < minSecsToEnd) minSecsToEnd = toEnd;
        } else {
            uint32_t toStart = mins_until(nowMinUTC, windows[i].start) * 60;
            if (toStart < minSecsToNext) minSecsToNext = toStart;
        }
    }

    if (result.shouldRecord) {
        result.secsUntilEnd = (minSecsToEnd < UINT32_MAX) ? minSecsToEnd : 0;
        result.secsUntilNext = 0;
    } else {
        /* If no windows could be built (e.g. sunrise/sunset enabled but no
         * lat/lon yet), fall back to one full day instead of 0 — otherwise
         * the sleep loop in powerScheduleCheck clamps to its 60 s minimum
         * and thrashes wake/sleep until conditions change. */
        result.secsUntilNext = (minSecsToNext < UINT32_MAX) ? minSecsToNext : 86400u;
        result.secsUntilEnd = 0;
    }

    return result;
}
