/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : FreeRTOS applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_freertos.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fatfs.h"
#include "user_diskio.h"
#include "flac_encoder.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
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
#define CONFIG_MAGIC      0x51544346   /* "QTCF" */
#define CONFIG_VERSION    3
#define CONFIG_FLASH_ADDR 0x081FE000   /* Bank 2, page 127 (last 8KB page of 2MB) */

typedef struct __attribute__((packed, aligned(16))) {
    uint32_t magic;
    uint8_t  version;
    char     stationId[16];   /* null-terminated */
    uint8_t  gain;            /* 0-4 */
    uint8_t  hpf;             /* 0=off, 1=8Hz, 2=48Hz */
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
    float    surveyLat;       /* averaged latitude (decimal degrees) */
    float    surveyLon;       /* averaged longitude (decimal degrees) */
    float    surveyAlt;       /* averaged altitude (meters) */
    uint32_t surveyCount;     /* number of GPS fixes averaged */
    uint8_t  _pad[128 - 89 - 4]; /* pad to 128 bytes: 89 pre-pad + 35 pad + 4 crc */
    uint32_t crc32;           /* CRC-32 over bytes 0..123 */
} device_config_t;

_Static_assert(sizeof(device_config_t) == 128, "device_config_t must be 128 bytes");

/* ---- OTA firmware update state ---- */
typedef enum { OTA_IDLE, OTA_RECEIVING, OTA_COMPLETE } ota_state_t;

typedef struct {
    ota_state_t state;
    uint32_t    imageSize;        /* expected total bytes */
    uint32_t    imageCrc;         /* expected CRC-32 */
    uint32_t    bytesReceived;
    uint32_t    pagesWritten;
    uint32_t    totalPages;
    uint32_t    lastActivityTick; /* for 30s timeout */
    uint16_t    pageBufPos;
    uint8_t     pageBuf[8192] __attribute__((aligned(16)));
} ota_ctx_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define FW_VERSION "0.7.1"

#define OTA_PAGE_SIZE     8192
#define OTA_BANK2_BASE    0x08100000
#define OTA_CONFIG_PAGE   127
#define OTA_CONFIG_MIRROR 0x080FE000
#define OTA_TIMEOUT_MS    30000
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
#define CMD_START_REC 1
#define CMD_STOP_REC  2

/* Shared state from main.c */
#define AUDIO_BUF_SIZE 1024
#define SAMPLE_RATE    48000

extern int32_t audioBuffer[];
extern volatile uint8_t halfComplete;
extern volatile uint8_t fullComplete;
extern int16_t pcmBuffer[];

extern FIL wavFile;
extern uint8_t isRecording;
extern uint8_t sdMounted;
extern uint8_t audioStarted;
extern uint32_t totalDataBytes;
extern uint32_t fileCounter;
extern uint8_t recFormat;
#define REC_FMT_FLAC 0
#define REC_FMT_WAV  1
extern flac_enc_t flacEncoder;
extern char deviceStationId[16];

/* Live audio peak level (updated by audioTask, read by bleTask for $STATUS) */
static volatile int16_t audioPeakLevel = 0;

/* BLE status streaming: 0 = off, otherwise push interval in ms */
static uint32_t streamInterval = 0;
static uint32_t lastStreamTick = 0;

/* BLE log forwarding — ring buffer fed by _write(), drained by BLE task */
#define BLE_LOG_RING_SIZE 512
volatile uint8_t bleLogEnabled = 0;
volatile uint8_t bleLogRing[BLE_LOG_RING_SIZE];
volatile uint16_t bleLogHead = 0;  /* written by _write (producer) */
volatile uint16_t bleLogTail = 0;  /* read by BLE task (consumer) */

/* Queues from main.c */
extern osMessageQueueId_t bleRxQueue;

/* Functions from main.c */
extern int getChar(uint32_t timeoutMs);
extern int getCharGps(uint32_t timeoutMs);
extern int getCharBle(uint32_t timeoutMs);
extern void bleSend(const char *s);
extern void WAV_WriteHeader(FIL *fp, uint32_t sampleRate, uint32_t dataSize);
extern void printMenu(void);
extern void printStatus(void);
extern void startRecording(void);
extern void stopRecording(void);
extern int formatSD(void);

/* PPS time sync state from main.c */
extern volatile uint32_t ppsCount;
extern volatile uint32_t ppsTick;
extern uint32_t ppsUtcTime;
extern uint32_t ppsUtcDate;
extern volatile uint8_t ppsSynced;
extern float ppsLatitude;
extern float ppsLongitude;
extern float ppsAltitude;

/* GPS state */
static gps_data_t gpsData;
static volatile uint8_t gpsRawOutput;

/* Survey-in state */
static uint8_t surveyActive = 0;       /* 1 = survey in progress */
static uint32_t surveyStartTick = 0;   /* HAL tick when survey started */
#define SURVEY_DURATION_MS  300000      /* 5 minutes */
#define SURVEY_MIN_SATS     4           /* minimum satellites for valid fix */

/* BLE state */
static char bleName[32];
static char bleAddr[20];
static uint8_t bleReady;
static volatile uint8_t bleConnected;
static char bleLastResponse[64];

/* Recording filename from main.c (for BLE $STATUS) */
extern char recFilename[];

/* BLE live probe — CLI task sets request, BLE task executes and stores result */
static volatile uint8_t bleLiveProbeReq;
static volatile uint8_t bleLiveProbeReady;
static char bleLiveProbeResp[64];

/* Flash-persisted device config (loaded at BLE task start) */
static device_config_t cfg;

/* OTA firmware update context (~8.2KB BSS) */
static ota_ctx_t ota;
/* USER CODE END Variables */
/* Definitions for audioTask */
osThreadId_t audioTaskHandle;
const osThreadAttr_t audioTask_attributes = {
  .name = "audioTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 1024 * 4
};
/* Definitions for cliTask */
osThreadId_t cliTaskHandle;
const osThreadAttr_t cliTask_attributes = {
  .name = "cliTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for fileMtx */
osMutexId_t fileMtxHandle;
const osMutexAttr_t fileMtx_attributes = {
  .name = "fileMtx"
};
/* Definitions for audioCmdQueue */
osMessageQueueId_t audioCmdQueueHandle;
const osMessageQueueAttr_t audioCmdQueue_attributes = {
  .name = "audioCmdQueue"
};
/* Definitions for audioDmaSem */
osSemaphoreId_t audioDmaSemHandle;
const osSemaphoreAttr_t audioDmaSem_attributes = {
  .name = "audioDmaSem"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void StartGpsTask(void *argument);
static void StartBleTask(void *argument);
static void printGpsStatus(void);
static void printBleStatus(void);
void printBleStatusBrief(void);

/* Flash config */
static void configSetDefaults(device_config_t *c);
static uint32_t configComputeCrc(const device_config_t *c);
static void configLoad(void);
static int configSave(void);

/* Survey-in */
static void surveyAccumulate(float lat, float lon, float alt);

/* BLE protocol */
static void bleSendLine(const char *fmt, ...);
static void bleHandleCommand(const char *cmd);
static void bleHandleStatusEx(const char *tag);
static inline void bleHandleStatus(void) { bleHandleStatusEx("$STATUS"); }
static void bleHandleConfig(void);
static void bleHandleSd(const char *arg);
static void bleHandleSet(const char *args);

/* OTA */
static void bleHandleOta(const char *args);
static uint32_t crc32_compute(const uint8_t *data, uint32_t len);
/* USER CODE END FunctionPrototypes */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* creation of fileMtx */
  fileMtxHandle = osMutexNew(&fileMtx_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */
  /* creation of audioDmaSem */
  audioDmaSemHandle = osSemaphoreNew(1, 1, &audioDmaSem_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* Consume initial count so first acquire blocks until DMA ISR signals */
  osSemaphoreAcquire(audioDmaSemHandle, 0);
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */
  /* creation of audioCmdQueue */
  audioCmdQueueHandle = osMessageQueueNew (5, sizeof(uint8_t), &audioCmdQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */
  /* creation of audioTask */
  audioTaskHandle = osThreadNew(StartAudioTask, NULL, &audioTask_attributes);

  /* creation of cliTask */
  cliTaskHandle = osThreadNew(StartCliTask, NULL, &cliTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  {
    const osThreadAttr_t gpsTask_attributes = {
      .name = "gpsTask",
      .priority = (osPriority_t) osPriorityNormal,
      .stack_size = 512 * 4
    };
    osThreadNew(StartGpsTask, NULL, &gpsTask_attributes);
  }
  {
    const osThreadAttr_t bleTask_attributes = {
      .name = "bleTask",
      .priority = (osPriority_t) osPriorityNormal,
      .stack_size = 1024 * 4
    };
    osThreadNew(StartBleTask, NULL, &bleTask_attributes);
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}
/* USER CODE BEGIN Header_StartAudioTask */
/**
* @brief Function implementing the audioTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAudioTask */
void StartAudioTask(void *argument)
{
  /* USER CODE BEGIN audioTask */
  for (;;)
  {
    /* Block until DMA half/full callback releases semaphore */
    osSemaphoreAcquire(audioDmaSemHandle, osWaitForever);

    /* Check for start/stop commands from CLI task */
    uint8_t cmd;
    while (osMessageQueueGet(audioCmdQueueHandle, &cmd, NULL, 0) == osOK) {
      if (cmd == CMD_START_REC) startRecording();
      else if (cmd == CMD_STOP_REC) stopRecording();
    }

    /* Always consume DMA flags and track peak level (even when not recording) */
    int32_t *src = NULL;

    if (halfComplete) {
      halfComplete = 0;
      src = &audioBuffer[0];
    } else if (fullComplete) {
      fullComplete = 0;
      src = &audioBuffer[AUDIO_BUF_SIZE / 2];
    }

    if (src) {
      /* MDF DFLTDR is left-justified: 25-bit Sinc4 output in bits [31:7].
       * >> 16 extracts bits [31:16] = top 16 bits. */
      int16_t blockPeak = 0;
      for (int i = 0; i < AUDIO_BUF_SIZE / 2; i++) {
        int16_t s = (int16_t)(src[i] >> 16);
        pcmBuffer[i] = s;
        int16_t a = s < 0 ? -s : s;
        if (a > blockPeak) blockPeak = a;
      }
      /* Update global peak (max of current peak and this block) */
      if (blockPeak > audioPeakLevel) audioPeakLevel = blockPeak;

      if (isRecording) {
        if (recFormat == REC_FMT_WAV) {
          /* Raw PCM write */
          osMutexAcquire(fileMtxHandle, osWaitForever);
          UINT bw;
          FRESULT fres = f_write(&wavFile, pcmBuffer, AUDIO_BUF_SIZE / 2 * sizeof(int16_t), &bw);
          if (fres != FR_OK) {
            printf("f_write FAILED: %d at %lu bytes\r\n", fres, (unsigned long)totalDataBytes);
            f_close(&wavFile);
            isRecording = 0;
          }
          totalDataBytes += bw;

          /* Sync every ~1 second */
          if ((totalDataBytes % (SAMPLE_RATE * 2)) < (AUDIO_BUF_SIZE / 2 * sizeof(int16_t))) {
            f_sync(&wavFile);
          }
          osMutexRelease(fileMtxHandle);
        } else {
          /* FLAC encode — accumulates 8 DMA callbacks into one 4096-sample block */
          uint32_t encoded = flac_enc_process(&flacEncoder, pcmBuffer, AUDIO_BUF_SIZE / 2);
          if (encoded > 0) {
            osMutexAcquire(fileMtxHandle, osWaitForever);
            UINT bw;
            FRESULT fres = f_write(&wavFile, flacEncoder.outBuf, encoded, &bw);
            if (fres != FR_OK) {
              printf("f_write FAILED: %d at %lu bytes\r\n", fres, (unsigned long)totalDataBytes);
              f_close(&wavFile);
              isRecording = 0;
            }
            totalDataBytes += bw;

            /* Sync every ~8 frames (~680ms) */
            if ((flacEncoder.frameNumber % 8) == 0) {
              f_sync(&wavFile);
            }
            osMutexRelease(fileMtxHandle);
          }
        }
      }
    }
  }
  /* USER CODE END audioTask */
}

/* USER CODE BEGIN Header_StartCliTask */
/**
* @brief Function implementing the cliTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCliTask */
void StartCliTask(void *argument)
{
  /* USER CODE BEGIN cliTask */
  /* Re-enable USART1 RXNE — the CubeMX-generated BSP_COM_Init after
   * osKernelInitialize() re-inits USART1 and clears our earlier enable. */
  USART1->CR1 |= USART_CR1_RXNEIE_RXFNEIE;

  /* Let GPS + BLE tasks finish their init messages before showing menu */
  osDelay(3000);
  printMenu();

  for (;;)
  {
    int c = getChar(10);  /* block up to 10ms — replaces poll + osDelay(10) */
    if (c >= 0) {
      switch (c) {
      case '1':
        printStatus();
        printMenu();
        break;

      case '2':
      {
        uint8_t cmd = CMD_START_REC;
        osMessageQueuePut(audioCmdQueueHandle, &cmd, 0, 0);
        /* Give audio task time to process command before reprinting menu */
        osDelay(50);
        printMenu();
        break;
      }

      case '3':
      {
        uint8_t cmd = CMD_STOP_REC;
        osMessageQueuePut(audioCmdQueueHandle, &cmd, 0, 0);
        osDelay(50);
        printMenu();
        break;
      }

      case '4':
        if (sdMounted) {
          FATFS *fs;
          DWORD fre_clust;
          printf("\r\n=== SD Card Info ===\r\n");
          osMutexAcquire(fileMtxHandle, osWaitForever);
          if (f_getfree("", &fre_clust, &fs) == FR_OK) {
            DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
            DWORD fre_sect = fre_clust * fs->csize;
            printf("Total: %lu KB\r\n", (unsigned long)(tot_sect / 2));
            printf("Free:  %lu KB\r\n", (unsigned long)(fre_sect / 2));
          }
          osMutexRelease(fileMtxHandle);
        } else {
          printf("SD card not mounted!\r\n");
        }
        printMenu();
        break;

      case '5':
        if (isRecording) {
          printf("Stop recording first!\r\n");
        } else {
          printf("Format SD card? ALL DATA WILL BE ERASED. (y/n) > ");
          int confirm = -1;
          while (confirm < 0) {
            confirm = getChar(osWaitForever);
            if (confirm == '\r' || confirm == '\n') confirm = -1;
          }
          printf("%c\r\n", confirm);
          if (confirm == 'y' || confirm == 'Y') {
            osMutexAcquire(fileMtxHandle, osWaitForever);
            formatSD();
            osMutexRelease(fileMtxHandle);
          } else {
            printf("Cancelled\r\n");
          }
        }
        printMenu();
        break;

      case '6':
        if (isRecording) {
          printf("Stop recording first!\r\n");
        } else if (!sdMounted) {
          printf("Already ejected\r\n");
        } else {
          extern char USERPath[];
          osMutexAcquire(fileMtxHandle, osWaitForever);
          f_mount(NULL, USERPath, 0);
          USER_disk_deinit();
          sdMounted = 0;
          osMutexRelease(fileMtxHandle);
          printf("SD card ejected - safe to remove\r\n");
        }
        printMenu();
        break;

      case '7':
      {
        if (sdMounted) {
          printf("Already mounted\r\n");
        } else {
          extern FATFS USERFatFS;
          extern char USERPath[];
          printf("Mounting... ");
          osMutexAcquire(fileMtxHandle, osWaitForever);
          if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
            sdMounted = 1;
            printf("OK\r\n");
          } else {
            printf("Not readable, formatting...\r\n");
            if (formatSD()) {
              printf("SD card ready\r\n");
            } else {
              printf("No card detected\r\n");
            }
          }
          osMutexRelease(fileMtxHandle);
        }
        printMenu();
        break;
      }

      case '8':
        printGpsStatus();
        printMenu();
        break;

      case '9':
        printBleStatus();
        printMenu();
        break;

      case 'f':
      case 'F':
        if (isRecording) {
          printf("Stop recording first!\r\n");
        } else {
          recFormat = (recFormat == REC_FMT_WAV) ? REC_FMT_FLAC : REC_FMT_WAV;
          printf("Recording format: %s\r\n", recFormat == REC_FMT_WAV ? "WAV" : "FLAC");
        }
        printMenu();
        break;

      case 'g':
      case 'G':
        gpsRawOutput = !gpsRawOutput;
        printf("GPS raw output: %s\r\n", gpsRawOutput ? "ON" : "OFF");
        printMenu();
        break;

      case 'r':
      case 'R':
      {
        uint8_t cmd = isRecording ? CMD_STOP_REC : CMD_START_REC;
        osMessageQueuePut(audioCmdQueueHandle, &cmd, 0, 0);
        osDelay(50);
        printMenu();
        break;
      }

      case 's':
      case 'S':
        if (surveyActive) {
          surveyActive = 0;
          configSave();
          printf("Survey stopped (%lu fixes)\r\n",
                 (unsigned long)cfg.surveyCount);
        } else {
          if (gpsData.satellites < SURVEY_MIN_SATS || gpsData.fix < 1) {
            printf("Need 4+ satellites to start survey (currently %d)\r\n",
                   gpsData.satellites);
          } else {
            cfg.surveyLat = 0.0f;
            cfg.surveyLon = 0.0f;
            cfg.surveyAlt = 0.0f;
            cfg.surveyCount = 0;
            surveyActive = 1;
            surveyStartTick = HAL_GetTick();
            printf("Survey started (5 min)\r\n");
          }
        }
        printMenu();
        break;

      case '\r':
      case '\n':
        break;
      }
    }
  }
  /* USER CODE END cliTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* ========================= GPS / NMEA ========================= */

/* Advance to the n-th comma-separated field in an NMEA sentence */
static const char *nmea_field(const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        s = strchr(s, ',');
        if (!s) return "";
        s++;
    }
    return s;
}

/* Parse DDDMM.MMMM → decimal degrees (no strtod/atof needed) */
static float nmea_parse_coord(const char *s)
{
    int32_t whole = 0, frac = 0, frac_div = 1;
    while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s++ - '0'); }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac = frac * 10 + (*s++ - '0');
            frac_div *= 10;
        }
    }
    int deg = whole / 100;
    float minutes = (float)(whole % 100) + (float)frac / (float)frac_div;
    return (float)deg + minutes / 60.0f;
}

static uint32_t nmea_parse_int(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); }
    return v;
}

static void nmea_parse_rmc(const char *line)
{
    /* $G?RMC,time,status,lat,N/S,lon,E/W,spd,crs,date,... */
    const char *f;

    f = nmea_field(line, 1);  gpsData.utc_time = nmea_parse_int(f);
    f = nmea_field(line, 2);  gpsData.valid = (*f == 'A') ? 1 : 0;

    f = nmea_field(line, 3);
    if (*f) {
        gpsData.latitude = nmea_parse_coord(f);
        f = nmea_field(line, 4);
        if (*f == 'S') gpsData.latitude = -gpsData.latitude;
    }

    f = nmea_field(line, 5);
    if (*f) {
        gpsData.longitude = nmea_parse_coord(f);
        f = nmea_field(line, 6);
        if (*f == 'W') gpsData.longitude = -gpsData.longitude;
    }

    f = nmea_field(line, 9);  gpsData.utc_date = nmea_parse_int(f);

    /* Latch PPS-synced time: RMC arrives ~300ms after PPS edge,
     * so its timestamp corresponds to the most recent PPS pulse */
    if (gpsData.valid && ppsCount > 0) {
        ppsUtcTime = gpsData.utc_time;
        ppsUtcDate = gpsData.utc_date;
        ppsLatitude = gpsData.latitude;
        ppsLongitude = gpsData.longitude;
        ppsAltitude = gpsData.altitude;
        ppsSynced = 1;

        /* Accumulate GPS fix for survey-in (PPS-synchronized fixes only) */
        surveyAccumulate(gpsData.latitude, gpsData.longitude, gpsData.altitude);
    }
}

static void nmea_parse_gga(const char *line)
{
    /* $G?GGA,time,lat,N/S,lon,E/W,fix,sats,hdop,alt,... */
    const char *f;

    f = nmea_field(line, 1);  gpsData.utc_time = nmea_parse_int(f);

    f = nmea_field(line, 2);
    if (*f) {
        gpsData.latitude = nmea_parse_coord(f);
        f = nmea_field(line, 3);
        if (*f == 'S') gpsData.latitude = -gpsData.latitude;
    }

    f = nmea_field(line, 4);
    if (*f) {
        gpsData.longitude = nmea_parse_coord(f);
        f = nmea_field(line, 5);
        if (*f == 'W') gpsData.longitude = -gpsData.longitude;
    }

    f = nmea_field(line, 6);  gpsData.fix = (uint8_t)nmea_parse_int(f);
    f = nmea_field(line, 7);  gpsData.satellites = (uint8_t)nmea_parse_int(f);

    /* Field 9: altitude above MSL in meters (e.g. "728.3") */
    f = nmea_field(line, 9);
    if (*f && (*f == '-' || (*f >= '0' && *f <= '9'))) {
        int32_t neg = 0, whole = 0, frac = 0, frac_div = 1;
        const char *p = f;
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') { whole = whole * 10 + (*p++ - '0'); }
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9') {
                frac = frac * 10 + (*p++ - '0');
                frac_div *= 10;
            }
        }
        gpsData.altitude = (float)whole + (float)frac / (float)frac_div;
        if (neg) gpsData.altitude = -gpsData.altitude;
    }
}

static void nmea_process_line(const char *line)
{
    if (line[0] != '$' || line[1] != 'G') return;

    if (strncmp(line + 3, "RMC,", 4) == 0)
        nmea_parse_rmc(line);
    else if (strncmp(line + 3, "GGA,", 4) == 0)
        nmea_parse_gga(line);

    if (gpsRawOutput)
        printf("%s\r\n", line);
}

static void printGpsStatus(void)
{
    printf("\r\n=== GPS Status ===\r\n");
    printf("Fix:        %s\r\n",
           gpsData.fix ? (gpsData.fix == 2 ? "DGPS" : "GPS") : "No fix");
    printf("Valid:      %s\r\n", gpsData.valid ? "Yes" : "No");
    printf("Satellites: %d\r\n", gpsData.satellites);
    if (gpsData.valid) {
        /* Integer-only coordinate printing (newlib-nano safe) */
        float lat = gpsData.latitude, lon = gpsData.longitude;
        char ls = ' ', os = ' ';
        if (lat < 0) { ls = '-'; lat = -lat; }
        if (lon < 0) { os = '-'; lon = -lon; }
        int32_t lat_d = (int32_t)lat, lon_d = (int32_t)lon;
        int32_t lat_f = (int32_t)((lat - (float)lat_d) * 1000000.0f);
        int32_t lon_f = (int32_t)((lon - (float)lon_d) * 1000000.0f);
        printf("Latitude:  %c%ld.%06ld\r\n", ls, (long)lat_d, (long)lat_f);
        printf("Longitude: %c%ld.%06ld\r\n", os, (long)lon_d, (long)lon_f);
        {
            float a = gpsData.altitude;
            char as = ' ';
            if (a < 0) { as = '-'; a = -a; }
            int32_t a_d = (int32_t)a;
            int32_t a_f = (int32_t)((a - (float)a_d) * 10.0f);
            printf("Altitude:  %c%ld.%01ld m\r\n", as, (long)a_d, (long)a_f);
        }
    }
    if (gpsData.utc_time) {
        printf("UTC Time:   %02lu:%02lu:%02lu\r\n",
               (unsigned long)(gpsData.utc_time / 10000),
               (unsigned long)((gpsData.utc_time / 100) % 100),
               (unsigned long)(gpsData.utc_time % 100));
    }
    if (gpsData.utc_date) {
        printf("UTC Date:   %02lu/%02lu/%02lu\r\n",
               (unsigned long)(gpsData.utc_date / 10000),
               (unsigned long)((gpsData.utc_date / 100) % 100),
               (unsigned long)(gpsData.utc_date % 100));
    }
    printf("PPS:\r\n");
    printf("  Count:    %lu\r\n", (unsigned long)ppsCount);
    printf("  Synced:   %s\r\n", ppsSynced ? "Yes" : "No");
    if (ppsCount > 0) {
        printf("  Last PPS: %lu ms ago\r\n",
               (unsigned long)(HAL_GetTick() - ppsTick));
    }
    if (ppsSynced) {
        printf("  UTC:      20%02lu/%02lu/%02lu %02lu:%02lu:%02lu\r\n",
               (unsigned long)(ppsUtcDate % 100),
               (unsigned long)((ppsUtcDate / 100) % 100),
               (unsigned long)(ppsUtcDate / 10000),
               (unsigned long)(ppsUtcTime / 10000),
               (unsigned long)((ppsUtcTime / 100) % 100),
               (unsigned long)(ppsUtcTime % 100));
    }
    printf("Survey:\r\n");
    printf("  Active:   %s\r\n", surveyActive ? "Yes" : "No");
    printf("  Fixes:    %lu\r\n", (unsigned long)cfg.surveyCount);
    if (cfg.surveyCount > 0) {
        float slat = cfg.surveyLat, slon = cfg.surveyLon, salt = cfg.surveyAlt;
        char sls = ' ', sos = ' ', sas = ' ';
        if (slat < 0) { sls = '-'; slat = -slat; }
        if (slon < 0) { sos = '-'; slon = -slon; }
        if (salt < 0) { sas = '-'; salt = -salt; }
        int32_t slat_d = (int32_t)slat, slon_d = (int32_t)slon, salt_d = (int32_t)salt;
        int32_t slat_f = (int32_t)((slat - (float)slat_d) * 1000000.0f);
        int32_t slon_f = (int32_t)((slon - (float)slon_d) * 1000000.0f);
        int32_t salt_f = (int32_t)((salt - (float)salt_d) * 10.0f);
        printf("  Lat:      %c%ld.%06ld\r\n", sls, (long)slat_d, (long)slat_f);
        printf("  Lon:      %c%ld.%06ld\r\n", sos, (long)slon_d, (long)slon_f);
        printf("  Alt:      %c%ld.%01ld m\r\n", sas, (long)salt_d, (long)salt_f);
    }
    printf("Raw output: %s\r\n", gpsRawOutput ? "ON" : "OFF");
    printf("==================\r\n");
}

static void StartGpsTask(void *argument)
{
    char buf[128];
    int pos = 0;

    printf("GPS: Listening on LPUART1 (9600 baud)\r\n");

    for (;;) {
        int c = getCharGps(osWaitForever);
        if (c >= 0) {
            if (c == '\n') {
                if (pos > 0 && buf[pos - 1] == '\r') pos--;
                buf[pos] = '\0';
                if (pos > 0) nmea_process_line(buf);
                pos = 0;
            } else if (pos < (int)sizeof(buf) - 1) {
                buf[pos++] = (char)c;
            }
        }
    }
}

/* ========================= Survey Accessors (for main.c) ========================= */
uint32_t configGetSurveyCount(void) { return cfg.surveyCount; }
float configGetSurveyLat(void) { return cfg.surveyLat; }
float configGetSurveyLon(void) { return cfg.surveyLon; }
float configGetSurveyAlt(void) { return cfg.surveyAlt; }

/* ========================= Survey-In ========================= */

static void surveyAccumulate(float lat, float lon, float alt)
{
    /* Skip fixes with insufficient satellite coverage */
    if (gpsData.satellites < SURVEY_MIN_SATS || gpsData.fix < 1)
        return;

    if (surveyActive) {
        /* Check if survey duration has elapsed */
        if ((HAL_GetTick() - surveyStartTick) >= SURVEY_DURATION_MS) {
            surveyActive = 0;
            if (configSave()) {
                printf("Survey: Complete (%lu fixes)\r\n",
                       (unsigned long)cfg.surveyCount);
            }
            return;
        }

        /* Incremental mean: avg += (new - avg) / count */
        cfg.surveyCount++;
        cfg.surveyLat += (lat - cfg.surveyLat) / (float)cfg.surveyCount;
        cfg.surveyLon += (lon - cfg.surveyLon) / (float)cfg.surveyCount;
        cfg.surveyAlt += (alt - cfg.surveyAlt) / (float)cfg.surveyCount;
    } else if (cfg.surveyCount > 0) {
        /* Continuous refinement: keep averaging after initial survey */
        cfg.surveyCount++;
        cfg.surveyLat += (lat - cfg.surveyLat) / (float)cfg.surveyCount;
        cfg.surveyLon += (lon - cfg.surveyLon) / (float)cfg.surveyCount;
        cfg.surveyAlt += (alt - cfg.surveyAlt) / (float)cfg.surveyCount;

        /* Save periodically (every 100 fixes) to avoid flash wear */
        if ((cfg.surveyCount % 100) == 0) {
            configSave();
        }
    }
}

/* ========================= BLE / HM-19 ========================= */

/* Send AT command and wait for response (blocking, with timeout) */
static int bleSendCmd(const char *cmd, char *resp, int respSize, int timeoutMs)
{
    bleSend(cmd);
    int pos = 0;
    uint32_t start = HAL_GetTick();

    while (1) {
        uint32_t elapsed = HAL_GetTick() - start;
        if (elapsed >= (uint32_t)timeoutMs) break;
        uint32_t remaining = (uint32_t)timeoutMs - elapsed;
        int c = getCharBle(remaining);
        if (c >= 0) {
            if (c == '\n' || c == '\r') {
                if (pos > 0) {
                    resp[pos] = '\0';
                    return pos;
                }
            } else if (pos < respSize - 1) {
                resp[pos++] = (char)c;
            }
        }
    }
    /* Some HM-19 responses don't end with newline (e.g. "OK") */
    if (pos > 0) {
        resp[pos] = '\0';
        return pos;
    }
    return 0;
}

static void printBleStatus(void)
{
    printf("\r\n=== BLE Status ===\r\n");
    printf("Module:    %s\r\n", bleReady ? "HM-19 (CC2640)" : "Not detected");
    if (bleReady) {
        printf("Name:      %s\r\n", bleName);
        printf("Address:   %s\r\n", bleAddr);
        printf("Connected: %s\r\n", bleConnected ? "Yes" : "No");

        /* Request BLE task to run AT probe (avoids queue contention) */
        bleLiveProbeReady = 0;
        bleLiveProbeReq = 1;
        uint32_t start = HAL_GetTick();
        while (!bleLiveProbeReady && (HAL_GetTick() - start < 2000))
            osDelay(50);

        if (bleLiveProbeReady)
            printf("AT test:   %s\r\n", bleLiveProbeResp);
        else
            printf("AT test:   Timeout\r\n");
    }
    printf("==================\r\n");
}

/* Brief BLE status for main status display (no live AT probe) */
void printBleStatusBrief(void)
{
    printf("BLE:\r\n");
    printf("  Module: %s\r\n", bleReady ? "HM-19 (CC2640)" : "Not detected");
    if (bleReady) {
        printf("  Name:   %s\r\n", bleName);
        printf("  Addr:   %s\r\n", bleAddr);
        printf("  Link:   %s\r\n", bleConnected ? "Connected" : "Idle");
    }
}

static void StartBleTask(void *argument)
{
    char resp[64];

    /* Load config from flash (or set defaults) */
    configLoad();

    /* Apply persisted config to runtime state */
    extern MDF_FilterConfigTypeDef AdfFilterConfig0;
    AdfFilterConfig0.Gain = (int32_t)cfg.gain * 6;  /* gain index 0-4 → 0,6,12,18,24 */
    recFormat = cfg.recFormat;

    printf("BLE: Config loaded (station=%s, gain=%d, fmt=%s)\r\n",
           cfg.stationId, cfg.gain,
           cfg.recFormat == REC_FMT_FLAC ? "FLAC" : "WAV");

    printf("BLE: Probing HM-19 on USART3 (PC10/PC11, 9600 baud)\r\n");

    /* Wait for module to power up */
    osDelay(500);

    /* Flush any bytes received during power-up (noise, banners, etc.) */
    {
        uint8_t discard;
        while (osMessageQueueGet(bleRxQueue, &discard, NULL, 0) == osOK) {}
    }

    /* Probe with AT (HM-19 expects bare commands, no \r\n) */
    if (bleSendCmd("AT", resp, sizeof(resp), 1000) > 0) {
        printf("BLE: AT -> %s\r\n", resp);
        bleReady = 1;
    } else {
        printf("BLE: No response from module\r\n");
        bleReady = 0;
    }

    if (bleReady) {
        /* Get module name */
        if (bleSendCmd("AT+NAME?", resp, sizeof(resp), 1000) > 0) {
            /* Response format: OK+NAME:xxx or +NAME=xxx depending on firmware */
            const char *name = resp;
            const char *p = strchr(resp, ':');
            if (!p) p = strchr(resp, '=');
            if (p) name = p + 1;
            strncpy(bleName, name, sizeof(bleName) - 1);
            bleName[sizeof(bleName) - 1] = '\0';
            printf("BLE: Name = %s\r\n", bleName);
        }

        /* Get MAC address */
        if (bleSendCmd("AT+ADDR?", resp, sizeof(resp), 1000) > 0) {
            const char *addr = resp;
            const char *p = strchr(resp, ':');
            if (!p) p = strchr(resp, '=');
            if (p) addr = p + 1;
            strncpy(bleAddr, addr, sizeof(bleAddr) - 1);
            bleAddr[sizeof(bleAddr) - 1] = '\0';
            printf("BLE: Addr = %s\r\n", bleAddr);
        }
    }

    /* Main loop: read incoming BLE data (transparent mode) */
    char buf[128];
    int pos = 0;
    for (;;) {
        /* OTA inactivity timeout */
        if (ota.state == OTA_RECEIVING &&
            (HAL_GetTick() - ota.lastActivityTick) > OTA_TIMEOUT_MS) {
            printf("OTA: Timeout — aborting\r\n");
            ota.state = OTA_IDLE;
            bleSendLine("$ERR,TIMEOUT");
        }

        /* Handle live AT probe requests from CLI task */
        if (bleLiveProbeReq) {
            bleLiveProbeReq = 0;
            if (bleSendCmd("AT", bleLiveProbeResp, sizeof(bleLiveProbeResp), 500) == 0)
                strcpy(bleLiveProbeResp, "No response");
            bleLiveProbeReady = 1;
            continue;
        }

        int c = getCharBle(100);  /* short timeout to check probe requests */
        if (c >= 0) {
            if (c == '\n') {
                if (pos > 0 && buf[pos - 1] == '\r') pos--;
                buf[pos] = '\0';
                if (pos > 0) {
                    if (buf[0] == '$') {
                        /* Protocol command from app */
                        bleHandleCommand(buf);
                    } else if (strncmp(buf, "OK+CONN", 7) == 0) {
                        bleConnected = 1;
                        printf("BLE: Connected\r\n");
                    } else if (strncmp(buf, "OK+LOST", 7) == 0) {
                        bleConnected = 0;
                        streamInterval = 0;  /* stop streaming on disconnect */
                        bleLogEnabled = 0;
                        printf("BLE: Disconnected\r\n");
                    }
                    /* else: ignore unknown lines */
                    strncpy(bleLastResponse, buf, sizeof(bleLastResponse) - 1);
                    bleLastResponse[sizeof(bleLastResponse) - 1] = '\0';
                }
                pos = 0;
            } else if (pos < (int)sizeof(buf) - 1) {
                buf[pos++] = (char)c;
            }
        }

        /* Push status if streaming is enabled and interval has elapsed */
        if (streamInterval > 0 && bleConnected &&
            (HAL_GetTick() - lastStreamTick) >= streamInterval) {
            lastStreamTick = HAL_GetTick();
            bleHandleStatusEx("$STREAM");
        }

        /* Drain log ring buffer over BLE */
        if (bleLogEnabled && bleConnected) {
            char logChunk[64];
            int n = 0;
            while (n < (int)sizeof(logChunk) - 1) {
                uint16_t t = bleLogTail;
                if (t == bleLogHead) break;
                logChunk[n++] = (char)bleLogRing[t];
                bleLogTail = (uint16_t)((t + 1) % BLE_LOG_RING_SIZE);
            }
            if (n > 0) {
                logChunk[n] = '\0';
                bleSend(logChunk);
            }
        }
    }
}

/* ========================= Flash Config ========================= */

static void configSetDefaults(device_config_t *c)
{
    memset(c, 0xFF, sizeof(*c));  /* match erased flash */
    c->magic = CONFIG_MAGIC;
    c->version = CONFIG_VERSION;
    strncpy(c->stationId, "QT001", sizeof(c->stationId));
    c->gain = 2;
    c->hpf = 0;
    c->recFormat = REC_FMT_FLAC;
    c->sunriseEnabled = 1;
    c->sunriseBefore = 30;
    c->sunriseAfter = 60;
    c->sunsetEnabled = 1;
    c->sunsetBefore = 30;
    c->sunsetAfter = 30;
    c->numWindows = 0;
    memset(c->windows, 0, sizeof(c->windows));
    c->trigEnabled = 0;
    c->trigDb = -40;
    c->trigPre = 2;
    c->trigPost = 5;
    c->lowBatPct = 10;
    c->autoStop = 1;
    c->surveyLat = 0.0f;
    c->surveyLon = 0.0f;
    c->surveyAlt = 0.0f;
    c->surveyCount = 0;
    memset(c->_pad, 0xFF, sizeof(c->_pad));
    c->crc32 = 0;
}

/* CRC-32 lookup table (standard Ethernet/ZIP polynomial) */
static const uint32_t crc_table[] = {
    0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
    0x0EDB8832,0x79DCB8A4,0xE0D5E91B,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D09,0x90BF1D9F,
    0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
    0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
    0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
    0x35B5A8FA,0x42B2986C,0xDBBBB979,0xACBCB9CF,0x32D86CE3,0x45DF5C75,0xDCD60DCB,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
    0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
    0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0D69,0x086D3D2F,0x91646C97,0xE6635C01,
    0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
    0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
    0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
    0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7822,
    0x3D6029B1,0x0A672127,0x9960B3BB,0xEE67C34D,0x78410030,0x0F460F0F,0x96470F69,0xE1405B95,
    0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
    0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
    0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
    0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
    0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
    0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
    0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
    0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA8670DC5,0x316E8EEF,0x4669BE79,
    0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
    0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,0xC2D766A7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
    0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
    0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
    0x86D3D2D4,0xF1D4E242,0x68DDB3F6,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
    0x88085AE6,0xFF0F6B70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
    0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
    0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
    0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD706FF,0x54DE5729,0x23D967BF,
    0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D,
};

static uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

static uint32_t configComputeCrc(const device_config_t *c)
{
    return crc32_compute((const uint8_t *)c, sizeof(device_config_t) - 4);
}

static void configLoad(void)
{
    const device_config_t *flash = (const device_config_t *)CONFIG_FLASH_ADDR;

    memcpy(&cfg, flash, sizeof(cfg));

    if (cfg.magic == CONFIG_MAGIC &&
        cfg.version == CONFIG_VERSION &&
        cfg.crc32 == configComputeCrc(&cfg)) {
        printf("Config: Loaded from flash (station=%s)\r\n", cfg.stationId);
        strncpy(deviceStationId, cfg.stationId, sizeof(deviceStationId));
        return;
    }

    printf("Config: Invalid/empty — writing defaults\r\n");
    configSetDefaults(&cfg);
    cfg.crc32 = configComputeCrc(&cfg);
    configSave();
    strncpy(deviceStationId, cfg.stationId, sizeof(deviceStationId));
}

static int configSave(void)
{
    cfg.crc32 = configComputeCrc(&cfg);

    HAL_FLASH_Unlock();

    /* Erase page 127 of Bank 2 */
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_2;
    erase.Page = 127;
    erase.NbPages = 1;
    uint32_t pageError = 0;

    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK) {
        printf("Config: Flash erase FAILED (err=0x%lx)\r\n",
               (unsigned long)HAL_FLASH_GetError());
        HAL_FLASH_Lock();
        return 0;
    }

    /* Program 8 quadwords (128 bytes = 8 × 16 bytes) */
    const uint8_t *src = (const uint8_t *)&cfg;
    for (int i = 0; i < 8; i++) {
        uint32_t addr = CONFIG_FLASH_ADDR + (uint32_t)(i * 16);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr,
                              (uint32_t)(src + i * 16)) != HAL_OK) {
            printf("Config: Flash program FAILED at offset %d (err=0x%lx)\r\n",
                   i * 16, (unsigned long)HAL_FLASH_GetError());
            HAL_FLASH_Lock();
            return 0;
        }
    }

    HAL_FLASH_Lock();

    /* Invalidate ICACHE so readback comes from flash, not stale cache */
    HAL_ICACHE_Invalidate();

    /* Verify readback */
    if (memcmp((const void *)CONFIG_FLASH_ADDR, &cfg, sizeof(cfg)) != 0) {
        printf("Config: Flash verify FAILED\r\n");
        return 0;
    }

    return 1;
}

/* ========================= BLE Protocol ========================= */

#include <stdarg.h>

static void bleSendLine(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (len > 0) {
        if (len > (int)sizeof(line) - 2) len = (int)sizeof(line) - 2;
        line[len] = '\n';
        line[len + 1] = '\0';
        bleSend(line);
    }
}

static void bleHandleStatusEx(const char *tag)
{
    extern MDF_FilterConfigTypeDef AdfFilterConfig0;

    bleSendLine("%s", tag);
    bleSendLine("id=%s", cfg.stationId);
    bleSendLine("fw=" FW_VERSION);

    /* Battery — not yet implemented */
    bleSendLine("bat_v=0");
    bleSendLine("bat_pct=0");
    bleSendLine("bat_lvl=0");

    /* GPS */
    bleSendLine("gps_valid=%d", gpsData.valid);
    bleSendLine("gps_sats=%d", gpsData.satellites);

    int32_t lat7 = (int32_t)(gpsData.latitude * 10000000.0f);
    int32_t lon7 = (int32_t)(gpsData.longitude * 10000000.0f);
    bleSendLine("gps_lat=%ld", (long)lat7);
    bleSendLine("gps_lon=%ld", (long)lon7);
    {
        int32_t alt_mm = (int32_t)(gpsData.altitude * 1000.0f);
        bleSendLine("gps_alt=%ld", (long)alt_mm);
    }

    /* GPS time: reformat DDMMYY + HHMMSS → YYYYMMDDHHmmss */
    if (ppsSynced && ppsUtcDate != 0) {
        uint32_t dd = ppsUtcDate / 10000;
        uint32_t mm = (ppsUtcDate / 100) % 100;
        uint32_t yy = ppsUtcDate % 100;
        uint32_t hh = ppsUtcTime / 10000;
        uint32_t mn = (ppsUtcTime / 100) % 100;
        uint32_t ss = ppsUtcTime % 100;
        bleSendLine("gps_time=20%02lu%02lu%02lu%02lu%02lu%02lu",
                    (unsigned long)yy, (unsigned long)mm, (unsigned long)dd,
                    (unsigned long)hh, (unsigned long)mn, (unsigned long)ss);
    } else {
        bleSendLine("gps_time=");
    }

    /* GPS extended */
    bleSendLine("gps_fix=%d", gpsData.fix);

    /* GPS date: reformat DDMMYY → YYYY-MM-DD */
    if (gpsData.utc_date != 0) {
        uint32_t dd = gpsData.utc_date / 10000;
        uint32_t mm = (gpsData.utc_date / 100) % 100;
        uint32_t yy = gpsData.utc_date % 100;
        bleSendLine("gps_date=20%02lu-%02lu-%02lu",
                    (unsigned long)yy, (unsigned long)mm, (unsigned long)dd);
    } else {
        bleSendLine("gps_date=");
    }

    /* PPS */
    bleSendLine("pps=%d", ppsSynced ? 1 : 0);
    bleSendLine("pps_count=%lu", (unsigned long)ppsCount);
    bleSendLine("pps_ms=%lu", (unsigned long)(HAL_GetTick() - ppsTick));

    /* Temperature/humidity — not yet implemented */
    bleSendLine("temp=0");
    bleSendLine("hum=0");

    /* SD card */
    bleSendLine("sd=%d", sdMounted ? 1 : 0);
    if (sdMounted) {
        FATFS *fs;
        DWORD fre_clust;
        if (osMutexAcquire(fileMtxHandle, 200) == osOK) {
            if (f_getfree("", &fre_clust, &fs) == FR_OK) {
                DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
                DWORD fre_sect = fre_clust * fs->csize;
                bleSendLine("sd_tot=%lu", (unsigned long)(tot_sect / 2));
                bleSendLine("sd_free=%lu", (unsigned long)(fre_sect / 2));
            } else {
                bleSendLine("sd_tot=0");
                bleSendLine("sd_free=0");
            }
            osMutexRelease(fileMtxHandle);
        } else {
            bleSendLine("sd_tot=0");
            bleSendLine("sd_free=0");
        }
    } else {
        bleSendLine("sd_tot=0");
        bleSendLine("sd_free=0");
    }

    /* Recording */
    bleSendLine("rec=%d", isRecording ? 1 : 0);
    bleSendLine("rec_fmt=%s", recFormat == REC_FMT_WAV ? "WAV" : "FLAC");
    bleSendLine("rec_file=%s", recFilename);
    bleSendLine("rec_bytes=%lu", (unsigned long)totalDataBytes);
    bleSendLine("rec_dur=%lu",
                (unsigned long)(isRecording ? totalDataBytes / (SAMPLE_RATE * 2) : 0));
    bleSendLine("rec_ovf=0");

    /* Audio buffer stats — read and reset peak */
    int16_t peak = audioPeakLevel;
    audioPeakLevel = 0;
    bleSendLine("aud_peak=%d", (int)peak);
    bleSendLine("aud_buf=0");
    bleSendLine("aud_cap=%d", AUDIO_BUF_SIZE / 2);

    /* BLE module info */
    bleSendLine("ble_ready=%d", bleReady ? 1 : 0);
    bleSendLine("ble_name=%s", bleName);
    bleSendLine("ble_addr=%s", bleAddr);
    bleSendLine("ble_conn=%d", bleConnected ? 1 : 0);

    /* Survey-in status */
    {
        int32_t slat7 = (int32_t)(cfg.surveyLat * 10000000.0f);
        int32_t slon7 = (int32_t)(cfg.surveyLon * 10000000.0f);
        int32_t salt_mm = (int32_t)(cfg.surveyAlt * 1000.0f);
        bleSendLine("survey_lat=%ld", (long)slat7);
        bleSendLine("survey_lon=%ld", (long)slon7);
        bleSendLine("survey_alt=%ld", (long)salt_mm);
        bleSendLine("survey_count=%lu", (unsigned long)cfg.surveyCount);
        bleSendLine("survey_active=%d", surveyActive ? 1 : 0);
    }

    bleSendLine("$END");
}

static void bleHandleConfig(void)
{
    bleSendLine("$CONFIG");
    bleSendLine("id=%s", cfg.stationId);
    bleSendLine("gain=%d", cfg.gain);
    bleSendLine("hpf=%d", cfg.hpf);
    bleSendLine("rate=%lu", (unsigned long)SAMPLE_RATE);
    bleSendLine("fmt=%s", cfg.recFormat == REC_FMT_WAV ? "WAV" : "FLAC");
    bleSendLine("sunrise=%d", cfg.sunriseEnabled);
    bleSendLine("sunrise_before=%d", (int)cfg.sunriseBefore);
    bleSendLine("sunrise_after=%d", (int)cfg.sunriseAfter);
    bleSendLine("sunset=%d", cfg.sunsetEnabled);
    bleSendLine("sunset_before=%d", (int)cfg.sunsetBefore);
    bleSendLine("sunset_after=%d", (int)cfg.sunsetAfter);
    bleSendLine("nwin=%d", cfg.numWindows);
    if (cfg.numWindows > 0) {
        char winBuf[128];
        int pos = 0;
        for (int i = 0; i < cfg.numWindows * 2 && i < 16; i++) {
            if (i > 0) winBuf[pos++] = ',';
            pos += snprintf(winBuf + pos, sizeof(winBuf) - pos, "%04u", cfg.windows[i]);
        }
        bleSendLine("win=%s", winBuf);
    }
    bleSendLine("trig=%d", cfg.trigEnabled);
    bleSendLine("trig_db=%d", (int)cfg.trigDb);
    bleSendLine("trig_pre=%d", cfg.trigPre);
    bleSendLine("trig_post=%d", cfg.trigPost);
    bleSendLine("lowbat=%d", cfg.lowBatPct);
    bleSendLine("autostop=%d", cfg.autoStop);

    /* Survey-in config */
    {
        int32_t slat7 = (int32_t)(cfg.surveyLat * 10000000.0f);
        int32_t slon7 = (int32_t)(cfg.surveyLon * 10000000.0f);
        int32_t salt_mm = (int32_t)(cfg.surveyAlt * 1000.0f);
        bleSendLine("survey_lat=%ld", (long)slat7);
        bleSendLine("survey_lon=%ld", (long)slon7);
        bleSendLine("survey_alt=%ld", (long)salt_mm);
        bleSendLine("survey_count=%lu", (unsigned long)cfg.surveyCount);
    }

    bleSendLine("$END");
}

static void bleHandleSd(const char *arg)
{
    if (strcmp(arg, "EJECT") == 0) {
        if (isRecording) { bleSendLine("$ERR,BUSY"); return; }
        if (!sdMounted)  { bleSendLine("$ERR,ALREADY"); return; }
        extern char USERPath[];
        osMutexAcquire(fileMtxHandle, osWaitForever);
        f_mount(NULL, USERPath, 0);
        USER_disk_deinit();
        sdMounted = 0;
        osMutexRelease(fileMtxHandle);
        printf("BLE: SD ejected\r\n");
        bleSendLine("$OK");
    } else if (strcmp(arg, "MOUNT") == 0) {
        if (sdMounted) { bleSendLine("$ERR,ALREADY"); return; }
        extern FATFS USERFatFS;
        extern char USERPath[];
        osMutexAcquire(fileMtxHandle, osWaitForever);
        if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
            sdMounted = 1;
            osMutexRelease(fileMtxHandle);
            printf("BLE: SD mounted\r\n");
            bleSendLine("$OK");
        } else {
            osMutexRelease(fileMtxHandle);
            bleSendLine("$ERR,NOSD");
        }
    } else if (strcmp(arg, "FORMAT") == 0) {
        if (isRecording) { bleSendLine("$ERR,BUSY"); return; }
        osMutexAcquire(fileMtxHandle, osWaitForever);
        int ok = formatSD();
        osMutexRelease(fileMtxHandle);
        if (ok) {
            printf("BLE: SD formatted\r\n");
            bleSendLine("$OK");
        } else {
            bleSendLine("$ERR,NOSD");
        }
    } else {
        bleSendLine("$ERR,BADARG");
    }
}

static void bleHandleSet(const char *args)
{
    /* args points past "$SET," — e.g. "STATION,QT001" */
    char key[16], val[32];
    const char *comma = strchr(args, ',');
    if (!comma) { bleSendLine("$ERR,BADARG"); return; }

    int keyLen = (int)(comma - args);
    if (keyLen < 1 || keyLen > (int)sizeof(key) - 1) {
        bleSendLine("$ERR,BADARG");
        return;
    }
    memcpy(key, args, keyLen);
    key[keyLen] = '\0';
    strncpy(val, comma + 1, sizeof(val) - 1);
    val[sizeof(val) - 1] = '\0';

    int ok = 1;

    if (strcmp(key, "STATION") == 0) {
        if (strlen(val) > 15) { bleSendLine("$ERR,BADARG"); return; }
        strncpy(cfg.stationId, val, sizeof(cfg.stationId));
        strncpy(deviceStationId, cfg.stationId, sizeof(deviceStationId));
    } else if (strcmp(key, "GAIN") == 0) {
        int v = val[0] - '0';
        if (v < 0 || v > 4) { bleSendLine("$ERR,BADARG"); return; }
        cfg.gain = (uint8_t)v;
        /* Apply to ADF filter immediately via HAL (writes hardware register) */
        extern MDF_HandleTypeDef AdfHandle0;
        int32_t newGain = (int32_t)v * 6;
        HAL_MDF_SetGain(&AdfHandle0, newGain);
        printf("BLE: ADF gain set to %ld\r\n", (long)newGain);
    } else if (strcmp(key, "HPF") == 0) {
        int v = val[0] - '0';
        if (v < 0 || v > 2) { bleSendLine("$ERR,BADARG"); return; }
        cfg.hpf = (uint8_t)v;
    } else if (strcmp(key, "FORMAT") == 0) {
        if (isRecording) { bleSendLine("$ERR,BUSY"); return; }
        if (strcmp(val, "WAV") == 0) {
            cfg.recFormat = REC_FMT_WAV;
            recFormat = REC_FMT_WAV;
        } else if (strcmp(val, "FLAC") == 0) {
            cfg.recFormat = REC_FMT_FLAC;
            recFormat = REC_FMT_FLAC;
        } else {
            bleSendLine("$ERR,BADARG"); return;
        }
    } else if (strcmp(key, "SUNRISE") == 0) {
        /* Value format: enabled,before,after e.g. "1,30,60" */
        const char *c2 = strchr(val, ',');
        if (!c2) { bleSendLine("$ERR,BADARG"); return; }
        const char *c3 = strchr(c2 + 1, ',');
        if (!c3) { bleSendLine("$ERR,BADARG"); return; }
        cfg.sunriseEnabled = (uint8_t)(val[0] - '0');
        cfg.sunriseBefore = (uint16_t)atoi(c2 + 1);
        cfg.sunriseAfter = (uint16_t)atoi(c3 + 1);
    } else if (strcmp(key, "SUNSET") == 0) {
        /* Value format: enabled,before,after e.g. "1,30,30" */
        const char *c2 = strchr(val, ',');
        if (!c2) { bleSendLine("$ERR,BADARG"); return; }
        const char *c3 = strchr(c2 + 1, ',');
        if (!c3) { bleSendLine("$ERR,BADARG"); return; }
        cfg.sunsetEnabled = (uint8_t)(val[0] - '0');
        cfg.sunsetBefore = (uint16_t)atoi(c2 + 1);
        cfg.sunsetAfter = (uint16_t)atoi(c3 + 1);
    } else if (strcmp(key, "WINDOWS") == 0) {
        /* Value format: N,HHMM,HHMM,... e.g. "2,0600,0900,1700,2030" */
        int n = val[0] - '0';
        if (n < 0 || n > 8) { bleSendLine("$ERR,BADARG"); return; }
        cfg.numWindows = (uint8_t)n;
        const char *p = strchr(val, ',');
        for (int i = 0; i < n * 2 && p; i++) {
            p++; /* skip comma */
            cfg.windows[i] = (uint16_t)atoi(p);
            p = strchr(p, ',');
        }
    } else if (strcmp(key, "TRIG") == 0) {
        int v = val[0] - '0';
        if (v < 0 || v > 1) { bleSendLine("$ERR,BADARG"); return; }
        cfg.trigEnabled = (uint8_t)v;
    } else if (strcmp(key, "TRIGDB") == 0) {
        int v = atoi(val);
        if (v < -60 || v > 0) { bleSendLine("$ERR,BADARG"); return; }
        cfg.trigDb = (int8_t)v;
    } else if (strcmp(key, "TRIGPRE") == 0) {
        int v = atoi(val);
        if (v < 0 || v > 30) { bleSendLine("$ERR,BADARG"); return; }
        cfg.trigPre = (uint8_t)v;
    } else if (strcmp(key, "TRIGPOST") == 0) {
        int v = atoi(val);
        if (v < 0 || v > 60) { bleSendLine("$ERR,BADARG"); return; }
        cfg.trigPost = (uint8_t)v;
    } else if (strcmp(key, "LOWBAT") == 0) {
        int v = atoi(val);
        if (v < 0 || v > 100) { bleSendLine("$ERR,BADARG"); return; }
        cfg.lowBatPct = (uint8_t)v;
    } else if (strcmp(key, "AUTOSTOP") == 0) {
        int v = val[0] - '0';
        if (v < 0 || v > 1) { bleSendLine("$ERR,BADARG"); return; }
        cfg.autoStop = (uint8_t)v;
    } else if (strcmp(key, "SURVEY") == 0) {
        if (strcmp(val, "START") == 0) {
            if (gpsData.satellites < SURVEY_MIN_SATS || gpsData.fix < 1) {
                bleSendLine("$ERR,NOSATS");
                return;
            }
            /* Reset and start 5-minute survey */
            cfg.surveyLat = 0.0f;
            cfg.surveyLon = 0.0f;
            cfg.surveyAlt = 0.0f;
            cfg.surveyCount = 0;
            surveyActive = 1;
            surveyStartTick = HAL_GetTick();
            printf("Survey: Started (5 min)\r\n");
            bleSendLine("$OK");
        } else if (strcmp(val, "STOP") == 0) {
            if (surveyActive) {
                surveyActive = 0;
                configSave();
                printf("Survey: Stopped (%lu fixes)\r\n",
                       (unsigned long)cfg.surveyCount);
            }
            bleSendLine("$OK");
        } else if (strcmp(val, "CLEAR") == 0) {
            surveyActive = 0;
            cfg.surveyLat = 0.0f;
            cfg.surveyLon = 0.0f;
            cfg.surveyAlt = 0.0f;
            cfg.surveyCount = 0;
            configSave();
            printf("Survey: Cleared\r\n");
            bleSendLine("$OK");
        } else {
            bleSendLine("$ERR,BADARG");
        }
        return; /* no additional flash save needed */
    } else if (strcmp(key, "STREAM") == 0) {
        /* Runtime-only: set status push interval in ms (0=off, 100-30000) */
        int v = atoi(val);
        if (v != 0 && (v < 100 || v > 30000)) { bleSendLine("$ERR,BADARG"); return; }
        streamInterval = (uint32_t)v;
        lastStreamTick = HAL_GetTick();
        printf("BLE: Stream interval = %d ms\r\n", v);
        bleSendLine("$OK");
        return; /* no flash save needed */
    } else {
        bleSendLine("$ERR,BADARG");
        return;
    }

    if (ok) {
        if (configSave()) {
            printf("BLE: SET %s=%s OK\r\n", key, val);
            bleSendLine("$OK");
        } else {
            bleSendLine("$ERR,FLASH");
        }
    }
}

/* ========================= OTA Firmware Update ========================= */

static int hexDecode(const char *hex, int hexLen, uint8_t *out, int outSize)
{
    if (hexLen & 1) return -1;
    int nBytes = hexLen / 2;
    if (nBytes > outSize) return -1;
    for (int i = 0; i < nBytes; i++) {
        uint8_t hi, lo;
        char c = hex[i * 2];
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else return -1;
        c = hex[i * 2 + 1];
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else return -1;
        out[i] = (hi << 4) | lo;
    }
    return nBytes;
}

static int otaErasePage(uint32_t pageNum)
{
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_2;
    erase.Page = pageNum;
    erase.NbPages = 1;
    uint32_t pageError = 0;

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef rc = HAL_FLASHEx_Erase(&erase, &pageError);
    HAL_FLASH_Lock();
    return (rc == HAL_OK) ? 0 : -1;
}

static int otaProgramPage(uint32_t pageNum, const uint8_t *data, uint32_t len)
{
    uint32_t addr = OTA_BANK2_BASE + pageNum * OTA_PAGE_SIZE;

    /* Pad to quadword boundary (16 bytes) */
    uint32_t padLen = (len + 15) & ~15u;
    uint8_t pad[16];

    HAL_FLASH_Unlock();
    for (uint32_t off = 0; off < padLen; off += 16) {
        const uint8_t *src;
        if (off + 16 <= len) {
            src = data + off;
        } else {
            uint32_t valid = len - off;
            memcpy(pad, data + off, valid);
            memset(pad + valid, 0xFF, 16 - valid);
            src = pad;
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr + off,
                              (uint32_t)src) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();

    /* Readback verify */
    if (memcmp((const void *)addr, data, len) != 0)
        return -1;

    return 0;
}

static int otaWritePage(void)
{
    uint32_t pageNum = ota.pagesWritten;
    if (pageNum >= OTA_CONFIG_PAGE) return -1;

    if (otaErasePage(pageNum) != 0) return -1;
    if (otaProgramPage(pageNum, ota.pageBuf, ota.pageBufPos) != 0) return -1;

    ota.pagesWritten++;
    ota.pageBufPos = 0;
    return 0;
}

static int otaCopyConfig(void)
{
    /* Read config from inactive bank (0x081FE000) */
    const device_config_t *src = (const device_config_t *)CONFIG_FLASH_ADDR;
    device_config_t tmp;
    memcpy(&tmp, src, sizeof(tmp));

    /* Validate — if corrupt, use current RAM config */
    if (tmp.magic != CONFIG_MAGIC ||
        tmp.crc32 != crc32_compute((const uint8_t *)&tmp, sizeof(tmp) - 4)) {
        memcpy(&tmp, &cfg, sizeof(tmp));
        tmp.crc32 = configComputeCrc(&tmp);
    }

    /* Erase active bank page 127 */
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = OTA_CONFIG_PAGE;
    erase.NbPages = 1;
    uint32_t pageError = 0;

    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK) {
        HAL_FLASH_Lock();
        return -1;
    }

    /* Program config to 0x080FE000 (8 quadwords = 128 bytes) */
    const uint8_t *data = (const uint8_t *)&tmp;
    for (int i = 0; i < 8; i++) {
        uint32_t addr = OTA_CONFIG_MIRROR + (uint32_t)(i * 16);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr,
                              (uint32_t)(data + i * 16)) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();

    /* Verify */
    if (memcmp((const void *)OTA_CONFIG_MIRROR, &tmp, sizeof(tmp)) != 0)
        return -1;

    return 0;
}

static void otaSwapBank(void)
{
    /* Read current option bytes */
    FLASH_OBProgramInitTypeDef ob = {0};
    HAL_FLASHEx_OBGetConfig(&ob);

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    /* Toggle SWAP_BANK */
    FLASH_OBProgramInitTypeDef obNew = {0};
    obNew.OptionType = OPTIONBYTE_USER;
    obNew.USERType = OB_USER_SWAP_BANK;
    obNew.USERConfig = (ob.USERConfig & OB_SWAP_BANK_ENABLE)
                        ? OB_SWAP_BANK_DISABLE : OB_SWAP_BANK_ENABLE;

    HAL_FLASHEx_OBProgram(&obNew);
    HAL_FLASH_OB_Launch();  /* triggers system reset — does not return */
}

static void otaBegin(const char *args)
{
    if (isRecording) { bleSendLine("$ERR,RECORDING"); return; }
    if (ota.state != OTA_IDLE) { bleSendLine("$ERR,ALREADY"); return; }

    /* Parse size */
    uint32_t size = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9') { size = size * 10 + (*p++ - '0'); }
    if (*p != ',') { bleSendLine("$ERR,BADARG"); return; }
    p++;

    /* Parse CRC-32 hex (8 hex digits) */
    uint32_t crc = 0;
    for (int i = 0; i < 8 && *p; i++, p++) {
        uint8_t nib;
        if (*p >= '0' && *p <= '9') nib = *p - '0';
        else if (*p >= 'A' && *p <= 'F') nib = *p - 'A' + 10;
        else if (*p >= 'a' && *p <= 'f') nib = *p - 'a' + 10;
        else { bleSendLine("$ERR,BADARG"); return; }
        crc = (crc << 4) | nib;
    }

    /* Validate: must fit in 127 pages */
    if (size == 0 || size > (uint32_t)OTA_CONFIG_PAGE * OTA_PAGE_SIZE) {
        bleSendLine("$ERR,TOOBIG");
        return;
    }

    /* Init OTA context */
    memset(&ota, 0, sizeof(ota));
    ota.state = OTA_RECEIVING;
    ota.imageSize = size;
    ota.imageCrc = crc;
    ota.totalPages = (size + OTA_PAGE_SIZE - 1) / OTA_PAGE_SIZE;
    ota.lastActivityTick = HAL_GetTick();

    printf("OTA: BEGIN size=%lu crc=%08lx pages=%lu\r\n",
           (unsigned long)size, (unsigned long)crc, (unsigned long)ota.totalPages);
    bleSendLine("$OTA,READY,%lu", (unsigned long)ota.totalPages);
}

static void otaData(const char *hexStr)
{
    if (ota.state != OTA_RECEIVING) { bleSendLine("$ERR,BADSTATE"); return; }

    ota.lastActivityTick = HAL_GetTick();

    int hexLen = strlen(hexStr);
    uint8_t tmp[48];  /* max 96 hex chars = 48 bytes */
    int nBytes = hexDecode(hexStr, hexLen, tmp, sizeof(tmp));
    if (nBytes <= 0) { bleSendLine("$ERR,BADHEX"); return; }

    if (ota.bytesReceived + (uint32_t)nBytes > ota.imageSize) {
        bleSendLine("$ERR,OVERFLOW");
        return;
    }

    int pos = 0;
    while (pos < nBytes) {
        int space = OTA_PAGE_SIZE - ota.pageBufPos;
        int chunk = (nBytes - pos < space) ? (nBytes - pos) : space;
        memcpy(ota.pageBuf + ota.pageBufPos, tmp + pos, chunk);
        ota.pageBufPos += chunk;
        ota.bytesReceived += chunk;
        pos += chunk;

        /* Page buffer full — write to flash */
        if (ota.pageBufPos == OTA_PAGE_SIZE) {
            if (otaWritePage() != 0) {
                bleSendLine("$ERR,FLASH");
                ota.state = OTA_IDLE;
                return;
            }
            bleSendLine("$OTA,P,%lu", (unsigned long)ota.pagesWritten);
        }
    }
}

static void otaEnd(void)
{
    if (ota.state != OTA_RECEIVING) { bleSendLine("$ERR,BADSTATE"); return; }

    /* Write final partial page if any data remains */
    if (ota.pageBufPos > 0) {
        if (otaWritePage() != 0) {
            bleSendLine("$ERR,FLASH");
            ota.state = OTA_IDLE;
            return;
        }
    }

    /* Verify received size */
    if (ota.bytesReceived != ota.imageSize) {
        bleSendLine("$ERR,SIZE");
        ota.state = OTA_IDLE;
        return;
    }

    /* CRC-32 verify entire image from flash readback */
    uint32_t crc = crc32_compute((const uint8_t *)OTA_BANK2_BASE, ota.imageSize);
    if (crc != ota.imageCrc) {
        printf("OTA: CRC mismatch: expected %08lx got %08lx\r\n",
               (unsigned long)ota.imageCrc, (unsigned long)crc);
        bleSendLine("$ERR,CRC");
        ota.state = OTA_IDLE;
        return;
    }

    printf("OTA: Verified %lu bytes, CRC OK\r\n", (unsigned long)ota.imageSize);
    ota.state = OTA_COMPLETE;
    bleSendLine("$OTA,VERIFIED");
}

static void otaCommit(void)
{
    if (ota.state != OTA_COMPLETE) { bleSendLine("$ERR,BADSTATE"); return; }

    printf("OTA: Copying config...\r\n");
    if (otaCopyConfig() != 0) {
        bleSendLine("$ERR,CONFIG");
        return;
    }

    printf("OTA: Swapping banks...\r\n");
    bleSendLine("$OTA,SWAPPING");
    osDelay(50);  /* let BLE transmit response */
    otaSwapBank();  /* does not return */
}

static void otaAbort(void)
{
    ota.state = OTA_IDLE;
    printf("OTA: Aborted\r\n");
    bleSendLine("$OK");
}

static void otaRollback(void)
{
    if (ota.state != OTA_IDLE) { bleSendLine("$ERR,BADSTATE"); return; }

    printf("OTA: Rolling back — copying config...\r\n");
    if (otaCopyConfig() != 0) {
        bleSendLine("$ERR,CONFIG");
        return;
    }

    printf("OTA: Swapping banks...\r\n");
    bleSendLine("$OTA,SWAPPING");
    osDelay(50);
    otaSwapBank();  /* does not return */
}

static void otaStatus(void)
{
    bleSendLine("$OTA,STATUS,%d,%lu,%lu,%lu",
                ota.state,
                (unsigned long)ota.bytesReceived,
                (unsigned long)ota.pagesWritten,
                (unsigned long)ota.totalPages);
}

static void bleHandleOta(const char *args)
{
    if (strncmp(args, "BEGIN,", 6) == 0) {
        otaBegin(args + 6);
    } else if (strncmp(args, "D,", 2) == 0) {
        otaData(args + 2);
    } else if (strcmp(args, "END") == 0) {
        otaEnd();
    } else if (strcmp(args, "COMMIT") == 0) {
        otaCommit();
    } else if (strcmp(args, "ABORT") == 0) {
        otaAbort();
    } else if (strcmp(args, "ROLLBACK") == 0) {
        otaRollback();
    } else if (strcmp(args, "STATUS") == 0) {
        otaStatus();
    } else {
        bleSendLine("$ERR,BADCMD");
    }
}

static void bleHandleCommand(const char *cmd)
{
    printf("BLE CMD: %s\r\n", cmd);

    /* OTA commands always routed regardless of state */
    if (strncmp(cmd, "$OTA,", 5) == 0) {
        bleHandleOta(cmd + 5);
        return;
    }

    /* Block non-OTA commands during OTA */
    if (ota.state != OTA_IDLE) {
        bleSendLine("$ERR,OTA_BUSY");
        return;
    }

    if (strcmp(cmd, "$PING") == 0) {
        bleSendLine("$PONG");
    } else if (strcmp(cmd, "$MENU") == 0) {
        bleSendLine("$MENU");
        bleSendLine("$PING              - connection test");
        bleSendLine("$VER               - firmware version");
        bleSendLine("$STATUS            - full device status");
        bleSendLine("$CONFIG            - current configuration");
        bleSendLine("$REC,START         - start recording");
        bleSendLine("$REC,STOP          - stop recording");
        bleSendLine("$REC,TOGGLE        - toggle recording");
        bleSendLine("$SD,MOUNT          - mount SD card");
        bleSendLine("$SD,EJECT          - eject SD card");
        bleSendLine("$SD,FORMAT         - format SD card");
        bleSendLine("$SET,STATION,<id>  - set station ID");
        bleSendLine("$SET,GAIN,<0-4>    - set mic gain");
        bleSendLine("$SET,HPF,<0-2>     - set high-pass filter");
        bleSendLine("$SET,FORMAT,<WAV|FLAC>");
        bleSendLine("$LOGS              - toggle log forwarding");
        bleSendLine("$SET,STREAM,<ms>   - status push (0=off)");
        bleSendLine("$SET,SURVEY,START  - begin 5-min survey");
        bleSendLine("$SET,SURVEY,STOP   - stop survey early");
        bleSendLine("$SET,SURVEY,CLEAR  - reset survey data");
        bleSendLine("$SET,SUNRISE,<e>,<before>,<after>");
        bleSendLine("$SET,SUNSET,<e>,<before>,<after>");
        bleSendLine("$SET,WINDOWS,<n>,<HHMM>,<HHMM>,...");
        bleSendLine("$SET,TRIG,<0|1>    - amplitude trigger");
        bleSendLine("$SET,TRIGDB,<-60..0>");
        bleSendLine("$SET,TRIGPRE,<0-30>");
        bleSendLine("$SET,TRIGPOST,<0-60>");
        bleSendLine("$SET,LOWBAT,<0-100>");
        bleSendLine("$SET,AUTOSTOP,<0|1>");
        bleSendLine("$OTA,BEGIN,<size>,<crc32hex>");
        bleSendLine("$OTA,D,<hexdata>   - OTA data chunk");
        bleSendLine("$OTA,END           - finalize OTA");
        bleSendLine("$OTA,COMMIT        - swap banks + reboot");
        bleSendLine("$OTA,ABORT         - cancel OTA");
        bleSendLine("$OTA,ROLLBACK      - revert to prev FW");
        bleSendLine("$OTA,STATUS        - OTA progress");
        bleSendLine("$END");
    } else if (strcmp(cmd, "$VER") == 0) {
        bleSendLine("$VER," FW_VERSION);
    } else if (strcmp(cmd, "$LOGS") == 0) {
        bleLogEnabled = !bleLogEnabled;
        if (bleLogEnabled) {
            bleLogHead = 0;
            bleLogTail = 0;
        }
        printf("BLE: Log forwarding = %s\r\n", bleLogEnabled ? "ON" : "OFF");
        bleSendLine("$OK,%s", bleLogEnabled ? "ON" : "OFF");
    } else if (strcmp(cmd, "$STATUS") == 0) {
        bleHandleStatus();
    } else if (strcmp(cmd, "$CONFIG") == 0) {
        bleHandleConfig();
    } else if (strcmp(cmd, "$REC,START") == 0) {
        if (isRecording) { bleSendLine("$ERR,ALREADY"); return; }
        if (!sdMounted)  { bleSendLine("$ERR,NOSD"); return; }
        uint8_t c = CMD_START_REC;
        osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
        osDelay(100);
        bleSendLine("$OK");
    } else if (strcmp(cmd, "$REC,STOP") == 0) {
        if (!isRecording) { bleSendLine("$ERR,ALREADY"); return; }
        uint8_t c = CMD_STOP_REC;
        osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
        osDelay(100);
        bleSendLine("$OK");
    } else if (strcmp(cmd, "$REC,TOGGLE") == 0) {
        if (!isRecording && !sdMounted) { bleSendLine("$ERR,NOSD"); return; }
        uint8_t c = isRecording ? CMD_STOP_REC : CMD_START_REC;
        osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
        osDelay(100);
        bleSendLine("$OK");
    } else if (strncmp(cmd, "$SD,", 4) == 0) {
        bleHandleSd(cmd + 4);
    } else if (strncmp(cmd, "$SET,", 5) == 0) {
        bleHandleSet(cmd + 5);
    } else {
        bleSendLine("$ERR,BADCMD");
    }
}

/* USER CODE END Application */

