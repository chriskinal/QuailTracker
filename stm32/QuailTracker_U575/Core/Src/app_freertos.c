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
    uint32_t utc_time;   /* HHMMSS as integer */
    uint32_t utc_date;   /* DDMMYY as integer */
    uint8_t  valid;      /* RMC status: 1=A, 0=V */
} gps_data_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

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

/* Functions from main.c */
extern int getChar(void);
extern int getCharGps(void);
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

/* GPS state */
static gps_data_t gpsData;
static volatile uint8_t gpsRawOutput;
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
static void printGpsStatus(void);
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

    /* Process audio data if recording */
    if (isRecording) {
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
        for (int i = 0; i < AUDIO_BUF_SIZE / 2; i++) {
          pcmBuffer[i] = (int16_t)(src[i] >> 16);
        }

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
  printMenu();

  for (;;)
  {
    int c = getChar();
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
            confirm = getChar();
            if (confirm == '\r' || confirm == '\n') confirm = -1;
            if (confirm < 0) osDelay(10);
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

      case '\r':
      case '\n':
        break;
      }
    }

    osDelay(10); /* yield CPU, ~100Hz poll rate */
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
        ppsSynced = 1;
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
    printf("Raw output: %s\r\n", gpsRawOutput ? "ON" : "OFF");
    printf("==================\r\n");
}

static void StartGpsTask(void *argument)
{
    char buf[128];
    int pos = 0;

    printf("GPS: Listening on LPUART1 (9600 baud)\r\n");

    for (;;) {
        int c = getCharGps();
        if (c >= 0) {
            if (c == '\n') {
                if (pos > 0 && buf[pos - 1] == '\r') pos--;
                buf[pos] = '\0';
                if (pos > 0) nmea_process_line(buf);
                pos = 0;
            } else if (pos < (int)sizeof(buf) - 1) {
                buf[pos++] = (char)c;
            }
        } else {
            osDelay(1);
        }
    }
}

/* USER CODE END Application */

