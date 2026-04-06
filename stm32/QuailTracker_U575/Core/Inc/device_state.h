/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * Consolidated runtime device state.  All pure runtime variables that are
 * shared across compilation units live here in a single struct so that
 * ble_proto_handlers (and anything else) can take an atomic snapshot.
 *
 * What stays OUT: HAL handles, RTOS handles, queues, mutexes, semaphores,
 * ring buffers, working buffers, tensor arena, filter state, recording
 * metadata statics, and the flash-persisted cfg/health structs.
 */

#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#include <stdint.h>

/* ---- Power states ---- */
typedef enum {
    PWR_SCHEDULED_NONREC = 0,  /* Sleeping between recording windows */
    PWR_SCHEDULED_REC    = 1,  /* Recording during a scheduled window */
    PWR_USER_CONNECTED   = 2,  /* User connected via BLE/WiFi */
    PWR_DEV_MODE         = 3,  /* Development mode — everything on */
} power_state_t;

/* Wake source from Stop 2 */
typedef enum {
    WAKE_RTC   = 0,   /* RTC timer expired */
    WAKE_ESP32 = 1,   /* ESP32 asserted CS pin (PD0) */
} wake_source_t;

/* ---- GPS data ---- */
typedef struct {
    uint8_t  fix;        /* 0=no fix, 1=GPS, 2=DGPS */
    uint8_t  satellites;
    float    latitude;   /* decimal degrees, + = N */
    float    longitude;  /* decimal degrees, + = E */
    float    altitude;   /* meters above MSL (from GGA) */
    uint32_t utc_time;   /* HHMMSS as integer */
    uint32_t utc_date;   /* DDMMYY as integer */
    uint8_t  valid;      /* RMC status: 1=A, 0=V */
} gps_data_t;

/* ---- Flash-persisted device configuration ---- */
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t magic;
    uint8_t  version;
    char     stationId[16];   /* null-terminated */
    uint8_t  gain;            /* 0-24 dB in 3dB steps */
    uint16_t bpfLow;          /* HPF cutoff Hz, 0=off, default 150 */
    uint16_t bpfHigh;         /* LPF cutoff Hz, 0=off, default 8000 */
    uint8_t  recFormat;       /* 0=FLAC, 1=WAV */
    uint8_t  sunriseEnabled;  /* 0/1 */
    uint16_t sunriseBefore;   /* minutes before sunrise */
    uint16_t sunriseAfter;    /* minutes after sunrise */
    uint8_t  sunsetEnabled;   /* 0/1 */
    uint16_t sunsetBefore;    /* minutes before sunset */
    uint16_t sunsetAfter;     /* minutes after sunset */
    uint8_t  numWindows;      /* 0-8 freeform windows */
    uint16_t windows[16];     /* pairs of HHMM start,end (max 8 windows) */
    uint8_t  trigEnabled;     /* 0/1 */
    int8_t   trigDb;          /* -60..0 */
    uint8_t  trigPre;         /* 0-30 seconds */
    uint8_t  trigPost;        /* 0-60 seconds */
    uint8_t  lowBatPct;       /* 0-100 */
    uint8_t  autoStop;        /* 0/1 */
    uint8_t  activityMode;    /* 0=off, 1=monitor, 2=squelch, 3=gate */
    uint8_t  activityMinPct;  /* 1-50, default 5 */
    uint8_t  activityMaxPct;  /* 50-99, default 80 */
    uint8_t  activityHoldSec; /* 1-30, gate mode holdoff, default 3 */
    float    surveyLat;       /* averaged latitude (decimal degrees) */
    float    surveyLon;       /* averaged longitude (decimal degrees) */
    float    surveyAlt;       /* averaged altitude (meters) */
    uint32_t surveyCount;     /* number of GPS fixes averaged */
    uint8_t  missionMode;     /* 0=record, 1=detect, 2=both */
    uint8_t  detConfThresh;   /* 0-100 confidence % threshold */
    uint8_t  detWindowStep;   /* 1-3 seconds inference window step */
    uint8_t  chunkMinutes;    /* 0=no chunking, 1-240 = chunk duration in minutes */
    uint32_t cfg_seq;         /* config sequence number for SPI sync (higher wins) */
    uint8_t  _pad[128 - 104 - 4]; /* pad to 128 bytes: 104 pre-pad + 20 pad + 4 crc */
    uint32_t crc32;           /* CRC-32 over bytes 0..123 */
} device_config_t;

_Static_assert(sizeof(device_config_t) == 128, "device_config_t must be 128 bytes");

/* ---- Flash-persisted health statistics ---- */
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t magic;
    uint8_t  version;
    /* Recording */
    uint32_t filesWritten;
    uint64_t totalBytes;
    uint32_t recordingSecs;
    char     lastFilename[40];
    uint32_t lastFileBytes;
    uint32_t lastFileSecs;
    uint32_t writeErrors;
    /* Detection */
    uint32_t detections;
    char     lastSpecies[32];
    uint8_t  lastConfidence;
    char     lastDetTime[16];
    /* System */
    uint32_t battMinMv;
    uint32_t battMaxMv;
    int32_t  tempMinC100;
    int32_t  tempMaxC100;
    uint32_t bootCount;
    uint32_t sdErrors;
    uint32_t gpsFixLosses;
    uint32_t uptimeStartTick;  /* HAL_GetTick() at boot (always 0, stored for reference) */
    uint8_t  _pad[256 - 158 - 4];  /* pad to 256 bytes: 158 packed data + 94 pad + 4 crc */
    uint32_t crc32;
} health_stats_t;

_Static_assert(sizeof(health_stats_t) == 256, "health_stats_t must be 256 bytes");

/* ---- Consolidated runtime device state ---- */
typedef struct {
    struct {
        volatile uint8_t  active; /* was isRecording — read by PPS ISR */
        uint8_t  sdMounted;
        uint8_t  audioStarted;
        uint8_t  format;          /* was recFormat (0=FLAC, 1=WAV) */
        uint32_t dataBytes;       /* was totalDataBytes */
        uint32_t overruns;        /* was ringOverruns */
        uint32_t startTick;       /* was recStartTick */
        uint32_t fileCounter;
        char     filename[48];    /* was recFilename */
        uint32_t sdTotalKb;       /* cached SD total (updated every 5s) */
        uint32_t sdFreeKb;        /* cached SD free  (updated every 5s) */
    } rec;

    struct {
        gps_data_t fix;           /* was gpsData */
        volatile uint8_t  ppsSynced;
        volatile uint32_t ppsCount;
        volatile uint32_t ppsTick;
        uint32_t ppsUtcTime;
        uint32_t ppsUtcDate;
        float    ppsLatitude;
        float    ppsLongitude;
        float    ppsAltitude;
        uint8_t  surveyActive;
        uint32_t surveyStartTick;
    } gps;

    struct {
        uint32_t batteryMv;
        int16_t  tempC100;        /* 0.01 °C units */
        uint16_t humRH100;        /* 0.01 %RH units */
    } env;

    struct {
        uint8_t  espReady;           /* 1 = ESP32 bridge responding on SPI */
        uint8_t  wifiActive;         /* 1 = WiFi AP running */
        uint8_t  bleConnected;       /* 1 = BLE client connected to ESP32 */
        uint32_t spiTransactions;    /* total SPI exchanges */
        uint32_t lastSpiTick;        /* HAL tick of last successful SPI */
        char     espFwVersion[16];   /* ESP32 firmware version string */
    } comms;

    struct {
        volatile int32_t peakLevel;  /* was audioPeakLevel */
        volatile uint8_t actRatio;
        uint32_t clipCount;          /* was limiterClipCount */
    } audio;

    struct {
        volatile power_state_t state;    /* current power state */
        uint8_t  devMode;                /* 1 = dev mode override */
        uint8_t  rtcSynced;             /* 1 = RTC has been set from GPS */
        uint8_t  scheduleActive;        /* 1 = autonomous schedule running */
        uint32_t lastGpsSyncTick;       /* HAL tick of last GPS→RTC sync */
        uint32_t gpsDutyCycleSec;       /* GPS wake interval during recording (0=off) */
    } pwr;

    struct {
        volatile uint8_t  modelLoaded;
        uint32_t modelBufSize;
        int      modelNumLabels;
        volatile uint32_t windowsProcessed;  /* was detWindowsProcessed */
        volatile uint32_t hits;              /* was detHits */
        char     lastSpecies[32];
        volatile uint8_t  lastConf;          /* was detLastConf */
        char     lastTime[20];
    } det;
} device_state_t;

extern device_state_t dev;

/* Take a snapshot of device state (critical section, <1µs at 160MHz) */
void device_state_snapshot(device_state_t *out);

#endif /* DEVICE_STATE_H */
