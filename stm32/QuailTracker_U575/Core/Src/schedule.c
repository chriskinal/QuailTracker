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

/* Compute UNIX epoch (seconds since 1970-01-01 00:00 UTC) from the calendar
 * fields the RTC already provides. Year is full 4-digit (e.g. 2026), month
 * 1-12, day 1-31, hour 0-23, minute 0-59. We don't need second resolution
 * for DST transition comparison. Valid 2000..2099 (uint8 RTC year). */
static uint32_t epoch_from_ymdhm(uint16_t year, uint8_t month, uint8_t day,
                                 uint8_t hour, uint8_t minute)
{
    static const uint16_t cum_days[] = { 0, 31, 59, 90, 120, 151,
                                         181, 212, 243, 273, 304, 334 };
    uint32_t y = (year >= 2000) ? (uint32_t)(year - 2000) : 0;
    /* Days from 2000-01-01 to start of year y. Year 2000 is a leap year. */
    uint32_t days = y * 365 + (y + 3) / 4;  /* leap years 2000, 2004, … */
    days += cum_days[(month - 1) % 12];
    if (month > 2 && (y % 4) == 0) days += 1;  /* leap-year Feb adjustment */
    days += (uint32_t)(day - 1);
    /* 2000-01-01 00:00 UTC = epoch 946684800 */
    return 946684800u + days * 86400u
                     + (uint32_t)hour * 3600u
                     + (uint32_t)minute * 60u;
}

/* Pick the offset that's currently in effect, auto-flipping when the device
 * crosses cfg->nextTransitionUtc. */
static int16_t effective_offset(const device_config_t *cfg, uint32_t nowEpoch)
{
    if (cfg->nextTransitionUtc != 0 && nowEpoch >= cfg->nextTransitionUtc) {
        return cfg->nextOffsetMin;
    }
    return cfg->utcOffsetMin;
}

/* Add a signed minute offset to a [0,1440) minute-of-day, wrapping. */
static uint16_t shift_min(uint16_t minOfDay, int32_t offsetMin)
{
    int32_t v = (int32_t)minOfDay + offsetMin;
    while (v < 0) v += MINS_PER_DAY;
    while (v >= MINS_PER_DAY) v -= MINS_PER_DAY;
    return (uint16_t)v;
}

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

    /* Pick the timezone offset that's currently in effect (auto-flipping at
     * cfg->nextTransitionUtc) and shift "now" plus all sunrise/sunset window
     * boundaries from UTC into local time, so freeform HHMM windows entered
     * by the user are interpreted as local wall-clock time. Freeform windows
     * are stored as-typed and need no shift. */
    uint32_t nowEpoch = epoch_from_ymdhm(year, month, day,
                                         (uint8_t)(nowMinUTC / 60u),
                                         (uint8_t)(nowMinUTC % 60u));
    int32_t offMin = (int32_t)effective_offset(cfg, nowEpoch);
    uint16_t nowMin = shift_min(nowMinUTC, offMin);

    /* Compute solar times if we have position and sunrise/sunset is enabled */
    solar_times_t solar = {0};
    uint8_t haveSolar = 0;
    if ((cfg->sunriseEnabled || cfg->sunsetEnabled) &&
        (lat != 0.0f || lon != 0.0f)) {
        solar_compute(day, month, year, lat, lon, &solar);
        haveSolar = solar.valid;
        if (haveSolar) {
            solar.sunriseMin = shift_min(solar.sunriseMin, offMin);
            solar.sunsetMin  = shift_min(solar.sunsetMin,  offMin);
        }
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
        if (in_window(nowMin, windows[i].start, windows[i].end)) {
            result.shouldRecord = 1;
            uint32_t toEnd = mins_until(nowMin, windows[i].end) * 60;
            if (toEnd < minSecsToEnd) minSecsToEnd = toEnd;
        } else {
            uint32_t toStart = mins_until(nowMin, windows[i].start) * 60;
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
