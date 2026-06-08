/*
 * QuailTracker — SPI Binary Protocol (STM32 ↔ ESP32)
 *
 * Shared header defining the 1024-byte SPI frame format.
 * Both sides maintain identical device_config_t copies synced via
 * binary SPI frames. Commands are enum + payload, not JSON strings.
 *
 * Compiles on ARM GCC (STM32) and RISC-V GCC (ESP32-C3).
 */

#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H

#include <stdint.h>
#include <string.h>

/* ── Frame layout (1024 bytes, full-duplex) ──────────────────────
 *
 * Offset  Size  Field
 * ------  ----  -----
 * 0       16    spi_frame_header_t
 * 16      128   device_config_t (from device_state.h)
 * 144     384   spi_state_t
 * 528     64    qt_spi_cmd_t
 * 592     432   reserved (zero-filled)
 */

#define SPI_FRAME_SIZE    1024
#define SPI_FRAME_MAGIC   0x51545350  /* "QTSP" */
#define SPI_FRAME_VERSION 1

/* ── Header flags ────────────────────────────────────────────── */

#define SPI_FLAG_CONFIG_DIRTY  0x0001  /* sender's config changed */
#define SPI_FLAG_CMD_PENDING   0x0002  /* command slot contains a command */
#define SPI_FLAG_STATE_VALID   0x0004  /* state section is populated */
#define SPI_FLAG_BOOT          0x0008  /* sender just booted */

/* ── Frame header (16 bytes) ─────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t magic;          /* SPI_FRAME_MAGIC */
    uint16_t frame_version;  /* SPI_FRAME_VERSION */
    uint16_t flags;          /* SPI_FLAG_* bitmask */
    uint32_t cfg_seq;        /* config sequence number (higher wins) */
    uint16_t crc16;          /* CRC-16-CCITT over full 1024 bytes (this field zeroed during calc) */
    uint16_t _pad;
} spi_frame_header_t;

_Static_assert(sizeof(spi_frame_header_t) == 16, "spi_frame_header_t must be 16 bytes");

/* ── Command types ───────────────────────────────────────────── */

typedef enum {
    SPI_CMD_NONE           = 0,
    SPI_CMD_REC_TOGGLE     = 1,
    SPI_CMD_SD_MOUNT       = 2,
    SPI_CMD_SD_EJECT       = 3,
    SPI_CMD_SD_FORMAT      = 4,
    SPI_CMD_SURVEY_START   = 5,
    SPI_CMD_SURVEY_CLEAR   = 6,
    SPI_CMD_SCHEDULE_ON    = 7,
    SPI_CMD_SCHEDULE_OFF   = 8,
    SPI_CMD_DEV_MODE       = 9,
    SPI_CMD_REBOOT         = 10,
    SPI_CMD_ESP_READY      = 11,
    SPI_CMD_ESP_VERSION    = 12,
    SPI_CMD_SET_DETECT     = 13,
    SPI_CMD_MODEL_RELOAD   = 14,
    SPI_CMD_AUDIO_STREAM   = 15,
    SPI_CMD_SET_TZ         = 16,  /* RAM-only TZ refresh — see qt_spi_tz_payload_t */
    /* (17-19 were the dual-bank A/B OTA opcodes — removed; STM32 is single-bank,
     *  updated only via the ESP ROM-bootloader flash.) */
} spi_cmd_type_t;

/* Payload for SPI_CMD_SET_TZ. Sent by ESP32 (browser-driven) so the device
 * can apply the user's local timezone without persisting to flash on every
 * heartbeat. The full triple lets the device flip offsets autonomously at
 * the next DST transition; nextTransitionUtc=0 means "no transition known". */
typedef struct __attribute__((packed)) {
    int16_t  utcOffsetMin;       /* current offset (signed minutes) */
    int16_t  nextOffsetMin;      /* offset that takes effect at nextTransitionUtc */
    uint32_t nextTransitionUtc;  /* seconds since 1970-01-01 00:00 UTC; 0 = none */
} qt_spi_tz_payload_t;

_Static_assert(sizeof(qt_spi_tz_payload_t) == 8, "qt_spi_tz_payload_t must be 8 bytes");

/* ── Command slot (64 bytes) ─────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  cmd;            /* spi_cmd_type_t */
    uint8_t  _pad[3];
    uint32_t seq;            /* for ack/dedup */
    uint8_t  payload[56];    /* cmd-specific data */
} qt_spi_cmd_t;

_Static_assert(sizeof(qt_spi_cmd_t) == 64, "qt_spi_cmd_t must be 64 bytes");

/* ── Solar charge state ──────────────────────────────────────── */

typedef enum {
    SOLAR_STANDBY  = 0,
    SOLAR_CHARGING = 1,
    SOLAR_COMPLETE = 2,
    SOLAR_FAULT    = 3,
} solar_state_t;

/* ── State struct (384 bytes) ────────────────────────────────── */
/* STM32 fills this from dev.*, health.*, globals. ESP32 caches for web UI. */

typedef struct __attribute__((packed)) {
    /* Recording (20 bytes) */
    uint8_t  rec_active;
    uint8_t  rec_sdMounted;
    uint8_t  rec_format;      /* 0=FLAC, 1=WAV */
    uint8_t  _pad_rec;
    uint32_t rec_dataBytes;
    uint32_t rec_overruns;
    uint32_t rec_sdFreeKb;
    uint32_t rec_sdTotalKb;

    /* GPS (24 bytes) */
    uint8_t  gps_fix;
    uint8_t  gps_satellites;
    uint8_t  gps_valid;
    uint8_t  gps_ppsSynced;
    int32_t  gps_lat5;        /* latitude * 100000 */
    int32_t  gps_lon5;        /* longitude * 100000 */
    int32_t  gps_alt1;        /* altitude * 10 */
    uint32_t gps_utcTime;     /* HHMMSS as integer */
    uint32_t gps_ppsCount;

    /* Environment (8 bytes) */
    uint16_t env_battMv;
    int16_t  env_tempC100;
    uint16_t env_humRH100;
    uint16_t _pad_env;

    /* Audio (12 bytes) */
    int32_t  audio_peakLevel;
    uint8_t  audio_actRatio;
    uint8_t  _pad_audio[3];
    uint32_t audio_clipCount;

    /* Power (4 bytes) */
    uint8_t  pwr_state;       /* power_state_t enum */
    uint8_t  pwr_devMode;
    uint8_t  pwr_rtcSynced;
    uint8_t  pwr_schedActive;

    /* Detection (68 bytes) */
    uint8_t  det_modelLoaded;
    uint8_t  _pad_det1[3];
    uint32_t det_modelBufSize;
    int32_t  det_modelNumLabels;
    uint32_t det_windowsProcessed;
    uint32_t det_hits;
    char     det_lastSpecies[32];
    uint8_t  det_lastConf;
    uint8_t  _pad_det2[3];
    char     det_lastTime[20];

    /* Health (40 bytes) */
    uint32_t health_filesWritten;
    uint32_t health_recordingSecs;
    uint32_t health_detections;
    uint32_t health_battMinMv;
    uint32_t health_battMaxMv;
    int32_t  health_tempMinC100;
    int32_t  health_tempMaxC100;
    uint32_t health_bootCount;
    uint32_t health_sdErrors;
    uint32_t health_gpsFixLosses;

    /* Solar (4 bytes) */
    uint8_t  solar_state;     /* solar_state_t enum */
    uint8_t  _pad_solar[3];

    /* Survey (12 bytes) */
    int32_t  survey_lat5;     /* cfg.surveyLat * 100000 */
    int32_t  survey_lon5;     /* cfg.surveyLon * 100000 */
    uint32_t survey_count;

    /* Comms (20 bytes) */
    uint32_t comms_spiTransactions;
    char     comms_stm32FwVersion[16];

    /* Power management — scheduled-sleep notice (2 bytes).
     * >0: the STM is about to enter (or is in) Stop 2 for ~this many seconds.
     * The ESP MUST treat the ensuing SPI silence as EXPECTED — not a dead STM —
     * until roughly this elapses, so the self-heal watchdog doesn't reflash a
     * unit that is merely sleeping between recording windows. 0 = awake/normal. */
    uint16_t pwr_sleepSecs;

    /* Reserved — pad to 384 bytes */
    uint8_t  _reserved[384 - 222];
} spi_state_t;

_Static_assert(sizeof(spi_state_t) == 384, "spi_state_t must be 384 bytes");

/* ── Complete SPI frame (1024 bytes) ─────────────────────────── */

#ifndef SPI_PROTOCOL_NO_CONFIG
/*
 * If device_config_t is defined elsewhere (device_state.h on STM32),
 * include this header AFTER device_state.h. The frame struct uses the
 * externally-defined device_config_t.
 *
 * On ESP32, device_config_t is defined below.
 */
#endif

/*
 * device_config_t definition for ESP32 (matches STM32's device_state.h).
 * On STM32 this is already defined — guard with the same include guard.
 */
#ifndef DEVICE_STATE_H

#define CONFIG_MAGIC      0x51544346   /* "QTCF" */
#define CONFIG_VERSION    10

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    char     stationId[16];
    uint8_t  gain;
    uint16_t bpfLow;
    uint16_t bpfHigh;
    uint8_t  recFormat;
    uint8_t  sunriseEnabled;
    uint16_t sunriseBefore;
    uint16_t sunriseAfter;
    uint8_t  sunsetEnabled;
    uint16_t sunsetBefore;
    uint16_t sunsetAfter;
    uint8_t  numWindows;
    uint16_t windows[16];
    uint8_t  trigEnabled;
    int8_t   trigDb;
    uint8_t  trigPre;
    uint8_t  trigPost;
    uint8_t  lowBatPct;
    uint8_t  autoStop;
    uint8_t  activityMode;
    uint8_t  activityMinPct;
    uint8_t  activityMaxPct;
    uint8_t  activityHoldSec;
    float    surveyLat;
    float    surveyLon;
    float    surveyAlt;
    uint32_t surveyCount;
    uint8_t  missionMode;
    uint8_t  detConfThresh;
    uint8_t  detWindowStep;
    uint8_t  chunkMinutes;
    uint16_t micHeading;                       /* mic axis compass heading 0-359, 0xFFFF=unset */
    int16_t  utcOffsetMin;                     /* signed minutes from UTC */
    int16_t  nextOffsetMin;                    /* offset after nextTransitionUtc */
    uint32_t nextTransitionUtc;                /* epoch s; 0 = none */
    uint32_t cfg_seq;                          /* config sequence number */
    uint8_t  _pad[128 - 114 - 4];             /* pad to 128 bytes */
    uint32_t crc32;
} device_config_t;

_Static_assert(sizeof(device_config_t) == 128, "device_config_t must be 128 bytes");

#endif /* DEVICE_STATE_H */

/* ── Audio streaming payload (fits in _reserved region) ─────── */

typedef struct __attribute__((packed)) {
    uint8_t  audio_active;    /* 1 = samples present */
    uint8_t  channel;         /* 0 = left, 1 = right */
    uint16_t num_samples;     /* count of int16 samples, max 214 */
    int16_t  samples[214];    /* 8 kHz int16 PCM */
} spi_audio_payload_t;

_Static_assert(sizeof(spi_audio_payload_t) == 432, "spi_audio_payload_t must be 432 bytes");

/* ── Complete SPI frame (1024 bytes) ─────────────────────────── */

typedef struct __attribute__((packed)) {
    spi_frame_header_t header;       /* 16 bytes */
    device_config_t    config;       /* 128 bytes */
    spi_state_t        state;        /* 384 bytes */
    qt_spi_cmd_t      command;      /* 64 bytes */
    uint8_t            _reserved[SPI_FRAME_SIZE - 16 - 128 - 384 - 64];  /* 432 bytes */
} spi_frame_t;

_Static_assert(sizeof(spi_frame_t) == SPI_FRAME_SIZE, "spi_frame_t must be 1024 bytes");

/* ── CRC-32 (IEEE 802.3, reflected) — used for the A/B OTA image checksum ──
 * Streaming: seed with 0 on the first call, pass the returned value back in for
 * subsequent chunks. Both MCUs MUST use this identical implementation so the
 * STM32's computed image CRC matches the value the ESP32 sends in OTA_BEGIN. */
static inline uint32_t spi_crc32(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

/* ── CRC-16-CCITT ────────────────────────────────────────────── */

static inline uint16_t spi_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* Compute CRC over frame with crc16 field zeroed */
static inline uint16_t spi_frame_crc(const spi_frame_t *frame)
{
    /* Copy header, zero the crc field, compute over full frame */
    spi_frame_header_t tmp_hdr;
    memcpy(&tmp_hdr, &frame->header, sizeof(tmp_hdr));
    tmp_hdr.crc16 = 0;

    uint16_t crc = 0xFFFF;
    const uint8_t *hdr_bytes = (const uint8_t *)&tmp_hdr;
    for (uint16_t i = 0; i < sizeof(tmp_hdr); i++) {
        crc ^= (uint16_t)hdr_bytes[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }

    /* Continue CRC over rest of frame (after header) */
    const uint8_t *rest = (const uint8_t *)frame + sizeof(spi_frame_header_t);
    uint16_t rest_len = SPI_FRAME_SIZE - sizeof(spi_frame_header_t);
    for (uint16_t i = 0; i < rest_len; i++) {
        crc ^= (uint16_t)rest[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

#endif /* SPI_PROTOCOL_H */
