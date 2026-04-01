/*
 * schedule.h — Recording schedule evaluator
 *
 * Combines sunrise/sunset windows and freeform HHMM windows from device
 * config to determine whether the device should be recording right now.
 */

#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdint.h>
#include "device_state.h"

typedef struct {
    uint8_t  shouldRecord;   /* 1 = inside a recording window */
    uint32_t secsUntilNext;  /* seconds until next window starts (0 if recording) */
    uint32_t secsUntilEnd;   /* seconds until current window ends (0 if not recording) */
} schedule_result_t;

/*
 * Evaluate whether we should be recording right now.
 *
 * @param cfg       Pointer to device config (schedule fields)
 * @param nowMinUTC Current time as minutes since midnight UTC (0-1439)
 * @param day       Day of month (1-31)
 * @param month     Month (1-12)
 * @param year      Full year (e.g. 2026)
 * @param lat       Latitude (decimal degrees, for solar calc)
 * @param lon       Longitude (decimal degrees, for solar calc)
 * @return          Schedule evaluation result
 */
schedule_result_t schedule_evaluate(const device_config_t *cfg,
                                    uint16_t nowMinUTC,
                                    uint8_t day, uint8_t month, uint16_t year,
                                    float lat, float lon);

/*
 * Check if any schedule windows are configured at all.
 * Returns 0 if no sunrise/sunset/freeform windows are enabled.
 */
uint8_t schedule_has_windows(const device_config_t *cfg);

#endif /* SCHEDULE_H */
