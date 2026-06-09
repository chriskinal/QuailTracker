/*
 * QuailTracker — SPI Bridge (STM32 side)
 *
 * Binary frame build/process for STM32 ↔ ESP32 SPI communication.
 */

#include "spi_bridge.h"
#include "main.h"
#include "schedule.h"
#include <string.h>
#include <stdio.h>

/* Must match the define in main.c */
#ifndef MDF_HW_GAIN_MAX
#define MDF_HW_GAIN_MAX 8
#endif

/* ── State fill ──────────────────────────────────────────────── */

void spi_state_fill(spi_state_t *s, const device_state_t *dev,
                    const health_stats_t *health, const device_config_t *cfg,
                    uint8_t solar_st)
{
    memset(s, 0, sizeof(*s));

    /* Recording */
    s->rec_active     = dev->rec.active;
    s->rec_sdMounted  = dev->rec.sdMounted;
    s->rec_format     = dev->rec.format;
    s->rec_dataBytes  = dev->rec.dataBytes;
    s->rec_overruns   = dev->rec.overruns;
    s->rec_sdFreeKb   = dev->rec.sdFreeKb;
    s->rec_sdTotalKb  = dev->rec.sdTotalKb;

    /* GPS */
    s->gps_fix        = dev->gps.fix.fix;
    s->gps_satellites = dev->gps.fix.satellites;
    s->gps_valid      = dev->gps.fix.valid;
    s->gps_ppsSynced  = dev->gps.ppsSynced;
    s->gps_lat5       = (int32_t)(dev->gps.fix.latitude * 100000.0f);
    s->gps_lon5       = (int32_t)(dev->gps.fix.longitude * 100000.0f);
    s->gps_alt1       = (int32_t)(dev->gps.fix.altitude * 10.0f);
    s->gps_utcTime    = dev->gps.fix.utc_time;
    s->gps_ppsCount   = dev->gps.ppsCount;

    /* Environment */
    s->env_battMv     = (uint16_t)dev->env.batteryMv;
    s->env_tempC100   = dev->env.tempC100;
    s->env_humRH100   = dev->env.humRH100;

    /* Audio */
    s->audio_peakLevel = dev->audio.peakLevel;
    s->audio_actRatio  = dev->audio.actRatio;
    s->audio_clipCount = dev->audio.clipCount;

    /* Power. schedActive is derived: schedule "active" means dev mode is OFF
     * AND the schedule is armable (RTC synced + at least one window). */
    s->pwr_state       = (uint8_t)dev->pwr.state;
    s->pwr_devMode     = dev->pwr.devMode;
    s->pwr_rtcSynced   = dev->pwr.rtcSynced;
    s->pwr_schedActive = (!dev->pwr.devMode
                          && dev->pwr.rtcSynced
                          && schedule_has_windows(cfg)) ? 1 : 0;
    s->pwr_sleepSecs   = dev->pwr.sleepIntentSecs;  /* gates the ESP self-heal watchdog during scheduled sleep */

    /* Detection */
    s->det_modelLoaded       = dev->det.modelLoaded;
    s->det_modelBufSize      = dev->det.modelBufSize;
    s->det_modelNumLabels    = dev->det.modelNumLabels;
    s->det_windowsProcessed  = dev->det.windowsProcessed;
    s->det_hits              = dev->det.hits;
    memcpy(s->det_lastSpecies, dev->det.lastSpecies, sizeof(s->det_lastSpecies));
    s->det_lastConf          = dev->det.lastConf;
    memcpy(s->det_lastTime, dev->det.lastTime, sizeof(s->det_lastTime));

    /* Health */
    s->health_filesWritten  = health->filesWritten;
    s->health_recordingSecs = health->recordingSecs;
    s->health_detections    = health->detections;
    s->health_battMinMv     = health->battMinMv;
    s->health_battMaxMv     = health->battMaxMv;
    s->health_tempMinC100   = health->tempMinC100;
    s->health_tempMaxC100   = health->tempMaxC100;
    s->health_bootCount     = health->bootCount;
    s->health_sdErrors      = health->sdErrors;
    s->health_gpsFixLosses  = health->gpsFixLosses;

    /* Solar */
    s->solar_state = solar_st;

    /* Survey */
    s->survey_lat5   = (int32_t)(cfg->surveyLat * 100000.0f);
    s->survey_lon5   = (int32_t)(cfg->surveyLon * 100000.0f);
    s->survey_count  = cfg->surveyCount;
    s->survey_active = dev->gps.surveyActive ? 1 : 0;

    /* Comms */
    s->comms_spiTransactions = dev->comms.spiTransactions;
    strncpy(s->comms_stm32FwVersion, FW_VERSION, sizeof(s->comms_stm32FwVersion) - 1);

    /* SD format progress (driven by formatSD() on the format thread). Percent
     * is sectors-written / estimated-total, clamped to 99 until the done flag
     * (state 2) snaps it to 100, so it never reads complete while still busy. */
    {
        extern volatile uint8_t  sdFormatState;
        extern volatile uint32_t sdFormatBytes;
        extern volatile uint32_t sdFormatStartTk;
        extern volatile uint32_t sdFormatTotalSect;
        s->sdFormat_state = sdFormatState;
        uint8_t pct = 0;
        if (sdFormatState == 2) {
            pct = 100;
        } else if (sdFormatState == 1 && sdFormatTotalSect) {
            uint32_t done = (sdFormatBytes / 512U) * 100U / sdFormatTotalSect;
            pct = (done > 99U) ? 99U : (uint8_t)done;
        }
        s->sdFormat_pct = pct;
        s->sdFormat_elapsedS = sdFormatState
            ? (uint16_t)((HAL_GetTick() - sdFormatStartTk) / 1000U) : 0;
    }
}

/* ── Frame build ─────────────────────────────────────────────── */

void spi_frame_build(spi_frame_t *frame, const device_config_t *cfg,
                     const device_state_t *dev, const health_stats_t *health,
                     uint8_t solar_st, uint16_t flags)
{
    memset(frame, 0, sizeof(*frame));

    /* Header */
    frame->header.magic         = SPI_FRAME_MAGIC;
    frame->header.frame_version = SPI_FRAME_VERSION;
    frame->header.flags         = flags | SPI_FLAG_STATE_VALID;
    frame->header.cfg_seq       = cfg->cfg_seq;

    /* Config (binary copy) */
    memcpy(&frame->config, cfg, sizeof(device_config_t));

    /* State */
    spi_state_fill(&frame->state, dev, health, cfg, solar_st);

    /* CRC (computed last, with crc16 field zeroed) */
    frame->header.crc16 = spi_frame_crc(frame);
}

/* ── Frame process RX ────────────────────────────────────────── */

int spi_frame_process_rx(const spi_frame_t *frame, device_config_t *cfg,
                         uint8_t *cmd_out, uint8_t *cmd_payload, uint32_t *cmd_seq)
{
    *cmd_out = SPI_CMD_NONE;
    int config_adopted = 0;

    /* Validate magic */
    if (frame->header.magic != SPI_FRAME_MAGIC)
        return 0;

    /* Validate CRC */
    uint16_t expected = spi_frame_crc(frame);
    if (expected != frame->header.crc16)
        return 0;

    /* Config sync: adopt if remote seq is higher */
    if (frame->header.cfg_seq > cfg->cfg_seq) {
        memcpy(cfg, &frame->config, sizeof(device_config_t));
        cfg->cfg_seq = frame->header.cfg_seq;
        config_adopted = 1;
    }

    /* Command dispatch */
    if (frame->header.flags & SPI_FLAG_CMD_PENDING) {
        *cmd_out = frame->command.cmd;
        if (cmd_payload)
            memcpy(cmd_payload, frame->command.payload, sizeof(frame->command.payload));
        if (cmd_seq)
            *cmd_seq = frame->command.seq;
    }

    return config_adopted;
}

/* ── Config apply ────────────────────────────────────────────── */

void config_apply(const device_config_t *cfg)
{
    extern MDF_HandleTypeDef MdfHandle0, MdfHandle1;
    extern MDF_FilterConfigTypeDef MdfFilterConfig0, MdfFilterConfig1;
    extern device_state_t dev;

    /* Clamp hardware gain to avoid PDM DC offset saturation.
     * The MDF signal chain is CIC→SCALE→HPF — SCALE amplifies DC offset
     * before the HPF can remove it, causing saturation on some mics above SCALE=8. */
    int32_t hwGain = (int32_t)cfg->gain;
    if (hwGain > MDF_HW_GAIN_MAX)
        hwGain = MDF_HW_GAIN_MAX;

    MdfFilterConfig0.Gain = hwGain;
    MdfFilterConfig1.Gain = hwGain;
    HAL_MDF_SetGain(&MdfHandle0, hwGain);
    HAL_MDF_SetGain(&MdfHandle1, hwGain);

    /* Recording format */
    dev.rec.format = cfg->recFormat;
}
