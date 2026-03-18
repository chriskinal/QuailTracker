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

/* Protobuf message encoding/decoding and command dispatch for BLE protocol.
 *
 * Implements two extern functions called by ble_proto.c:
 *   ble_proto_dispatch()  — decode incoming protobuf messages, handle commands
 *   ble_proto_push_topic() — encode and send a push message for a given topic
 */

#include "ble_proto.h"
#include "quailtracker.pb.h"
#include "main.h"        /* FW_VERSION */
#include "fatfs.h"
#include "user_diskio.h"
#include "mel_spectrogram.h"

#include <pb_encode.h>
#include <pb_decode.h>
#include <string.h>
#include <stdio.h>

#include "cmsis_os2.h"
#include "stm32u5xx_hal.h"
#include "stm32u5xx_hal_mdf.h"

/* ---- Command defines (must match app_freertos.c) ---- */
#define CMD_START_REC  1
#define CMD_STOP_REC   2
#define REC_FMT_FLAC   0
#define REC_FMT_WAV    1
#define MISSION_RECORD  0
#define SURVEY_MIN_SATS 4

/* ---- Ring buffer sizes (must match app_freertos.c) ---- */
#define PCM_RING_SIZE   16384
#define SAMPLE_RATE     48000

/* ---- GPS data type ---- */
typedef struct {
    uint8_t  fix;
    uint8_t  satellites;
    float    latitude;
    float    longitude;
    float    altitude;
    uint32_t utc_time;
    uint32_t utc_date;
    uint8_t  valid;
} gps_data_t;

/* ---- Extern variables from app_freertos.c / main.c ---- */
extern uint8_t sdMounted, isRecording, audioStarted, recFormat;
extern uint32_t totalDataBytes, ringOverruns;
extern volatile int32_t audioPeakLevel;
extern volatile uint8_t actRatio;
extern uint32_t limiterClipCount;
extern volatile uint32_t ringHead;
extern uint32_t ringTail;
extern char recFilename[];

extern osMutexId_t fileMtxHandle;
extern osMessageQueueId_t audioCmdQueueHandle;
extern osThreadId_t inferenceTaskHandle;

extern int16_t  sht30TempC100;
extern uint16_t sht30HumRH100;
extern void sht30Read(void);

extern uint32_t battReadMv(void);

/* GPS */
extern gps_data_t gpsData;
extern volatile uint8_t ppsSynced;
extern volatile uint32_t ppsCount, ppsTick;
extern uint32_t ppsUtcDate, ppsUtcTime;

/* Detection */
extern uint32_t detWindowsProcessed, detHits;
extern char detLastSpecies[];
extern uint8_t detLastConf;
extern char detLastTime[];

/* Model */
extern uint8_t modelLoaded;
extern uint32_t modelBufSize;
extern int modelNumLabels;

/* Config */
typedef struct __attribute__((packed, aligned(16))) {
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
    uint8_t  _pad[128 - 99 - 4];
    uint32_t crc32;
} device_config_t;

extern device_config_t cfg;
extern int configSave(void);

/* Survey */
extern uint8_t surveyActive;
extern uint32_t surveyStartTick;

/* FatFS */
extern FATFS USERFatFS;
extern char USERPath[];
extern void USER_disk_deinit(void);
extern int formatSD(void);

/* BLE state */
extern uint8_t bleReady;
extern char bleName[];

/* Log forwarding */
extern volatile uint8_t bleLogEnabled;
extern volatile uint16_t bleLogHead, bleLogTail;

/* ADF gain */
extern MDF_HandleTypeDef AdfHandle0;

/* Subscription management (defined in ble_proto.c) */
extern void ble_proto_add_subscription(uint8_t topic, uint32_t interval_ms);
extern void ble_proto_remove_subscription(uint8_t topic);

/* ---- Helpers ---- */

/* Send a CommandAck with success=true */
static void send_ack(quailtracker_CommandType cmd_type)
{
    quailtracker_CommandAck ack = quailtracker_CommandAck_init_zero;
    ack.type = cmd_type;
    ack.success = true;

    uint8_t buf[quailtracker_CommandAck_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    pb_encode(&stream, quailtracker_CommandAck_fields, &ack);
    ble_proto_send(TOPIC_COMMAND_ACK, buf, stream.bytes_written);
}

/* Send a CommandAck with success=false and an error message */
static void send_nack(quailtracker_CommandType cmd_type, const char *err)
{
    quailtracker_CommandAck ack = quailtracker_CommandAck_init_zero;
    ack.type = cmd_type;
    ack.success = false;
    strncpy(ack.error, err, sizeof(ack.error) - 1);

    uint8_t buf[quailtracker_CommandAck_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    pb_encode(&stream, quailtracker_CommandAck_fields, &ack);
    ble_proto_send(TOPIC_COMMAND_ACK, buf, stream.bytes_written);
}

/* Read SD free/total KB.  Returns 0,0 if unmounted or locked. */
static void get_sd_space(uint32_t *total_kb, uint32_t *free_kb)
{
    *total_kb = 0;
    *free_kb = 0;
    if (!sdMounted) return;

    FATFS *fs;
    DWORD fre_clust;
    if (osMutexAcquire(fileMtxHandle, 200) == osOK) {
        if (f_getfree("", &fre_clust, &fs) == FR_OK) {
            DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
            DWORD fre_sect = fre_clust * fs->csize;
            *total_kb = (uint32_t)(tot_sect / 2);
            *free_kb  = (uint32_t)(fre_sect / 2);
        }
        osMutexRelease(fileMtxHandle);
    }
}

/* Format GPS UTC time string: "YYYYMMDDHHmmss" from PPS date/time */
static void format_utc_string(char *out, size_t out_size)
{
    if (ppsSynced && ppsUtcDate != 0) {
        uint32_t dd = ppsUtcDate / 10000;
        uint32_t mm = (ppsUtcDate / 100) % 100;
        uint32_t yy = ppsUtcDate % 100;
        uint32_t hh = ppsUtcTime / 10000;
        uint32_t mn = (ppsUtcTime / 100) % 100;
        uint32_t ss = ppsUtcTime % 100;
        snprintf(out, out_size, "20%02lu%02lu%02lu%02lu%02lu%02lu",
                 (unsigned long)yy, (unsigned long)mm, (unsigned long)dd,
                 (unsigned long)hh, (unsigned long)mn, (unsigned long)ss);
    } else {
        out[0] = '\0';
    }
}

/* ---- Push topic encoders ---- */

static void push_status(void)
{
    quailtracker_Status msg = quailtracker_Status_init_zero;

    /* Battery */
    uint32_t mv = battReadMv();
    msg.battery_mv = mv;
    int pct = (int)(mv - 3000) * 100 / 1200;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    msg.battery_pct = (uint32_t)pct;

    /* BLE */
    msg.ble_ready = bleReady ? true : false;
    strncpy(msg.ble_name, bleName, sizeof(msg.ble_name) - 1);

    /* SD card */
    msg.sd_mounted = sdMounted ? true : false;
    get_sd_space(&msg.sd_total_kb, &msg.sd_free_kb);

    /* Recording */
    msg.recording = isRecording ? true : false;
    strncpy(msg.rec_filename, recFilename, sizeof(msg.rec_filename) - 1);
    msg.rec_bytes = totalDataBytes;
    msg.rec_duration_s = isRecording ? totalDataBytes / (SAMPLE_RATE * 3) : 0;
    msg.rec_overruns = ringOverruns;
    msg.rec_format = (uint32_t)recFormat;

    /* Detection */
    msg.det_active = (cfg.missionMode != MISSION_RECORD && modelLoaded) ? true : false;
    msg.det_windows = detWindowsProcessed;
    msg.det_hits = detHits;
    strncpy(msg.det_last_species, detLastSpecies, sizeof(msg.det_last_species) - 1);
    msg.model_loaded = modelLoaded ? true : false;
    msg.model_size = modelBufSize;
    msg.model_labels = (uint32_t)modelNumLabels;

    /* Environment */
    msg.temperature_c100 = (int32_t)sht30TempC100;
    msg.humidity_rh100 = (uint32_t)sht30HumRH100;

    /* Firmware */
    strncpy(msg.firmware_version, FW_VERSION, sizeof(msg.firmware_version) - 1);
    strncpy(msg.station_id, cfg.stationId, sizeof(msg.station_id) - 1);

    uint8_t buf[quailtracker_Status_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if (pb_encode(&stream, quailtracker_Status_fields, &msg)) {
        ble_proto_send(TOPIC_STATUS, buf, stream.bytes_written);
    }
}

static void push_gps_fix(void)
{
    quailtracker_GpsFix msg = quailtracker_GpsFix_init_zero;

    msg.valid = gpsData.valid ? true : false;
    msg.satellites = (uint32_t)gpsData.satellites;
    msg.latitude_e7 = (int32_t)(gpsData.latitude * 10000000.0f);
    msg.longitude_e7 = (int32_t)(gpsData.longitude * 10000000.0f);
    msg.altitude_mm = (int32_t)(gpsData.altitude * 1000.0f);
    msg.fix_type = (uint32_t)gpsData.fix;
    msg.pps_synced = ppsSynced ? true : false;
    msg.pps_count = ppsCount;
    format_utc_string(msg.utc_time, sizeof(msg.utc_time));

    uint8_t buf[quailtracker_GpsFix_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if (pb_encode(&stream, quailtracker_GpsFix_fields, &msg)) {
        ble_proto_send(TOPIC_GPS_FIX, buf, stream.bytes_written);
    }
}

static void push_audio_level(void)
{
    quailtracker_AudioLevel msg = quailtracker_AudioLevel_init_zero;

    /* Read and reset peak */
    int32_t peak = audioPeakLevel;
    audioPeakLevel = 0;

    msg.peak = (uint32_t)(peak < 0 ? 0 : peak);
    msg.activity_pct = (uint32_t)actRatio;
    msg.clip_count = limiterClipCount;
    msg.buf_used = (uint32_t)(ringHead - ringTail);
    msg.buf_capacity = PCM_RING_SIZE;

    uint8_t buf[quailtracker_AudioLevel_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if (pb_encode(&stream, quailtracker_AudioLevel_fields, &msg)) {
        ble_proto_send(TOPIC_AUDIO_LEVEL, buf, stream.bytes_written);
    }
}

static void push_recording_state(void)
{
    quailtracker_RecordingState msg = quailtracker_RecordingState_init_zero;

    msg.active = isRecording ? true : false;
    strncpy(msg.filename, recFilename, sizeof(msg.filename) - 1);
    msg.bytes_written = totalDataBytes;
    msg.duration_s = isRecording ? totalDataBytes / (SAMPLE_RATE * 3) : 0;
    msg.overruns = ringOverruns;

    uint32_t total_kb, free_kb;
    get_sd_space(&total_kb, &free_kb);
    msg.sd_free_kb = free_kb;

    uint8_t buf[quailtracker_RecordingState_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if (pb_encode(&stream, quailtracker_RecordingState_fields, &msg)) {
        ble_proto_send(TOPIC_RECORDING_STATE, buf, stream.bytes_written);
    }
}

static void push_config_dump(void)
{
    quailtracker_Config msg = quailtracker_Config_init_zero;

    msg.gain = (uint32_t)cfg.gain;
    msg.bpf_low_hz = (uint32_t)cfg.bpfLow;
    msg.bpf_high_hz = (uint32_t)cfg.bpfHigh;
    msg.sample_rate = SAMPLE_RATE;
    msg.rec_format = (uint32_t)cfg.recFormat;
    strncpy(msg.station_id, cfg.stationId, sizeof(msg.station_id) - 1);

    /* Trigger */
    msg.trig_enabled = cfg.trigEnabled ? true : false;
    msg.trig_db = (int32_t)cfg.trigDb;
    msg.trig_pre_s = (uint32_t)cfg.trigPre;
    msg.trig_post_s = (uint32_t)cfg.trigPost;

    /* Battery */
    msg.low_bat_pct = (uint32_t)cfg.lowBatPct;
    msg.auto_stop = cfg.autoStop ? true : false;

    /* Activity filter */
    msg.act_mode = (uint32_t)cfg.activityMode;
    msg.act_min_pct = (uint32_t)cfg.activityMinPct;
    msg.act_max_pct = (uint32_t)cfg.activityMaxPct;
    msg.act_hold_s = (uint32_t)cfg.activityHoldSec;

    /* Scheduling */
    msg.sunrise_enabled = cfg.sunriseEnabled ? true : false;
    msg.sunrise_before = (uint32_t)cfg.sunriseBefore;
    msg.sunrise_after = (uint32_t)cfg.sunriseAfter;
    msg.sunset_enabled = cfg.sunsetEnabled ? true : false;
    msg.sunset_before = (uint32_t)cfg.sunsetBefore;
    msg.sunset_after = (uint32_t)cfg.sunsetAfter;

    /* Time windows */
    msg.windows_count = cfg.numWindows;
    for (int i = 0; i < cfg.numWindows && i < 8; i++) {
        msg.windows[i].start_hhmm = (uint32_t)cfg.windows[i * 2];
        msg.windows[i].end_hhmm   = (uint32_t)cfg.windows[i * 2 + 1];
    }

    /* Detection */
    msg.mission_mode = (uint32_t)cfg.missionMode;
    msg.det_threshold = (uint32_t)cfg.detConfThresh;
    msg.det_step_s = (uint32_t)cfg.detWindowStep;

    /* Survey-in */
    msg.survey_lat_e7 = (int32_t)(cfg.surveyLat * 10000000.0f);
    msg.survey_lon_e7 = (int32_t)(cfg.surveyLon * 10000000.0f);
    msg.survey_alt_mm = (int32_t)(cfg.surveyAlt * 1000.0f);
    msg.survey_count = cfg.surveyCount;

    uint8_t buf[quailtracker_Config_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if (pb_encode(&stream, quailtracker_Config_fields, &msg)) {
        ble_proto_send(TOPIC_CONFIG_DUMP, buf, stream.bytes_written);
    }
}

/* ---- Command handlers ---- */

static void handle_command(const uint8_t *payload, size_t payload_len)
{
    quailtracker_Command cmd = quailtracker_Command_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);
    if (!pb_decode(&stream, quailtracker_Command_fields, &cmd)) {
        send_nack(quailtracker_CommandType_CMD_NONE, "decode");
        return;
    }

    printf("BLE PB CMD: %d\r\n", (int)cmd.type);

    switch (cmd.type) {
    case quailtracker_CommandType_CMD_REC_START:
        if (isRecording) { send_nack(cmd.type, "ALREADY"); return; }
        if (!sdMounted)  { send_nack(cmd.type, "NOSD"); return; }
        {
            uint8_t c = CMD_START_REC;
            osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
        }
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_REC_STOP:
        if (!isRecording) { send_nack(cmd.type, "ALREADY"); return; }
        {
            uint8_t c = CMD_STOP_REC;
            osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
        }
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_REC_TOGGLE:
        if (!isRecording && !sdMounted) { send_nack(cmd.type, "NOSD"); return; }
        {
            uint8_t c = isRecording ? CMD_STOP_REC : CMD_START_REC;
            osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
        }
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_SD_MOUNT:
        if (sdMounted) { send_nack(cmd.type, "ALREADY"); return; }
        osMutexAcquire(fileMtxHandle, osWaitForever);
        if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
            sdMounted = 1;
            osMutexRelease(fileMtxHandle);
            printf("BLE PB: SD mounted\r\n");
            send_ack(cmd.type);
        } else {
            osMutexRelease(fileMtxHandle);
            send_nack(cmd.type, "NOSD");
        }
        break;

    case quailtracker_CommandType_CMD_SD_EJECT:
        if (isRecording) { send_nack(cmd.type, "BUSY"); return; }
        if (!sdMounted)  { send_nack(cmd.type, "ALREADY"); return; }
        osMutexAcquire(fileMtxHandle, osWaitForever);
        f_mount(NULL, USERPath, 0);
        USER_disk_deinit();
        sdMounted = 0;
        osMutexRelease(fileMtxHandle);
        printf("BLE PB: SD ejected\r\n");
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_SD_FORMAT:
        if (isRecording) { send_nack(cmd.type, "BUSY"); return; }
        osMutexAcquire(fileMtxHandle, osWaitForever);
        {
            int ok = formatSD();
            osMutexRelease(fileMtxHandle);
            if (ok) {
                printf("BLE PB: SD formatted\r\n");
                send_ack(cmd.type);
            } else {
                send_nack(cmd.type, "NOSD");
            }
        }
        break;

    case quailtracker_CommandType_CMD_GET_STATUS:
        push_status();
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_GET_CONFIG:
        push_config_dump();
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_MODEL_RELOAD:
        if (inferenceTaskHandle != NULL) {
            osThreadFlagsSet(inferenceTaskHandle, 0x02);
            send_ack(cmd.type);
        } else {
            send_nack(cmd.type, "NOTASK");
        }
        break;

    case quailtracker_CommandType_CMD_MODEL_STATUS:
        /* Push status which includes model info */
        push_status();
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_DET_STATUS:
        push_status();
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_SURVEY_START:
        if (gpsData.satellites < SURVEY_MIN_SATS || gpsData.fix < 1) {
            send_nack(cmd.type, "NOSATS");
            return;
        }
        cfg.surveyLat = 0.0f;
        cfg.surveyLon = 0.0f;
        cfg.surveyAlt = 0.0f;
        cfg.surveyCount = 0;
        surveyActive = 1;
        surveyStartTick = HAL_GetTick();
        printf("BLE PB: Survey started\r\n");
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_SURVEY_STOP:
        if (surveyActive) {
            surveyActive = 0;
            configSave();
            printf("BLE PB: Survey stopped (%lu fixes)\r\n",
                   (unsigned long)cfg.surveyCount);
        }
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_SURVEY_CLEAR:
        surveyActive = 0;
        cfg.surveyLat = 0.0f;
        cfg.surveyLon = 0.0f;
        cfg.surveyAlt = 0.0f;
        cfg.surveyCount = 0;
        configSave();
        printf("BLE PB: Survey cleared\r\n");
        send_ack(cmd.type);
        break;

    case quailtracker_CommandType_CMD_LOGS_TOGGLE:
        bleLogEnabled = !bleLogEnabled;
        if (bleLogEnabled) {
            bleLogHead = 0;
            bleLogTail = 0;
        }
        printf("BLE PB: Log forwarding = %s\r\n", bleLogEnabled ? "ON" : "OFF");
        send_ack(cmd.type);
        break;

    default:
        send_nack(cmd.type, "UNKNOWN");
        break;
    }
}

static void handle_set_config(const uint8_t *payload, size_t payload_len)
{
    quailtracker_SetConfig sc = quailtracker_SetConfig_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);
    if (!pb_decode(&stream, quailtracker_SetConfig_fields, &sc)) {
        send_nack(quailtracker_CommandType_CMD_NONE, "decode");
        return;
    }

    if (sc.has_gain) {
        if (sc.gain <= 24 && (sc.gain % 3) == 0) {
            cfg.gain = (uint8_t)sc.gain;
            HAL_MDF_SetGain(&AdfHandle0, (int32_t)sc.gain);
            float gc = 18.0f - (float)((int)sc.gain - 6) * 6.0f;
            mel_set_gain_compensation(gc);
            printf("BLE PB: gain=%lu\r\n", (unsigned long)sc.gain);
        }
    }
    if (sc.has_bpf_low_hz) {
        cfg.bpfLow = (uint16_t)sc.bpf_low_hz;
    }
    if (sc.has_bpf_high_hz) {
        cfg.bpfHigh = (uint16_t)sc.bpf_high_hz;
    }
    if (sc.has_rec_format) {
        if (!isRecording) {
            cfg.recFormat = (uint8_t)sc.rec_format;
            recFormat = (uint8_t)sc.rec_format;
        }
    }
    if (sc.has_station_id) {
        strncpy(cfg.stationId, sc.station_id, sizeof(cfg.stationId) - 1);
        cfg.stationId[sizeof(cfg.stationId) - 1] = '\0';
    }
    if (sc.has_trig_enabled) {
        cfg.trigEnabled = sc.trig_enabled ? 1 : 0;
    }
    if (sc.has_trig_db) {
        cfg.trigDb = (int8_t)sc.trig_db;
    }
    if (sc.has_trig_pre_s) {
        cfg.trigPre = (uint8_t)sc.trig_pre_s;
    }
    if (sc.has_trig_post_s) {
        cfg.trigPost = (uint8_t)sc.trig_post_s;
    }
    if (sc.has_low_bat_pct) {
        cfg.lowBatPct = (uint8_t)sc.low_bat_pct;
    }
    if (sc.has_auto_stop) {
        cfg.autoStop = sc.auto_stop ? 1 : 0;
    }
    if (sc.has_act_mode) {
        cfg.activityMode = (uint8_t)sc.act_mode;
    }
    if (sc.has_act_min_pct) {
        cfg.activityMinPct = (uint8_t)sc.act_min_pct;
    }
    if (sc.has_act_max_pct) {
        cfg.activityMaxPct = (uint8_t)sc.act_max_pct;
    }
    if (sc.has_act_hold_s) {
        cfg.activityHoldSec = (uint8_t)sc.act_hold_s;
    }
    if (sc.has_sunrise_enabled) {
        cfg.sunriseEnabled = sc.sunrise_enabled ? 1 : 0;
    }
    if (sc.has_sunrise_before) {
        cfg.sunriseBefore = (uint16_t)sc.sunrise_before;
    }
    if (sc.has_sunrise_after) {
        cfg.sunriseAfter = (uint16_t)sc.sunrise_after;
    }
    if (sc.has_sunset_enabled) {
        cfg.sunsetEnabled = sc.sunset_enabled ? 1 : 0;
    }
    if (sc.has_sunset_before) {
        cfg.sunsetBefore = (uint16_t)sc.sunset_before;
    }
    if (sc.has_sunset_after) {
        cfg.sunsetAfter = (uint16_t)sc.sunset_after;
    }
    if (sc.windows_count > 0) {
        int n = (int)sc.windows_count;
        if (n > 8) n = 8;
        cfg.numWindows = (uint8_t)n;
        for (int i = 0; i < n; i++) {
            cfg.windows[i * 2]     = (uint16_t)sc.windows[i].start_hhmm;
            cfg.windows[i * 2 + 1] = (uint16_t)sc.windows[i].end_hhmm;
        }
    }
    if (sc.has_mission_mode) {
        cfg.missionMode = (uint8_t)sc.mission_mode;
    }
    if (sc.has_det_threshold) {
        cfg.detConfThresh = (uint8_t)sc.det_threshold;
    }
    if (sc.has_det_step_s) {
        cfg.detWindowStep = (uint8_t)sc.det_step_s;
    }

    if (configSave()) {
        printf("BLE PB: Config saved\r\n");
        send_ack(quailtracker_CommandType_CMD_NONE);
    } else {
        send_nack(quailtracker_CommandType_CMD_NONE, "FLASH");
    }
}

static void handle_subscribe(const uint8_t *payload, size_t payload_len)
{
    quailtracker_Subscribe sub = quailtracker_Subscribe_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);
    if (!pb_decode(&stream, quailtracker_Subscribe_fields, &sub)) {
        return;
    }

    ble_proto_add_subscription((uint8_t)sub.topic, sub.interval_ms);

    /* Immediately push the first value */
    ble_proto_push_topic((uint8_t)sub.topic);
}

static void handle_unsubscribe(const uint8_t *payload, size_t payload_len)
{
    quailtracker_Unsubscribe unsub = quailtracker_Unsubscribe_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);
    if (!pb_decode(&stream, quailtracker_Unsubscribe_fields, &unsub)) {
        return;
    }

    ble_proto_remove_subscription((uint8_t)unsub.topic);
}

static void handle_ping(void)
{
    /* Reply with empty Pong */
    ble_proto_send(TOPIC_PONG, NULL, 0);
}

/* ---- Public API ---- */

void ble_proto_dispatch(uint8_t topic, const uint8_t *payload, size_t payload_len)
{
    switch (topic) {
    case TOPIC_COMMAND:
        handle_command(payload, payload_len);
        break;

    case TOPIC_SET_CONFIG:
        handle_set_config(payload, payload_len);
        break;

    case TOPIC_SUBSCRIBE:
        handle_subscribe(payload, payload_len);
        break;

    case TOPIC_UNSUBSCRIBE:
        handle_unsubscribe(payload, payload_len);
        break;

    case TOPIC_PING:
        handle_ping();
        break;

    default:
        printf("BLE PB: Unknown topic 0x%02X\r\n", topic);
        break;
    }
}

void ble_proto_push_topic(uint8_t topic)
{
    switch (topic) {
    case TOPIC_STATUS:
        push_status();
        break;
    case TOPIC_GPS_FIX:
        push_gps_fix();
        break;
    case TOPIC_AUDIO_LEVEL:
        push_audio_level();
        break;
    case TOPIC_RECORDING_STATE:
        push_recording_state();
        break;
    case TOPIC_CONFIG_DUMP:
        push_config_dump();
        break;
    default:
        break;
    }
}
