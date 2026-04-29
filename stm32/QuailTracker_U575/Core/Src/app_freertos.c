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
#include <math.h>
#include "fatfs.h"
#include "user_diskio.h"
#include "flac_encoder.h"
#include "mel_spectrogram.h"
#include "tflite_inference.h"
#include "SEGGER_RTT.h"
#include "device_state.h"
#include "spi_bridge.h"
#include "solar.h"
#include "schedule.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* ---- Flash config/health constants (types now in device_state.h) ---- */
#define CONFIG_MAGIC      0x51544346   /* "QTCF" */
#define CONFIG_VERSION    9
#define CONFIG_FLASH_ADDR 0x080FE000   /* Last page of Bank 1 on 2MB (Nucleo ZI), Bank 2 page 63 on 1MB (VGT6) */

#define HEALTH_MAGIC       0x51544853   /* "QTHS" */
#define HEALTH_VERSION     1
#define HEALTH_FLASH_ADDR  0x080FC000   /* One page before config page */

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
#define OTA_PAGE_SIZE     8192
#define OTA_BANK2_BASE    0x08080000   /* Bank 2 start for 1MB flash */
#define OTA_CONFIG_PAGE   63           /* Last page of Bank 2 (1MB: 64 pages/bank) */
#define OTA_CONFIG_MIRROR 0x0807E000   /* Last page of Bank 1 (config backup during OTA) */
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

extern int32_t pcmBuffer[];
extern int32_t pcmBufferR[];

/* PCM ring buffer — defined in main.c, written by DMA ISR callbacks */
#define PCM_RING_SIZE  16384
#define PCM_RING_MASK  (PCM_RING_SIZE - 1)
extern int32_t pcmRing[];
extern volatile uint32_t ringHead;
extern uint32_t ringTail;
extern int32_t pcmRingR[];
extern volatile uint32_t ringHeadR;
extern uint32_t ringTailR;
extern volatile uint8_t audioChannelRight;

extern FIL wavFile;
#define REC_FMT_FLAC 0
#define REC_FMT_WAV  1
extern flac_enc_t flacEncoder;
extern char deviceStationId[16];

/* ---- Consolidated device state (single instance) ---- */
device_state_t dev;

/* Snapshot: copy dev under critical section for consistent encoding */
void device_state_snapshot(device_state_t *out)
{
    taskENTER_CRITICAL();
    memcpy(out, &dev, sizeof(dev));
    taskEXIT_CRITICAL();
}

/* ---- Aliases: old variable names → dev struct fields ---- */
#define isRecording         dev.rec.active
#define sdMounted           dev.rec.sdMounted
#define audioStarted        dev.rec.audioStarted
#define totalDataBytes      dev.rec.dataBytes
#define ringOverruns        dev.rec.overruns
#define recStartTick        dev.rec.startTick
#define fileCounter         dev.rec.fileCounter
#define recFilename         dev.rec.filename
#define gpsData             dev.gps.fix
#define ppsSynced           dev.gps.ppsSynced
#define ppsCount            dev.gps.ppsCount
#define ppsTick             dev.gps.ppsTick
#define ppsUtcTime          dev.gps.ppsUtcTime
#define ppsUtcDate          dev.gps.ppsUtcDate
#define ppsLatitude         dev.gps.ppsLatitude
#define ppsLongitude        dev.gps.ppsLongitude
#define ppsAltitude         dev.gps.ppsAltitude
#define surveyActive        dev.gps.surveyActive
#define surveyStartTick     dev.gps.surveyStartTick
#define batteryMv           dev.env.batteryMv
#define sht30TempC100       dev.env.tempC100
#define sht30HumRH100       dev.env.humRH100
#define audioPeakLevel      dev.audio.peakLevel
#define actRatio            dev.audio.actRatio
#define limiterClipCount    dev.audio.clipCount
#define modelLoaded         dev.det.modelLoaded
#define modelBufSize        dev.det.modelBufSize
#define modelNumLabels      dev.det.modelNumLabels
#define detWindowsProcessed dev.det.windowsProcessed
#define detHits             dev.det.hits
#define detLastSpecies      dev.det.lastSpecies
#define detLastConf         dev.det.lastConf
#define detLastTime         dev.det.lastTime

/* Peak limiter: hard-clip samples above this threshold.
 * Prevents ADF Sinc4 saturation artifacts on loud close-range calls.
 * Set to ~85% of int16 max to leave headroom while preserving dynamics. */
#define PEAK_LIMITER_THRESHOLD  7168000

static uint32_t lastSht30Tick = 0;
#define SHT30_INTERVAL_MS 5000

/* Functions from main.c */
extern int getChar(uint32_t timeoutMs);
extern int getCharGps(uint32_t timeoutMs);
extern void WAV_WriteHeader(FIL *fp, uint32_t sampleRate, uint32_t dataSize);
extern void printMenu(void);
extern void printStatus(void);
extern void startRecording(void);
extern void stopRecording(void);
extern int formatSD(void);

/* Battery/SHT30 functions from main.c */
extern uint32_t battReadMv(void);
extern void sht30Read(void);

#define SURVEY_DURATION_MS  300000      /* 5 minutes */
#define SURVEY_MIN_SATS     4           /* minimum satellites for valid fix */

static volatile uint8_t gpsRawOutput;
static uint8_t gpsPowered = 1;

/* Flash-persisted device config (loaded at bridge task start) */
device_config_t cfg __attribute__((aligned(16)));

/* Flash-persisted health statistics */
health_stats_t health __attribute__((aligned(16)));
static uint32_t lastHealthSaveTick = 0;
#define HEALTH_SAVE_INTERVAL_MS 300000  /* 5 minutes */
static uint8_t prevGpsValid = 0;  /* for GPS fix loss detection */

/* SPI2 bridge to ESP32-C3 */
extern SPI_HandleTypeDef hspi2;
#define SPI2_CS_PORT  GPIOB
#define SPI2_CS_PIN   GPIO_PIN_12
#define SPI2_BUF_SIZE 1024
#define SPI2_POLL_MS       250  /* SPI command poll + status push interval */
#define SPI2_POLL_STREAM_MS 20  /* faster poll when audio streaming (must align with osDelay) */
static uint32_t lastSpiPollTick = 0;

/* ---- Live audio streaming state ---- */
static volatile uint8_t streamActive = 0;
static volatile uint8_t streamChannel = 0;  /* 0=L, 1=R */
static uint32_t streamTailL = 0;   /* independent ring tail for left channel */
static uint32_t streamTailR = 0;   /* independent ring tail for right channel */
static uint32_t streamLastSpiTick = 0;  /* auto-stop timeout */
#define STREAM_TIMEOUT_MS 1000

/* ---- Power management ---- */
#define GPS_DUTY_CYCLE_DEFAULT_SEC  300  /* GPS wake every 5 min during recording */
#define GPS_FIX_TIMEOUT_MS         15000 /* max wait for GPS fix during duty cycle */
#define SCHEDULE_CHECK_INTERVAL_MS 1000  /* how often to evaluate schedule */
static uint32_t lastScheduleCheckTick = 0;
static uint32_t lastGpsDutyTick = 0;
static uint8_t  gpsDutyActive = 0;   /* 1 = GPS is on for duty cycle fix */

/* Forward declarations for power management */
static void powerEnterNonRecord(void);
static void powerEnterRecord(void);
static void powerScheduleCheck(void);
static void gpsDutyCycle(void);

/* Forward declarations for health functions */
static void healthLoad(void);
int healthSave(void);
void healthReset(void);
void healthUpdateEnvironment(uint32_t battMv, int32_t tempC100);
void healthUpdateRecStart(const char *filename);
void healthUpdateRecStop(uint32_t bytes, uint32_t durationSecs);

/* ---- BPF filter state (reset on recording start) ---- */
static int32_t  hpfPrevIn  = 0;
static int32_t  hpfPrevOut = 0;
static uint32_t hpfAlpha   = 0;   /* computed from bpfLow, Q16 */
static int32_t  lpfPrevOut = 0;
static uint32_t lpfAlpha   = 0;   /* computed from bpfHigh, Q16 */
/* Right channel filter state (independent, same coefficients) */
static int32_t  hpfPrevInR  = 0;
static int32_t  hpfPrevOutR = 0;
static int32_t  lpfPrevOutR = 0;

/* Compute Q16 HPF alpha from cutoff frequency: alpha = e^(-2*pi*fc/fs) * 65536 */
static uint32_t computeHpfAlpha(uint16_t fc) {
    if (fc == 0) return 0;
    return (uint32_t)(expf(-6.2831853f * (float)fc / (float)SAMPLE_RATE) * 65536.0f);
}

/* Compute Q16 LPF alpha from cutoff frequency: alpha = (1 - e^(-2*pi*fc/fs)) * 65536 */
static uint32_t computeLpfAlpha(uint16_t fc) {
    if (fc == 0) return 0;
    return (uint32_t)((1.0f - expf(-6.2831853f * (float)fc / (float)SAMPLE_RATE)) * 65536.0f);
}

/* ---- Activity filter state ---- */
static int32_t  actMean  = 0;             /* running mean(|sample|), Q0 */
static int32_t  actMad   = 0;             /* running MAD, Q0 */
static uint32_t actAbove = 0;             /* above-threshold count in current window */
static uint32_t actTotal = 0;             /* total samples in current window */
static uint8_t  actSquelch   = 0;         /* 1 = current window is uninteresting */
static uint8_t  actHolddown  = 0;         /* gate mode holdoff countdown (seconds) */
static uint8_t  actGateOpen  = 1;         /* gate mode: 1 = recording allowed */

/* OTA firmware update context (~8.2KB BSS) */
static ota_ctx_t ota;

/* ---- Inference engine state ---- */
#define MISSION_RECORD 0
#define MISSION_DETECT 1
#define MISSION_BOTH   2

/* Inference buffers from main.c */
extern uint8_t modelBuf[56 * 1024];
extern uint8_t tensorArena[112 * 1024];
extern volatile uint64_t absSampleCount;

/* Inference task handle + notification */
osThreadId_t inferenceTaskHandle;

/* Decimation + mel accumulation: 2 audio blocks of 128 → 256 at 24kHz (one mel hop) */
static int16_t melAccumBuf[256];  /* MEL_HOP samples */
static int melAccumIdx = 0;

/* Model metadata (loaded from model_config.json) */
static char modelLabels[TFLITE_MAX_CLASSES][32];
static float modelMelMin = -80.0f;
static float modelMelMax = 0.0f;


/* Detection CSV filename for today */
static char detCsvFilename[56] = "";
static uint32_t detCsvDate = 0;  /* YYYYMMDD */
/* USER CODE END Variables */
/* Definitions for audioTask */
osThreadId_t audioTaskHandle;
const osThreadAttr_t audioTask_attributes = {
  .name = "audioTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 2048 * 4
};
/* Definitions for cliTask */
osThreadId_t cliTaskHandle;
const osThreadAttr_t cliTask_attributes = {
  .name = "cliTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 1024 * 4
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
static void StartBridgeTask(void *argument);
static void StartInferenceTask(void *argument);
static void printGpsStatus(void);
static void gpsSetPower(uint8_t on);
static void gpsReset(void);

/* Flash config */
static void configSetDefaults(device_config_t *c);
static uint32_t configComputeCrc(const device_config_t *c);
static void configLoad(void);
int configSave(void);

/* Survey-in */
static void surveyAccumulate(float lat, float lon, float alt);

/* Diagnostic log */
void diagLog(const char *event);

/* OTA */
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
  /* Override CubeMX binary semaphore (max=1) with counting semaphore (max=16).
   * A binary semaphore silently drops DMA callbacks when FLAC encode + SD write
   * takes longer than 10.67ms (one DMA half-period), causing audio glitches.
   * Counting semaphore ensures no callbacks are lost. */
  osSemaphoreDelete(audioDmaSemHandle);
  audioDmaSemHandle = osSemaphoreNew(16, 0, &audioDmaSem_attributes);
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
      .stack_size = 1024 * 4
    };
    osThreadNew(StartGpsTask, NULL, &gpsTask_attributes);
  }
  {
    const osThreadAttr_t bridgeTask_attributes = {
      .name = "bridgeTask",
      .priority = (osPriority_t) osPriorityNormal,
      .stack_size = 1536 * 4
    };
    osThreadNew(StartBridgeTask, NULL, &bridgeTask_attributes);
  }
  {
    const osThreadAttr_t inferenceTask_attributes = {
      .name = "inferTask",
      .priority = (osPriority_t) osPriorityBelowNormal,
      .stack_size = 2048 * 4
    };
    inferenceTaskHandle = osThreadNew(StartInferenceTask, NULL, &inferenceTask_attributes);
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
    /* Block until ISR has pushed data into ring buffer */
    osSemaphoreAcquire(audioDmaSemHandle, osWaitForever);

    /* Check for start/stop commands from CLI task */
    uint8_t cmd;
    while (osMessageQueueGet(audioCmdQueueHandle, &cmd, NULL, 0) == osOK) {
      if (cmd == CMD_START_REC) {
        /* Compute BPF filter coefficients and reset state */
        hpfAlpha = computeHpfAlpha(cfg.bpfLow);
        lpfAlpha = computeLpfAlpha(cfg.bpfHigh);
        hpfPrevIn = 0; hpfPrevOut = 0; lpfPrevOut = 0;
        hpfPrevInR = 0; hpfPrevOutR = 0; lpfPrevOutR = 0;
        /* Reset activity filter state */
        actMean = 0; actMad = 0;
        actAbove = 0; actTotal = 0;
        actRatio = 0; actSquelch = 0;
        actHolddown = 0; actGateOpen = 1;
        melAccumIdx = 0;
        mel_reset();
        startRecording();
      }
      else if (cmd == CMD_STOP_REC) stopRecording();
    }

    /* Drain ring buffer into encoder/file writer.
     * The ISR copies DMA data into the ring immediately on each half-complete,
     * so no data is lost even if this task is blocked on f_sync for 100ms+. */
    if (isRecording) {
      /* Wait for both L and R ring buffers to have data */
      while ((ringHead - ringTail) >= (AUDIO_BUF_SIZE / 2) &&
             (ringHeadR - ringTailR) >= (AUDIO_BUF_SIZE / 2)) {
        const int blockLen = AUDIO_BUF_SIZE / 2;

        /* Step 1: Copy from both ring buffers */
        uint32_t tL = ringTail;
        uint32_t tR = ringTailR;
        for (int i = 0; i < blockLen; i++) {
          pcmBuffer[i]  = pcmRing[tL & PCM_RING_MASK];
          pcmBufferR[i] = pcmRingR[tR & PCM_RING_MASK];
          tL++;
          tR++;
        }
        ringTail  = tL;
        ringTailR = tR;

        /* Step 2a: Apply HPF if configured (bpfLow) */
        if (hpfAlpha > 0) {
          for (int i = 0; i < blockLen; i++) {
            int32_t in = pcmBuffer[i];
            int32_t out = (int32_t)(((int64_t)hpfAlpha * (hpfPrevOut + in - hpfPrevIn)) >> 16);
            hpfPrevIn = in;
            hpfPrevOut = out;
            pcmBuffer[i] = out;
          }
        }

        /* Step 2b: Apply LPF if configured (bpfHigh) */
        if (lpfAlpha > 0) {
          for (int i = 0; i < blockLen; i++) {
            int32_t in = pcmBuffer[i];
            lpfPrevOut = (int32_t)(((int64_t)lpfAlpha * in + (int64_t)(65536 - lpfAlpha) * lpfPrevOut) >> 16);
            pcmBuffer[i] = lpfPrevOut;
          }
        }

        /* Step 2c: Apply same HPF to right channel */
        if (hpfAlpha > 0) {
          for (int i = 0; i < blockLen; i++) {
            int32_t in = pcmBufferR[i];
            int32_t out = (int32_t)(((int64_t)hpfAlpha * (hpfPrevOutR + in - hpfPrevInR)) >> 16);
            hpfPrevInR = in;
            hpfPrevOutR = out;
            pcmBufferR[i] = out;
          }
        }

        /* Step 2d: Apply same LPF to right channel */
        if (lpfAlpha > 0) {
          for (int i = 0; i < blockLen; i++) {
            int32_t in = pcmBufferR[i];
            lpfPrevOutR = (int32_t)(((int64_t)lpfAlpha * in + (int64_t)(65536 - lpfAlpha) * lpfPrevOutR) >> 16);
            pcmBufferR[i] = lpfPrevOutR;
          }
        }

        /* Step 3: Peak limiter + peak level tracking (both channels) */
        int32_t blockPeak = 0;
        for (int i = 0; i < blockLen; i++) {
          int32_t s = pcmBuffer[i];
          if (s > PEAK_LIMITER_THRESHOLD) { s = PEAK_LIMITER_THRESHOLD; limiterClipCount++; }
          else if (s < -PEAK_LIMITER_THRESHOLD) { s = -PEAK_LIMITER_THRESHOLD; limiterClipCount++; }
          pcmBuffer[i] = s;

          int32_t sR = pcmBufferR[i];
          if (sR > PEAK_LIMITER_THRESHOLD) { sR = PEAK_LIMITER_THRESHOLD; }
          else if (sR < -PEAK_LIMITER_THRESHOLD) { sR = -PEAK_LIMITER_THRESHOLD; }
          pcmBufferR[i] = sR;

          int32_t a = s < 0 ? -s : s;
          if (a > blockPeak) blockPeak = a;
        }
        if (blockPeak > audioPeakLevel) audioPeakLevel = blockPeak;

        /* Step 4: Activity filter analysis */
        if (cfg.activityMode > 0) {
          int64_t blockAbsSum = 0;
          int64_t blockDevSum = 0;
          uint32_t blockAbove = 0;
          int32_t threshold = actMean + 2 * actMad;
          for (int i = 0; i < blockLen; i++) {
            int32_t a = pcmBuffer[i] < 0 ? -pcmBuffer[i] : pcmBuffer[i];
            blockAbsSum += a;
            int32_t dev = a - actMean;
            blockDevSum += (dev < 0 ? -dev : dev);
            if (a > threshold) blockAbove++;
          }
          /* Exponential smoothing: mean = (15*mean + blockMean) / 16 */
          int32_t blockMean = blockAbsSum / blockLen;
          int32_t blockMad  = blockDevSum / blockLen;
          actMean = (15 * actMean + blockMean) >> 4;
          actMad  = (15 * actMad  + blockMad)  >> 4;
          actAbove += blockAbove;
          actTotal += blockLen;

          /* Every 1 second (48000 samples): evaluate activity ratio */
          if (actTotal >= SAMPLE_RATE) {
            uint8_t ratio = (uint8_t)(actAbove * 100 / actTotal);
            actRatio = ratio;
            uint8_t interesting = (ratio >= cfg.activityMinPct &&
                                   ratio <= cfg.activityMaxPct) ? 1 : 0;

            /* Squelch mode: flag for this window */
            if (cfg.activityMode == 2) {
              actSquelch = interesting ? 0 : 1;
            }

            /* Gate mode: start/stop recording based on activity */
            if (cfg.activityMode == 3) {
              if (interesting) {
                actHolddown = cfg.activityHoldSec;
                if (!actGateOpen) {
                  actGateOpen = 1;
                  /* Re-start recording */
                  uint8_t c = CMD_START_REC;
                  osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
                }
              } else {
                if (actHolddown > 0) {
                  actHolddown--;
                } else if (actGateOpen) {
                  actGateOpen = 0;
                  /* Stop recording */
                  uint8_t c = CMD_STOP_REC;
                  osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
                }
              }
            }

            actAbove = 0;
            actTotal = 0;
          }
        }

        /* Step 5b: Mel spectrogram feed (BEFORE squelch, needs real audio) */
        if (cfg.missionMode != MISSION_RECORD && modelLoaded) {
          /* Decimate: average pairs of int32 ADF1 samples → int16 for Q15 FFT.
           * ADF1 DFLTDR is a 25-bit sinc4 output left-justified in 32 bits
           * (bits [31:7]).  >> 8 gives ~17 usable bits, saturate to int16. */
          int nDecim = blockLen / 2;
          for (int i = 0; i < nDecim; i++) {
            int32_t avg = (pcmBuffer[i * 2] + pcmBuffer[i * 2 + 1]) / 2;
            int32_t s16 = avg >> 8;
            if (s16 > 32767) s16 = 32767;
            if (s16 < -32768) s16 = -32768;
            melAccumBuf[melAccumIdx++] = (int16_t)s16;
          }

          /* Debug: print pcmBuffer and melAccumBuf samples once */
          {
            static int melDecimDbg = 0;
            if (melDecimDbg == 0) {
              printf("PCM32: %ld %ld %ld %ld\r\n",
                     (long)pcmBuffer[0], (long)pcmBuffer[1],
                     (long)pcmBuffer[2], (long)pcmBuffer[3]);
              printf("MEL16: %d %d %d %d\r\n",
                     (int)melAccumBuf[0], (int)melAccumBuf[1],
                     (int)melAccumBuf[2], (int)melAccumBuf[3]);
              melDecimDbg = 1;
            }
          }

          /* Process one mel hop when we have 256 decimated samples */
          if (melAccumIdx >= MEL_HOP) {
            HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_7); /* blue LED: mel heartbeat */
            mel_process_frame(melAccumBuf);
            melAccumIdx = 0;

            /* When mel buffer is full (256 frames = 3s), notify inferenceTask */
            if (mel_get_frame_count() >= MEL_FRAMES) {
              if (inferenceTaskHandle != NULL) {
                osThreadFlagsSet(inferenceTaskHandle, 0x01);
              }
            }
          }
        }

        /* Step 5: Squelch — zero out uninteresting audio (after mel feed) */
        if (cfg.activityMode == 2 && actSquelch) {
          memset(pcmBuffer, 0, blockLen * sizeof(int32_t));
        }

        /* Step 6: Encode + write (skip if detect-only mode) */
        if (cfg.missionMode == MISSION_DETECT) goto skip_write;
        if (dev.rec.format == REC_FMT_WAV) {
          /* Pack interleaved stereo 24-bit LE: L0,R0,L1,R1,... (6 bytes/frame) */
          static uint8_t packed[512 * 6];  /* stereo: 2 channels × 3 bytes × blockLen */
          for (int i = 0; i < blockLen; i++) {
            uint32_t vL = (uint32_t)pcmBuffer[i];
            uint32_t vR = (uint32_t)pcmBufferR[i];
            packed[i * 6 + 0] = (uint8_t)(vL);
            packed[i * 6 + 1] = (uint8_t)(vL >> 8);
            packed[i * 6 + 2] = (uint8_t)(vL >> 16);
            packed[i * 6 + 3] = (uint8_t)(vR);
            packed[i * 6 + 4] = (uint8_t)(vR >> 8);
            packed[i * 6 + 5] = (uint8_t)(vR >> 16);
          }
          osMutexAcquire(fileMtxHandle, osWaitForever);
          UINT bw;
          FRESULT fres = f_write(&wavFile, packed, blockLen * 6, &bw);
          if (fres != FR_OK) {
            printf("f_write FAILED: %d at %lu bytes\r\n", fres, (unsigned long)totalDataBytes);
            f_close(&wavFile);
            isRecording = 0;
          }
          totalDataBytes += bw;

          /* Sync every ~1 second (stereo: 2ch × 3 bytes × 48000 = 288000 bytes/sec) */
          if ((totalDataBytes % (SAMPLE_RATE * 6)) < (uint32_t)(blockLen * 6)) {
            f_sync(&wavFile);
          }
          osMutexRelease(fileMtxHandle);
        } else {
          /* FLAC encode -accumulates 8 calls into one 4096-sample block */
          uint32_t encoded = flac_enc_process_stereo(&flacEncoder, pcmBuffer, pcmBufferR, blockLen);
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
            flac_enc_notify_write(&flacEncoder, bw);

            /* Sync every ~8 frames (~680ms) */
            if ((flacEncoder.frameNumber % 8) == 0) {
              f_sync(&wavFile);
            }
            osMutexRelease(fileMtxHandle);
          }
        }
        /* Step 7: Check chunk duration — split file if elapsed */
        if (cfg.chunkMinutes > 0 && isRecording) {
          uint32_t elapsedMs = HAL_GetTick() - recStartTick;
          if (elapsedMs >= (uint32_t)cfg.chunkMinutes * 60000u) {
            extern void chunkRecording(void);
            diagLog("Chunk rotation");
            chunkRecording();
          }
        }

skip_write:
        (void)0; /* label requires a statement */
      }
    } else {
      /* Not recording -drain ring and track peak for live audio monitor.
       * Apply HPF to remove DC offset / LF noise (same as recording path)
       * using separate state so recording init doesn't conflict. */
      static int32_t monHpfPrevIn = 0, monHpfPrevOut = 0;
      int32_t alpha = computeHpfAlpha(cfg.bpfLow);
      {
      /* Drain BOTH ring buffers — left for peak monitoring, right to keep it current */
      while ((ringHead - ringTail) >= (AUDIO_BUF_SIZE / 2)) {
        const int blockLen = AUDIO_BUF_SIZE / 2;
        uint32_t t = ringTail;
        int32_t blockPeak = 0;
        for (int i = 0; i < blockLen; i++) {
          int32_t raw = pcmRing[t & PCM_RING_MASK];
          t++;
          if (alpha > 0) {
            int32_t out = (int32_t)(((int64_t)alpha * (monHpfPrevOut + raw - monHpfPrevIn)) >> 16);
            monHpfPrevIn = raw;
            monHpfPrevOut = out;
            raw = out;
          }
          int32_t a = raw < 0 ? -raw : raw;
          if (a > blockPeak) blockPeak = a;
        }
        ringTail = t;
        if (blockPeak > audioPeakLevel) audioPeakLevel = blockPeak;
      }
      /* Drain right ring to keep it current (discard data, no peak tracking) */
      ringTailR = ringHeadR;
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
  /* Let GPS + bridge tasks finish their init messages before showing menu */
  osDelay(3000);
  printMenu();

  uint32_t lastHeartbeat = HAL_GetTick();
  for (;;)
  {
    /* Heartbeat: toggle PD13 every 1s so user can see RTOS is alive */
    if ((HAL_GetTick() - lastHeartbeat) >= 1000) {
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);
        lastHeartbeat = HAL_GetTick();

        /* Low battery auto-sleep: stop recording if battery below threshold */
        if (cfg.autoStop && cfg.lowBatPct > 0 && batteryMv > 0) {
            uint32_t pct = (batteryMv <= 3000) ? 0 :
                           (batteryMv >= 4200) ? 100 :
                           (batteryMv - 3000) * 100 / 1200;
            if (pct <= cfg.lowBatPct && isRecording) {
                printf("PWR: Low battery (%lu%%) — stopping recording\r\n",
                       (unsigned long)pct);
                uint8_t cmd = CMD_STOP_REC;
                osMessageQueuePut(audioCmdQueueHandle, &cmd, 0, 0);
            }
        }
    }

    /* Autonomous schedule check — every 1 second */
    if ((HAL_GetTick() - lastScheduleCheckTick) >= SCHEDULE_CHECK_INTERVAL_MS) {
        lastScheduleCheckTick = HAL_GetTick();
        powerScheduleCheck();
    }

    /* SD card detect — auto-mount on insert, auto-unmount on remove */
    {
      uint32_t flags = osThreadFlagsClear(0x30);
      if (flags & 0x10) {
        /* Card inserted — debounce then mount */
        osDelay(50);
        if (HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin) == GPIO_PIN_RESET && !sdMounted) {
          extern FATFS USERFatFS;
          extern char USERPath[];
          osMutexAcquire(fileMtxHandle, osWaitForever);
          if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
            sdMounted = 1;
            { extern void sdCreateDirs(void); sdCreateDirs(); }
            printf("SD card inserted — mounted\r\n");
          } else {
            printf("SD card inserted — mount failed\r\n");
          }
          osMutexRelease(fileMtxHandle);
          { extern void sd_space_refresh(void); sd_space_refresh(); }
        }
      }
      if (flags & 0x20) {
        /* Card removed — stop recording if active, unmount */
        osDelay(50);
        if (HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin) != GPIO_PIN_RESET && sdMounted) {
          if (isRecording) {
            uint8_t cmd = CMD_STOP_REC;
            osMessageQueuePut(audioCmdQueueHandle, &cmd, 0, 0);
            osDelay(100);
          }
          extern char USERPath[];
          osMutexAcquire(fileMtxHandle, osWaitForever);
          f_mount(NULL, USERPath, 0);
          USER_disk_deinit();
          sdMounted = 0;
          osMutexRelease(fileMtxHandle);
          printf("SD card removed — unmounted\r\n");
        }
      }
    }

    int c = getChar(10);  /* block up to 10ms -replaces poll + osDelay(10) */
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
          fflush(stdout);
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
      {
        printGpsStatus();
        printf("\r\nGPS Commands:\r\n");
        printf("  1. GPS Power ON\r\n");
        printf("  2. GPS Power OFF\r\n");
        printf("  3. GPS Reset\r\n");
        printf("  4. Toggle Raw Output\r\n");
        printf("  ESC/other: Back to main menu\r\n");
        printf("[GPS] > ");
        fflush(stdout);
        int gc = -1;
        while (gc < 0) {
          gc = getChar(osWaitForever);
          if (gc == '\r' || gc == '\n') gc = -1;
        }
        printf("%c\r\n", gc);
        switch (gc) {
        case '1':
          gpsSetPower(1);
          break;
        case '2':
          gpsSetPower(0);
          break;
        case '3':
          gpsReset();
          break;
        case '4':
          gpsRawOutput = !gpsRawOutput;
          printf("GPS raw output: %s\r\n", gpsRawOutput ? "ON" : "OFF");
          break;
        default:
          break;
        }
        printMenu();
        break;
      }

      case '9':
      {
        uint32_t age = dev.comms.lastSpiTick ?
            (HAL_GetTick() - dev.comms.lastSpiTick) : 0;
        printf("\r\n=== Comms Status ===\r\n");
        printf("STM32 FW:     %s\r\n", FW_VERSION);
        printf("ESP32 FW:     %s\r\n",
               dev.comms.espFwVersion[0] ? dev.comms.espFwVersion : "Unknown");
        printf("ESP32 Bridge: %s\r\n",
               dev.comms.espReady ? "Ready" : "No response");
        printf("SPI2:         %lu transactions\r\n",
               (unsigned long)dev.comms.spiTransactions);
        printf("Last SPI:     %lu ms ago\r\n", (unsigned long)age);
        printf("WiFi AP:      Managed by ESP32\r\n");
        printf("BLE Beacon:   Managed by ESP32\r\n");
        printf("====================\r\n");
        printMenu();
        break;
      }

      case 'f':
      case 'F':
        if (isRecording) {
          printf("Stop recording first!\r\n");
        } else {
          dev.rec.format = (dev.rec.format == REC_FMT_WAV) ? REC_FMT_FLAC : REC_FMT_WAV;
          cfg.recFormat = dev.rec.format;
          configSave();
          printf("Recording format: %s\r\n", dev.rec.format == REC_FMT_WAV ? "WAV" : "FLAC");
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

      case 'z':
      case 'Z':
        if (isRecording) {
          printf("Stop recording first!\r\n");
        } else {
          printf("Sleep duration (seconds, 1-65535) > ");
          fflush(stdout);
          char sleepBuf[8];
          int si = 0;
          for (;;) {
            int sc = getChar(osWaitForever);
            if (sc == '\r' || sc == '\n') break;
            if (sc == 0x7f || sc == '\b') {
              if (si > 0) { si--; printf("\b \b"); fflush(stdout); }
              continue;
            }
            if (sc >= '0' && sc <= '9' && si < 6) {
              sleepBuf[si++] = (char)sc;
              printf("%c", sc); fflush(stdout);
            }
          }
          sleepBuf[si] = '\0';
          printf("\r\n");
          uint32_t sleepSec = (uint32_t)atoi(sleepBuf);
          if (sleepSec < 1 || sleepSec > 65535) {
            printf("Invalid (1-65535)\r\n");
          } else {
            /* Unmount SD before sleep to prevent FAT corruption */
            uint8_t wasMounted = sdMounted;
            if (sdMounted) {
              extern char USERPath[];
              osMutexAcquire(fileMtxHandle, osWaitForever);
              f_mount(NULL, USERPath, 0);
              USER_disk_deinit();
              sdMounted = 0;
              osMutexRelease(fileMtxHandle);
            }
            printf("Entering Stop 2 for %lu seconds...\r\n",
                   (unsigned long)sleepSec);
            fflush(stdout);
            osDelay(50);
            {
              wake_source_t ws = enterStop2(sleepSec);
              printf("\r\nWoke from Stop 2 (%s, %lu sec)\r\n",
                     ws == WAKE_ESP32 ? "ESP32" : "RTC",
                     (unsigned long)sleepSec);
            }
            /* Remount SD after wake */
            if (wasMounted) {
              extern FATFS USERFatFS;
              extern char USERPath[];
              osMutexAcquire(fileMtxHandle, osWaitForever);
              if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
                sdMounted = 1;
                printf("SD: remounted OK\r\n");
              } else {
                printf("SD: remount failed\r\n");
              }
              osMutexRelease(fileMtxHandle);
            }
          }
        }
        printMenu();
        break;

      case 'a':
      case 'A':
      {
        /* Toggle autonomous schedule mode */
        if (dev.pwr.scheduleActive) {
            dev.pwr.scheduleActive = 0;
            dev.pwr.state = PWR_DEV_MODE;
            printf("Schedule: OFF (dev mode)\r\n");
        } else {
            if (!dev.pwr.rtcSynced) {
                printf("Schedule: Need GPS fix to sync RTC first!\r\n");
            } else if (!schedule_has_windows(&cfg)) {
                printf("Schedule: No windows configured\r\n");
            } else {
                dev.pwr.scheduleActive = 1;
                dev.pwr.devMode = 0;
                dev.pwr.state = PWR_SCHEDULED_REC;  /* start evaluating */
                printf("Schedule: ON — autonomous recording enabled\r\n");
            }
        }
        printMenu();
        break;
      }

      case 'c':
      case 'C':
      {
        if (isRecording) {
            printf("Stop recording first!\r\n");
        } else {
            audioChannelRight = !audioChannelRight;
            printf("Audio channel: %s\r\n", audioChannelRight ? "RIGHT" : "LEFT");
        }
        printMenu();
        break;
      }

      case 'd':
      case 'D':
      {
        /* Toggle dev mode */
        dev.pwr.devMode = !dev.pwr.devMode;
        if (dev.pwr.devMode) {
            dev.pwr.scheduleActive = 0;
            dev.pwr.state = PWR_DEV_MODE;
            printf("Dev mode: ON (schedule disabled, everything stays on)\r\n");
        } else {
            dev.pwr.state = PWR_SCHEDULED_REC;
            printf("Dev mode: OFF\r\n");
        }
        printMenu();
        break;
      }

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

/* ========================= FreeRTOS hooks ========================= */

/* RTT write — works with interrupts disabled (no UART needed) */
static void rtt_poll_puts(const char *s)
{
    SEGGER_RTT_Write(0, s, strlen(s));
}

void vAssertCalled(const char *file, int line)
{
    taskDISABLE_INTERRUPTS();
    char buf[80];
    snprintf(buf, sizeof(buf), "\r\n!!! ASSERT: %s:%d\r\n", file, line);
    rtt_poll_puts(buf);
    /* Blink status LED (PD13) to signal fault */
    for (;;) {
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);
        for (volatile int i = 0; i < 500000; i++) {}
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    taskDISABLE_INTERRUPTS();
    char buf[80];
    snprintf(buf, sizeof(buf), "\r\n!!! STACK OVERFLOW: %s\r\n", pcTaskName);
    rtt_poll_puts(buf);
    /* Blink status LED (PD13) to signal fault */
    for (;;) {
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);
        for (volatile int i = 0; i < 500000; i++) {}
    }
}

/* ========================= Inference Task ========================= */

/* Load model from SD card /model/quail_model.tflite + /model/model_config.json */
static int loadModelFromSD(void)
{
    FIL f;
    UINT br;
    FRESULT fres;

    /* Load .tflite model binary */
    osMutexAcquire(fileMtxHandle, osWaitForever);
    fres = f_open(&f, "model/quail_model.tflite", FA_READ);
    if (fres != FR_OK) {
        osMutexRelease(fileMtxHandle);
        printf("Inference: model/quail_model.tflite not found\r\n");
        return -1;
    }
    DWORD fsize = f_size(&f);
    if (fsize > sizeof(modelBuf)) {
        f_close(&f);
        osMutexRelease(fileMtxHandle);
        printf("Inference: model too large (%lu > %u)\r\n",
               (unsigned long)fsize, (unsigned)sizeof(modelBuf));
        return -1;
    }
    fres = f_read(&f, modelBuf, fsize, &br);
    f_close(&f);
    if (fres != FR_OK || br != fsize) {
        osMutexRelease(fileMtxHandle);
        printf("Inference: model read error\r\n");
        return -1;
    }
    modelBufSize = br;
    printf("Inference: Loaded model (%lu bytes)\r\n", (unsigned long)modelBufSize);

    /* Load model_config.json for labels + normalization */
    fres = f_open(&f, "model/model_config.json", FA_READ);
    if (fres == FR_OK) {
        char jsonBuf[1024];
        fres = f_read(&f, jsonBuf, sizeof(jsonBuf) - 1, &br);
        f_close(&f);
        if (fres == FR_OK) {
            jsonBuf[br] = '\0';

            /* Parse mel_min */
            char *p = strstr(jsonBuf, "\"mel_min\"");
            if (p) {
                p = strchr(p, ':');
                if (p) modelMelMin = strtof(p + 1, NULL);
            }
            /* Parse mel_max */
            p = strstr(jsonBuf, "\"mel_max\"");
            if (p) {
                p = strchr(p, ':');
                if (p) modelMelMax = strtof(p + 1, NULL);
            }

            /* Parse labels array (simple: find "labels" then extract quoted strings) */
            modelNumLabels = 0;
            p = strstr(jsonBuf, "\"labels\"");
            if (p) {
                p = strchr(p, '[');
                if (p) {
                    p++;
                    while (modelNumLabels < TFLITE_MAX_CLASSES) {
                        char *q1 = strchr(p, '"');
                        if (!q1) break;
                        q1++;
                        char *q2 = strchr(q1, '"');
                        if (!q2) break;
                        int len = (int)(q2 - q1);
                        if (len > 31) len = 31;
                        memcpy(modelLabels[modelNumLabels], q1, len);
                        modelLabels[modelNumLabels][len] = '\0';
                        modelNumLabels++;
                        p = q2 + 1;
                        if (strchr(p, ']') && strchr(p, ']') < strchr(p, '"'))
                            break;
                    }
                }
            }
            printf("Inference: %d labels, mel_min=%.2f, mel_max=%.2f\r\n",
                   modelNumLabels, modelMelMin, modelMelMax);
        }
    } else {
        printf("Inference: model_config.json not found (using defaults)\r\n");
    }
    osMutexRelease(fileMtxHandle);

    /* Initialize TFLite Micro interpreter */
    if (tflite_init(modelBuf, modelBufSize, tensorArena, sizeof(tensorArena)) != 0) {
        printf("Inference: TFLite init FAILED\r\n");
        return -1;
    }

    const tflite_info_t *info = tflite_get_info();
    printf("Inference: TFLite ready - input=%d bytes, %d classes, arena=%d bytes\r\n",
           info->input_size, info->output_classes, info->arena_used);
    printf("Inference: input quant scale=%.6f zp=%d, mel_min=%.2f mel_max=%.2f\r\n",
           (double)info->input_scale, info->input_zero_point,
           (double)modelMelMin, (double)modelMelMax);

    /* Self-test: verify model responds differently to different inputs.
     * tflite_infer copies into the input tensor, so we just need a
     * source buffer.  Use modelBuf (model flatbuffer) as scratch since
     * it's already allocated and the model is parsed — raw bytes are fine. */
    {
        static int8_t testBuf[10240];  /* reuse .bss, freed after this scope */
        tflite_result_t tr[TFLITE_MAX_CLASSES];

        memset(testBuf, -128, info->input_size);
        int n1 = tflite_infer(testBuf, info->input_size, tr, TFLITE_MAX_CLASSES);
        int8_t raw_min = (n1 > 0) ? tr[0].raw_score : -99;

        memset(testBuf, 0, info->input_size);
        int n2 = tflite_infer(testBuf, info->input_size, tr, TFLITE_MAX_CLASSES);
        int8_t raw_mid = (n2 > 0) ? tr[0].raw_score : -99;

        memset(testBuf, 127, info->input_size);
        int n3 = tflite_infer(testBuf, info->input_size, tr, TFLITE_MAX_CLASSES);
        int8_t raw_max = (n3 > 0) ? tr[0].raw_score : -99;

        printf("Inference: self-test raw[-128]=%d raw[0]=%d raw[127]=%d\r\n",
               (int)raw_min, (int)raw_mid, (int)raw_max);
    }

    /* Initialize mel spectrogram */
    mel_init(modelMelMin, modelMelMax);

    /* Compensate for device mic level vs training data level.
     * +18dB was calibrated at ADF gain=6 (36dB).  Adjust for current
     * gain setting: each gain step is 6dB, higher gain needs less comp. */
    float gainComp = 18.0f - (float)(cfg.gain - 6) * 6.0f;
    mel_set_gain_compensation(gainComp);
    printf("Inference: gain compensation %.0f dB (ADF gain=%d)\r\n",
           gainComp, cfg.gain);

    modelLoaded = 1;
    return 0;
}

/* Build detection CSV filename for today */
static void detUpdateCsvFilename(void)
{
    if (!ppsSynced || ppsUtcDate == 0) return;

    uint32_t dd = ppsUtcDate / 10000;
    uint32_t mm = (ppsUtcDate / 100) % 100;
    uint32_t yy = ppsUtcDate % 100;
    uint32_t dateKey = (2000 + yy) * 10000 + mm * 100 + dd;

    if (dateKey != detCsvDate) {
        detCsvDate = dateKey;
        snprintf(detCsvFilename, sizeof(detCsvFilename),
                 "logs/detections_%08lu_%s.csv",
                 (unsigned long)dateKey, cfg.stationId);
    }
}

/* Log a detection to the daily CSV file */
static void detLogCsv(const char *species, float confidence,
                       uint64_t windowStartSample)
{
    if (!sdMounted || detCsvFilename[0] == '\0') return;

    FIL f;
    osMutexAcquire(fileMtxHandle, osWaitForever);

    /* Open or create CSV file */
    FRESULT fres = f_open(&f, detCsvFilename, FA_WRITE | FA_OPEN_APPEND);
    if (fres == FR_OK && f_size(&f) == 0) {
        /* New/empty file — write CSV header */
        f_printf(&f, "UTC_Timestamp,Species,Confidence,Latitude,Longitude,"
                     "Altitude,Temperature_C,Humidity_RH,"
                     "StationId,PPS_Sync,Window_Start_Sample\n");
    } else if (fres != FR_OK) {
        fres = f_open(&f, detCsvFilename, FA_WRITE | FA_CREATE_ALWAYS);
        if (fres == FR_OK) {
            f_printf(&f, "UTC_Timestamp,Species,Confidence,Latitude,Longitude,"
                         "Altitude,Temperature_C,Humidity_RH,"
                         "StationId,PPS_Sync,Window_Start_Sample\n");
        }
    }

    if (fres == FR_OK) {
        /* Build UTC timestamp for window center */
        char ts[24];
        if (ppsSynced && ppsUtcDate != 0) {
            uint32_t dd = ppsUtcDate / 10000;
            uint32_t mm = (ppsUtcDate / 100) % 100;
            uint32_t yy = ppsUtcDate % 100;
            uint32_t hh = ppsUtcTime / 10000;
            uint32_t mn = (ppsUtcTime / 100) % 100;
            uint32_t ss = ppsUtcTime % 100;
            snprintf(ts, sizeof(ts), "20%02lu%02lu%02luT%02lu%02lu%02luZ",
                     (unsigned long)yy, (unsigned long)mm, (unsigned long)dd,
                     (unsigned long)hh, (unsigned long)mn, (unsigned long)ss);
        } else {
            strncpy(ts, "unknown", sizeof(ts));
        }

        /* Get current GPS position */
        float lat = ppsLatitude;
        float lon = ppsLongitude;
        float alt = ppsAltitude;
        if (cfg.surveyCount > 0) {
            lat = cfg.surveyLat;
            lon = cfg.surveyLon;
            alt = cfg.surveyAlt;
        }

        /* Temperature and humidity from SHT30 */
        int32_t tempWhole = sht30TempC100 / 100;
        int32_t tempFrac  = sht30TempC100 % 100;
        if (tempFrac < 0) tempFrac = -tempFrac;
        uint32_t humWhole = sht30HumRH100 / 100;
        uint32_t humFrac  = sht30HumRH100 % 100;

        /* FatFS f_printf doesn't support %f or %llu — use snprintf + f_puts */
        char line[224];
        snprintf(line, sizeof(line),
                 "%s,%s,%.2f,%.6f,%.6f,%.1f,%ld.%02ld,%lu.%02lu,%s,%d,%llu\n",
                 ts, species, (double)confidence,
                 (double)lat, (double)lon, (double)alt,
                 (long)tempWhole, (long)tempFrac,
                 (unsigned long)humWhole, (unsigned long)humFrac,
                 cfg.stationId, ppsSynced ? 1 : 0,
                 (unsigned long long)windowStartSample);
        f_puts(line, &f);
        f_close(&f);
    }

    osMutexRelease(fileMtxHandle);
}

static void StartInferenceTask(void *argument)
{
    (void)argument;

    /* Wait for SD to mount before trying to load model */
    while (!sdMounted) {
        osDelay(500);
    }

    /* Attempt to load model from SD card */
    if (loadModelFromSD() != 0) {
        printf("Inference: No model -task idle\r\n");
        /* Stay alive so $MODEL,RELOAD can retry */
        for (;;) {
            osThreadFlagsWait(0x01, osFlagsWaitAny, osWaitForever);
            /* Check if this is a reload request (flag 0x02) */
            if (sdMounted) {
                loadModelFromSD();
            }
        }
    }

    /* Main inference loop */
    for (;;) {
        /* Wait for audioTask to signal a complete mel buffer */
        uint32_t flags = osThreadFlagsWait(0x03, osFlagsWaitAny, osWaitForever);

        /* Flag 0x02 = reload request */
        if (flags & 0x02) {
            modelLoaded = 0;
            tflite_deinit();
            if (loadModelFromSD() != 0) {
                printf("Inference: Reload failed\r\n");
            }
            continue;
        }

        /* Flag 0x01 = mel buffer ready */
        if (!(flags & 0x01)) continue;
        if (!modelLoaded || cfg.missionMode == MISSION_RECORD) continue;

        /* Get completed spectrogram and run inference */
        const int8_t *melData = mel_get_buffer();
        uint64_t windowStart = absSampleCount - (uint64_t)(MEL_FRAMES * MEL_HOP * 2);

        /* Debug: dump mel buffer stats + signal chain diagnostics */
        {
            int8_t bMin = 127, bMax = -128;
            int32_t bSum = 0;
            for (int k = 0; k < MEL_FRAMES * MEL_BINS; k++) {
                if (melData[k] < bMin) bMin = melData[k];
                if (melData[k] > bMax) bMax = melData[k];
                bSum += melData[k];
            }
            int16_t dbgIn, dbgFft, dbgMag;
            mel_get_debug(&dbgIn, &dbgFft, &dbgMag);
            printf("MEL: min=%d max=%d avg=%d IN=%d FFT=%d MAG=%d\r\n",
                   (int)bMin, (int)bMax, (int)(bSum / (MEL_FRAMES * MEL_BINS)),
                   (int)dbgIn, (int)dbgFft, (int)dbgMag);
        }

        tflite_result_t results[TFLITE_MAX_CLASSES];
        int nResults = tflite_infer(melData, MEL_FRAMES * MEL_BINS,
                                     results, TFLITE_MAX_CLASSES);

        detWindowsProcessed++;
        detUpdateCsvFilename();

        /* Debug: print raw inference output for every window */
        if (nResults > 0) {
            printf("INF: raw=%d conf=%d%% thr=%d%% win=%lu\r\n",
                   (int)results[0].raw_score,
                   (int)(results[0].confidence * 100.0f),
                   (int)cfg.detConfThresh,
                   (unsigned long)detWindowsProcessed);
        }

        if (nResults <= 0) continue;

        /* Check each class against threshold */
        uint8_t threshold = cfg.detConfThresh;
        for (int i = 0; i < nResults; i++) {
            uint8_t confPct = (uint8_t)(results[i].confidence * 100.0f);
            if (confPct >= threshold && results[i].class_index < modelNumLabels) {
                const char *species = modelLabels[results[i].class_index];

                /* Update last detection info */
                strncpy(detLastSpecies, species, sizeof(detLastSpecies) - 1);
                detLastConf = confPct;
                if (ppsSynced && ppsUtcDate != 0) {
                    uint32_t dd = ppsUtcDate / 10000;
                    uint32_t mm = (ppsUtcDate / 100) % 100;
                    uint32_t yy = ppsUtcDate % 100;
                    uint32_t hh = ppsUtcTime / 10000;
                    uint32_t mn = (ppsUtcTime / 100) % 100;
                    uint32_t ss = ppsUtcTime % 100;
                    snprintf(detLastTime, sizeof(detLastTime),
                             "20%02lu%02lu%02luT%02lu%02lu%02luZ",
                             (unsigned long)yy, (unsigned long)mm, (unsigned long)dd,
                             (unsigned long)hh, (unsigned long)mn, (unsigned long)ss);
                }
                detHits++;

                /* Update health stats */
                health.detections++;
                strncpy(health.lastSpecies, species, sizeof(health.lastSpecies) - 1);
                health.lastSpecies[sizeof(health.lastSpecies) - 1] = '\0';
                health.lastConfidence = confPct;
                strncpy(health.lastDetTime, detLastTime, sizeof(health.lastDetTime) - 1);
                health.lastDetTime[sizeof(health.lastDetTime) - 1] = '\0';

                /* Log to CSV */
                detLogCsv(species, results[i].confidence, windowStart);

                printf("DET: %s %d%% @ %s\r\n", species, confPct, detLastTime);

                /* Detection data is pushed to ESP32 via SPI JSON */
            }
        }
    }
}

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

    /* Auto-sync RTC from GPS on first valid fix (and periodically) */
    if (gpsData.valid && gpsData.utc_date != 0 && !dev.pwr.rtcSynced) {
        rtcSyncFromGps();
        printf("RTC: Synced from GPS (%02lu:%02lu:%02lu UTC)\r\n",
               (unsigned long)(gpsData.utc_time / 10000),
               (unsigned long)((gpsData.utc_time / 100) % 100),
               (unsigned long)(gpsData.utc_time % 100));
    }

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
    printf("Power:      %s\r\n", gpsPowered ? "ON" : "OFF");
    printf("Raw output: %s\r\n", gpsRawOutput ? "ON" : "OFF");
    printf("==================\r\n");
}

static void gpsSetPower(uint8_t on)
{
    if (on) {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);   /* GPS_EN high */
        osDelay(10);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);   /* WAKE high */
        gpsPowered = 1;
        printf("GPS: Power ON\r\n");
    } else {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_RESET); /* WAKE low */
        osDelay(10);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET); /* GPS_EN low */
        gpsPowered = 0;
        printf("GPS: Power OFF\r\n");
    }
}

static void gpsReset(void)
{
    printf("GPS: Resetting...\r\n");
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_RESET);     /* nRESET low */
    osDelay(100);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_SET);       /* nRESET high */
    printf("GPS: Reset complete\r\n");
}

static void StartGpsTask(void *argument)
{
    char buf[128];
    int pos = 0;

    printf("GPS: Listening on USART1 (9600 baud)\r\n");

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
uint16_t configGetMicHeading(void) { return cfg.micHeading; }

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
            /* Survey results pushed to ESP32 via SPI JSON */
            return;
        }

        /* Incremental mean: avg += (new - avg) / count */
        cfg.surveyCount++;
        cfg.surveyLat += (lat - cfg.surveyLat) / (float)cfg.surveyCount;
        cfg.surveyLon += (lon - cfg.surveyLon) / (float)cfg.surveyCount;
        cfg.surveyAlt += (alt - cfg.surveyAlt) / (float)cfg.surveyCount;

        /* CLI progress every 10 fixes */
        if ((cfg.surveyCount % 10) == 0) {
            uint32_t elapsed = (HAL_GetTick() - surveyStartTick) / 1000;
            uint32_t remain = elapsed < 300 ? 300 - elapsed : 0;
            printf("Survey: %lu fixes, %lus remaining\r\n",
                   (unsigned long)cfg.surveyCount, (unsigned long)remain);
        }
        /* Status pushed to ESP32 via SPI JSON */
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

/* Diagnostic event log — append timestamped events to diag.log on SD card */
void diagLog(const char *event)
{
    if (!sdMounted) return;

    uint32_t uptimeS = HAL_GetTick() / 1000;
    char line[160];
    if (ppsSynced && ppsUtcDate != 0) {
        uint32_t d = ppsUtcDate, t = ppsUtcTime;
        snprintf(line, sizeof(line), "[%02lu/%02lu/%02lu %02lu:%02lu:%02lu up=%lus] %s\n",
                 (unsigned long)(d/10000), (unsigned long)((d/100)%100),
                 (unsigned long)(d%100),
                 (unsigned long)(t/10000), (unsigned long)((t/100)%100),
                 (unsigned long)(t%100),
                 (unsigned long)uptimeS, event);
    } else {
        snprintf(line, sizeof(line), "[up=%lus] %s\n",
                 (unsigned long)uptimeS, event);
    }

    if (osMutexAcquire(fileMtxHandle, 200) == osOK) {
        FIL f;
        if (f_open(&f, "logs/diag.log", FA_OPEN_APPEND | FA_WRITE) == FR_OK) {
            UINT bw;
            f_write(&f, line, strlen(line), &bw);
            f_close(&f);
        }
        osMutexRelease(fileMtxHandle);
    }

    printf("DIAG: %s", line);
}

/* ========================= Audio Stream Decimation ========================= */

/* Decimate 48kHz int32 ring buffer → 8kHz int16 output.
 * Averages groups of 6 consecutive samples. Uses independent tail pointer
 * so streaming doesn't affect recording. Returns number of output samples. */
static uint16_t decimate_8k(int16_t *out, uint16_t maxSamples)
{
    int32_t *ring;
    volatile uint32_t *head;
    uint32_t *tail;

    if (streamChannel == 0) {
        ring = pcmRing;
        head = &ringHead;
        tail = &streamTailL;
    } else {
        ring = pcmRingR;
        head = &ringHeadR;
        tail = &streamTailR;
    }

    uint16_t count = 0;
    uint32_t h = *head;
    uint32_t t = *tail;

    while (count < maxSamples && (h - t) >= 6) {
        int32_t sum = 0;
        for (int i = 0; i < 6; i++) {
            /* Ring stores 24-bit (>>8 from ADF). Shift >>8 more for int16 range. */
            sum += (ring[(t + i) & PCM_RING_MASK] >> 8);
        }
        t += 6;
        out[count++] = (int16_t)(sum / 6);
    }

    *tail = t;
    return count;
}

/* ========================= Bridge Task (SPI2 + housekeeping) ========================= */

static void StartBridgeTask(void *argument)
{
    /* Load health stats from flash, increment boot count */
    healthLoad();
    health.bootCount++;
    healthSave();

    /* Load config from flash (or set defaults) */
    configLoad();

    /* Apply persisted config to runtime state */
    config_apply(&cfg);

    /* Initialize power management state — default to dev mode (everything on) */
    dev.pwr.state = PWR_DEV_MODE;
    dev.pwr.devMode = 1;
    dev.pwr.scheduleActive = 0;
    dev.pwr.rtcSynced = 0;
    dev.pwr.gpsDutyCycleSec = GPS_DUTY_CYCLE_DEFAULT_SEC;

    printf("Config loaded (station=%s, gain=%d, fmt=%s)\r\n",
           cfg.stationId, cfg.gain,
           cfg.recFormat == REC_FMT_FLAC ? "FLAC" : "WAV");

    /* Log boot event */
    {
        char msg[80];
        snprintf(msg, sizeof(msg), "BOOT v%s boots=%lu bat=%lumV",
                 FW_VERSION, (unsigned long)health.bootCount,
                 (unsigned long)battReadMv());
        diagLog(msg);
    }

    /* Main loop: SPI2 bridge to ESP32 + sensor reads + health saves */
    for (;;) {
        osDelay(10);  /* yield to other tasks */

        /* Periodic SHT30 temperature/humidity read (~every 5s) */
        if ((HAL_GetTick() - lastSht30Tick) >= SHT30_INTERVAL_MS) {
            lastSht30Tick = HAL_GetTick();
            sht30Read();
            /* Update health min/max from latest readings */
            uint32_t mv = battReadMv();
            healthUpdateEnvironment(mv, (int32_t)sht30TempC100);
            /* Track GPS fix losses */
            uint8_t curGpsValid = gpsData.valid;
            if (prevGpsValid && !curGpsValid)
                health.gpsFixLosses++;
            prevGpsValid = curGpsValid;
            /* Refresh cached SD space */
            extern void sd_space_refresh(void);
            sd_space_refresh();
        }

        /* Auto-stop streaming if no SPI exchange for 1s */
        if (streamActive && (HAL_GetTick() - streamLastSpiTick) > STREAM_TIMEOUT_MS) {
            streamActive = 0;
            printf("Stream: auto-stopped (SPI timeout)\r\n");
        }

        /* SPI2 poll — 25ms when streaming, 250ms otherwise */
        uint32_t spiPollMs = streamActive ? SPI2_POLL_STREAM_MS : SPI2_POLL_MS;
        if ((HAL_GetTick() - lastSpiPollTick) >= spiPollMs) {
            lastSpiPollTick = HAL_GetTick();

            /* Build binary SPI frame */
            static spi_frame_t spi_tx_frame __attribute__((aligned(4)));
            static spi_frame_t spi_rx_frame __attribute__((aligned(4)));

            /* Determine solar charge state */
            uint8_t solar_st;
            {
                int chrg = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);
                int done_pin = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1);
                if (!chrg && done_pin)       solar_st = SOLAR_CHARGING;
                else if (chrg && !done_pin)  solar_st = SOLAR_COMPLETE;
                else if (!chrg && !done_pin) solar_st = SOLAR_FAULT;
                else                         solar_st = SOLAR_STANDBY;
            }

            spi_frame_build(&spi_tx_frame, &cfg, &dev, &health, solar_st, 0);

            /* Fill audio streaming payload into reserved region */
            if (streamActive) {
                spi_audio_payload_t *ap = (spi_audio_payload_t *)spi_tx_frame._reserved;
                ap->channel = streamChannel;
                ap->num_samples = decimate_8k(ap->samples, 214);
                ap->audio_active = (ap->num_samples > 0) ? 1 : 0;
                /* Recompute CRC since we modified the frame */
                spi_tx_frame.header.crc16 = spi_frame_crc(&spi_tx_frame);
            }

            HAL_GPIO_WritePin(SPI2_CS_PORT, SPI2_CS_PIN, GPIO_PIN_RESET);
            HAL_StatusTypeDef spiResult = HAL_SPI_TransmitReceive(&hspi2,
                (uint8_t *)&spi_tx_frame, (uint8_t *)&spi_rx_frame, SPI2_BUF_SIZE, 100);
            HAL_GPIO_WritePin(SPI2_CS_PORT, SPI2_CS_PIN, GPIO_PIN_SET);

            /* Track ESP32 comms status */
            if (spiResult == HAL_OK) {
                dev.comms.espReady = 1;
                dev.comms.spiTransactions++;
                dev.comms.lastSpiTick = HAL_GetTick();
                if (streamActive)
                    streamLastSpiTick = HAL_GetTick();
            }

            /* Process received frame — binary protocol */
            static uint32_t lastCmdSeq = 0;  /* dedup: skip already-processed commands */
            if (spi_rx_frame.header.magic == SPI_FRAME_MAGIC) {
                uint8_t cmd = SPI_CMD_NONE;
                uint8_t cmd_payload[56];
                uint32_t cmd_seq = 0;

                int adopted = spi_frame_process_rx(&spi_rx_frame, &cfg,
                                                   &cmd, cmd_payload, &cmd_seq);
                if (adopted) {
                    config_apply(&cfg);
                    configSave();
                    printf("SPI: config adopted (seq=%lu)\r\n", (unsigned long)cfg.cfg_seq);
                }

                /* Dedup: ESP32 sends same command in multiple frames for reliability.
                 * Skip if we already processed this seq number. */
                if (cmd != SPI_CMD_NONE && cmd_seq != 0 && cmd_seq == lastCmdSeq) {
                    cmd = SPI_CMD_NONE;
                }
                if (cmd != SPI_CMD_NONE && cmd_seq != 0) {
                    lastCmdSeq = cmd_seq;
                }

                /* Extract ESP32 firmware version from CMD_ESP_VERSION */
                if (cmd == SPI_CMD_ESP_VERSION) {
                    int vlen = strnlen((char *)cmd_payload, sizeof(cmd_payload));
                    if (vlen > 15) vlen = 15;
                    memcpy(dev.comms.espFwVersion, cmd_payload, vlen);
                    dev.comms.espFwVersion[vlen] = '\0';
                }

                /* Dispatch command */
                switch (cmd) {
                case SPI_CMD_REC_TOGGLE: {
                    uint8_t c = isRecording ? CMD_STOP_REC : CMD_START_REC;
                    osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
                    printf("SPI cmd: rec_toggle\r\n");
                    break;
                }
                case SPI_CMD_SD_MOUNT:
                    if (!sdMounted) {
                        extern FATFS USERFatFS;
                        extern char USERPath[];
                        osMutexAcquire(fileMtxHandle, osWaitForever);
                        if (f_mount(&USERFatFS, USERPath, 1) == FR_OK)
                            sdMounted = 1;
                        osMutexRelease(fileMtxHandle);
                    }
                    printf("SPI cmd: sd_mount\r\n");
                    break;
                case SPI_CMD_SD_EJECT:
                    if (sdMounted && !isRecording) {
                        extern char USERPath[];
                        osMutexAcquire(fileMtxHandle, osWaitForever);
                        f_mount(NULL, USERPath, 0);
                        USER_disk_deinit();
                        sdMounted = 0;
                        osMutexRelease(fileMtxHandle);
                    }
                    printf("SPI cmd: sd_eject\r\n");
                    break;
                case SPI_CMD_SD_FORMAT:
                    if (!isRecording) {
                        osMutexAcquire(fileMtxHandle, osWaitForever);
                        extern int formatSD(void);
                        formatSD();
                        osMutexRelease(fileMtxHandle);
                    }
                    printf("SPI cmd: sd_format\r\n");
                    break;
                case SPI_CMD_SURVEY_START:
                    if (!surveyActive && gpsData.fix >= 1) {
                        cfg.surveyLat = 0.0f;
                        cfg.surveyLon = 0.0f;
                        cfg.surveyAlt = 0.0f;
                        cfg.surveyCount = 0;
                        surveyActive = 1;
                        surveyStartTick = HAL_GetTick();
                    }
                    printf("SPI cmd: survey_start\r\n");
                    break;
                case SPI_CMD_SURVEY_CLEAR:
                    surveyActive = 0;
                    cfg.surveyLat = 0.0f;
                    cfg.surveyLon = 0.0f;
                    cfg.surveyAlt = 0.0f;
                    cfg.surveyCount = 0;
                    configSave();
                    printf("SPI cmd: survey_clear\r\n");
                    break;
                case SPI_CMD_SET_DETECT:
                    /* Config fields already adopted via config sync.
                     * If cmd sent without config change, no-op. */
                    printf("SPI cmd: set_detect\r\n");
                    break;
                case SPI_CMD_MODEL_RELOAD:
                    printf("SPI cmd: model_reload\r\n");
                    break;
                case SPI_CMD_SCHEDULE_ON:
                    if (dev.pwr.rtcSynced && schedule_has_windows(&cfg)) {
                        dev.pwr.scheduleActive = 1;
                        dev.pwr.devMode = 0;
                        dev.pwr.state = PWR_SCHEDULED_REC;
                        printf("SPI cmd: schedule_on\r\n");
                    }
                    break;
                case SPI_CMD_SCHEDULE_OFF:
                    dev.pwr.scheduleActive = 0;
                    dev.pwr.devMode = 1;
                    dev.pwr.state = PWR_DEV_MODE;
                    printf("SPI cmd: schedule_off\r\n");
                    break;
                case SPI_CMD_DEV_MODE:
                    dev.pwr.devMode = !dev.pwr.devMode;
                    if (dev.pwr.devMode) {
                        dev.pwr.scheduleActive = 0;
                        dev.pwr.state = PWR_DEV_MODE;
                    }
                    printf("SPI cmd: dev_mode=%d\r\n", dev.pwr.devMode);
                    break;
                case SPI_CMD_AUDIO_STREAM: {
                    uint8_t ch = cmd_payload[0];
                    uint8_t en = cmd_payload[1];
                    if (en) {
                        streamChannel = ch;
                        /* Start at live edge — no backlog */
                        streamTailL = ringHead;
                        streamTailR = ringHeadR;
                        streamLastSpiTick = HAL_GetTick();
                        streamActive = 1;
                        printf("Stream: START ch=%d\r\n", ch);
                    } else {
                        streamActive = 0;
                        printf("Stream: STOP\r\n");
                    }
                    break;
                }
                default:
                    break;
                }
            }
            /* Backward compat: fall back to JSON if ESP32 sends old format */
            else if (((uint8_t *)&spi_rx_frame)[0] == '{') {
                char *rx = (char *)&spi_rx_frame;
                rx[SPI2_BUF_SIZE - 1] = '\0';
                /* Parse ESP32 firmware version */
                char *fwp = strstr(rx, "\"espFw\":\"");
                if (fwp) {
                    fwp += 9;
                    char *end = strchr(fwp, '"');
                    if (end) {
                        int vlen = end - fwp;
                        if (vlen > 15) vlen = 15;
                        memcpy(dev.comms.espFwVersion, fwp, vlen);
                        dev.comms.espFwVersion[vlen] = '\0';
                    }
                }
                /* Parse legacy JSON commands */
                if (strstr(rx, "rec_toggle")) {
                    uint8_t c = isRecording ? CMD_STOP_REC : CMD_START_REC;
                    osMessageQueuePut(audioCmdQueueHandle, &c, 0, 0);
                }
            }
        }

        /* Periodic health save to flash (~every 5 min) */
        if ((HAL_GetTick() - lastHealthSaveTick) >= HEALTH_SAVE_INTERVAL_MS) {
            lastHealthSaveTick = HAL_GetTick();
            healthSave();
        }

    }
}

/* ========================= Flash Config ========================= */

static void configSetDefaults(device_config_t *c)
{
    memset(c, 0xFF, sizeof(*c));  /* match erased flash */
    c->magic = CONFIG_MAGIC;
    c->version = CONFIG_VERSION;
    /* Per-unit default derived from STM32U5 96-bit unique ID so unprovisioned
     * boards don't collide on the BLE/WiFi name. User can still rename via app. */
    uint32_t uid = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    snprintf(c->stationId, sizeof(c->stationId), "QT_%04lX",
             (unsigned long)(uid & 0xFFFFu));
    c->gain = 6;  /* 6 dB default */
    c->bpfLow = 150;
    c->bpfHigh = 8000;
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
    c->activityMode = 0;
    c->activityMinPct = 5;
    c->activityMaxPct = 80;
    c->activityHoldSec = 3;
    c->surveyLat = 0.0f;
    c->surveyLon = 0.0f;
    c->surveyAlt = 0.0f;
    c->surveyCount = 0;
    c->missionMode = MISSION_RECORD;
    c->detConfThresh = 50;   /* 50% default threshold */
    c->detWindowStep = 3;    /* 3 seconds */
    c->chunkMinutes = 30;    /* 30-minute file chunks */
    c->micHeading = 0xFFFF;  /* unset — installer enters via web UI */
    c->cfg_seq = 1;          /* start at 1 so ESP32 (seq=0) adopts on first sync */
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

    printf("Config: Invalid/empty -writing defaults\r\n");
    configSetDefaults(&cfg);
    cfg.crc32 = configComputeCrc(&cfg);
    configSave();
    strncpy(deviceStationId, cfg.stationId, sizeof(deviceStationId));
}

int configSave(void)
{
    cfg.cfg_seq++;  /* bump sequence so ESP32 adopts on next SPI exchange */
    cfg.crc32 = configComputeCrc(&cfg);

    /* ICACHE must be disabled during flash erase/program on STM32U5 */
    HAL_ICACHE_Disable();

    HAL_FLASH_Unlock();

    /* Clear any sticky error flags from previous operations */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    /* Compute bank and page from address (works on both 1MB VGT6 and 2MB ZIT6Q) */
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    if (CONFIG_FLASH_ADDR < FLASH_BASE + FLASH_BANK_SIZE) {
        erase.Banks = FLASH_BANK_1;
        erase.Page = (CONFIG_FLASH_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE;
    } else {
        erase.Banks = FLASH_BANK_2;
        erase.Page = (CONFIG_FLASH_ADDR - FLASH_BASE - FLASH_BANK_SIZE) / FLASH_PAGE_SIZE;
    }
    erase.NbPages = 1;
    uint32_t pageError = 0;

    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK) {
        printf("Config: Flash erase FAILED (err=0x%lx)\r\n",
               (unsigned long)HAL_FLASH_GetError());
        HAL_FLASH_Lock();
        HAL_ICACHE_Enable();
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
            HAL_ICACHE_Enable();
            return 0;
        }
    }

    HAL_FLASH_Lock();
    HAL_ICACHE_Enable();

    /* Verify readback */
    if (memcmp((const void *)CONFIG_FLASH_ADDR, &cfg, sizeof(cfg)) != 0) {
        printf("Config: Flash verify FAILED\r\n");
        return 0;
    }

    return 1;
}

/* ========================= Health Statistics Persistence ========================= */

static uint32_t healthComputeCrc(const health_stats_t *h)
{
    return crc32_compute((const uint8_t *)h, sizeof(health_stats_t) - 4);
}

static void healthLoad(void)
{
    const health_stats_t *flash = (const health_stats_t *)HEALTH_FLASH_ADDR;

    memcpy(&health, flash, sizeof(health));

    if (health.magic == HEALTH_MAGIC &&
        health.version == HEALTH_VERSION &&
        health.crc32 == healthComputeCrc(&health)) {
        printf("Health: Loaded from flash (boots=%lu, files=%lu)\r\n",
               (unsigned long)health.bootCount, (unsigned long)health.filesWritten);
        return;
    }

    printf("Health: Invalid/empty — initializing\r\n");
    memset(&health, 0, sizeof(health));
    health.magic = HEALTH_MAGIC;
    health.version = HEALTH_VERSION;
    health.battMinMv = 0xFFFFFFFF;  /* will be replaced by first reading */
    health.tempMinC100 = 32767;     /* will be replaced by first reading */
    health.tempMaxC100 = -32768;
}

int healthSave(void)
{
    health.crc32 = healthComputeCrc(&health);

    HAL_ICACHE_Disable();
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    if (HEALTH_FLASH_ADDR < FLASH_BASE + FLASH_BANK_SIZE) {
        erase.Banks = FLASH_BANK_1;
        erase.Page = (HEALTH_FLASH_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE;
    } else {
        erase.Banks = FLASH_BANK_2;
        erase.Page = (HEALTH_FLASH_ADDR - FLASH_BASE - FLASH_BANK_SIZE) / FLASH_PAGE_SIZE;
    }
    erase.NbPages = 1;
    uint32_t pageError = 0;

    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK) {
        printf("Health: Flash erase FAILED\r\n");
        HAL_FLASH_Lock();
        HAL_ICACHE_Enable();
        return 0;
    }

    /* Program 16 quadwords (256 bytes = 16 × 16 bytes) */
    const uint8_t *src = (const uint8_t *)&health;
    for (int i = 0; i < 16; i++) {
        uint32_t addr = HEALTH_FLASH_ADDR + (uint32_t)(i * 16);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr,
                              (uint32_t)(src + i * 16)) != HAL_OK) {
            printf("Health: Flash program FAILED at offset %d\r\n", i * 16);
            HAL_FLASH_Lock();
            HAL_ICACHE_Enable();
            return 0;
        }
    }

    HAL_FLASH_Lock();
    HAL_ICACHE_Enable();

    if (memcmp((const void *)HEALTH_FLASH_ADDR, &health, sizeof(health)) != 0) {
        printf("Health: Flash verify FAILED\r\n");
        return 0;
    }

    return 1;
}

void healthReset(void)
{
    uint32_t boots = health.bootCount;  /* preserve boot count across resets */
    memset(&health, 0, sizeof(health));
    health.magic = HEALTH_MAGIC;
    health.version = HEALTH_VERSION;
    health.bootCount = boots;
    health.battMinMv = 0xFFFFFFFF;
    health.tempMinC100 = 32767;
    health.tempMaxC100 = -32768;
}

/* Update battery/temp min/max — called from SHT30 periodic read */
void healthUpdateEnvironment(uint32_t battMv, int32_t tempC100)
{
    if (battMv < health.battMinMv) health.battMinMv = battMv;
    if (battMv > health.battMaxMv) health.battMaxMv = battMv;
    if (tempC100 < health.tempMinC100) health.tempMinC100 = tempC100;
    if (tempC100 > health.tempMaxC100) health.tempMaxC100 = tempC100;
}

/* Called from startRecording() in main.c */
void healthUpdateRecStart(const char *filename)
{
    health.filesWritten++;
    strncpy(health.lastFilename, filename, sizeof(health.lastFilename) - 1);
    health.lastFilename[sizeof(health.lastFilename) - 1] = '\0';
}

/* Called from stopRecording() in main.c */
void healthUpdateRecStop(uint32_t bytes, uint32_t durationSecs)
{
    health.totalBytes += bytes;
    health.recordingSecs += durationSecs;
    health.lastFileBytes = bytes;
    health.lastFileSecs = durationSecs;
}

/* ========================= SD Space Refresh ========================= */

void sd_space_refresh(void)
{
    if (!sdMounted) {
        dev.rec.sdTotalKb = 0;
        dev.rec.sdFreeKb = 0;
        return;
    }
    FATFS *fs;
    DWORD fre_clust;
    if (osMutexAcquire(fileMtxHandle, 200) == osOK) {
        if (f_getfree("", &fre_clust, &fs) == FR_OK) {
            DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
            DWORD fre_sect = fre_clust * fs->csize;
            dev.rec.sdTotalKb = (uint32_t)(tot_sect / 2);
            dev.rec.sdFreeKb  = (uint32_t)(fre_sect / 2);
        }
        osMutexRelease(fileMtxHandle);
    }
}

/* ========================= Power Management ========================= */

/* Transition to Non-Record state: stop recording, unmount SD, power down peripherals, sleep */
static void powerEnterNonRecord(void)
{
    dev.pwr.state = PWR_SCHEDULED_NONREC;

    /* Stop recording if active */
    if (isRecording) {
        uint8_t cmd = CMD_STOP_REC;
        osMessageQueuePut(audioCmdQueueHandle, &cmd, 0, 0);
        osDelay(200);  /* let audio task finalize file */
    }

    /* Unmount SD to prevent FAT corruption */
    if (sdMounted) {
        extern char USERPath[];
        osMutexAcquire(fileMtxHandle, osWaitForever);
        f_mount(NULL, USERPath, 0);
        USER_disk_deinit();
        sdMounted = 0;
        osMutexRelease(fileMtxHandle);
    }

    /* Power down peripherals */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);  /* PERIPH_VCC off */
    gpsSetPower(0);
    gpsDutyActive = 0;

    printf("PWR: Entering Non-Record sleep\r\n");
    fflush(stdout);
    osDelay(50);
}

/* Transition to Record state: power on peripherals, mount SD, start recording */
static void powerEnterRecord(void)
{
    dev.pwr.state = PWR_SCHEDULED_REC;

    /* Power on peripherals */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);   /* PERIPH_VCC on */
    osDelay(50);  /* let power rails stabilize */

    /* Mount SD */
    if (!sdMounted) {
        extern FATFS USERFatFS;
        extern char USERPath[];
        osMutexAcquire(fileMtxHandle, osWaitForever);
        if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
            sdMounted = 1;
            extern void sdCreateDirs(void);
            sdCreateDirs();
        } else {
            printf("PWR: SD mount failed!\r\n");
        }
        osMutexRelease(fileMtxHandle);
    }

    /* Start GPS duty cycle */
    gpsSetPower(1);
    lastGpsDutyTick = HAL_GetTick();
    gpsDutyActive = 0;

    /* Start recording */
    if (sdMounted && !isRecording) {
        uint8_t cmd = CMD_START_REC;
        osMessageQueuePut(audioCmdQueueHandle, &cmd, 0, 0);
    }

    printf("PWR: Entering Scheduled Record\r\n");
}

/* GPS duty cycling: periodically wake GPS for RTC sync during recording */
static void gpsDutyCycle(void)
{
    if (dev.pwr.state != PWR_SCHEDULED_REC) return;

    uint32_t dutySec = dev.pwr.gpsDutyCycleSec;
    if (dutySec == 0) dutySec = GPS_DUTY_CYCLE_DEFAULT_SEC;

    uint32_t now = HAL_GetTick();

    if (gpsDutyActive) {
        /* GPS is on, waiting for fix */
        if (gpsData.valid && gpsData.fix >= 1) {
            /* Got fix — sync RTC and power down GPS */
            rtcSyncFromGps();
            gpsSetPower(0);
            gpsDutyActive = 0;
            lastGpsDutyTick = now;
            printf("PWR: GPS duty cycle sync OK\r\n");
        } else if ((now - lastGpsDutyTick) > GPS_FIX_TIMEOUT_MS) {
            /* Timeout — give up, try again next cycle */
            gpsSetPower(0);
            gpsDutyActive = 0;
            lastGpsDutyTick = now;
            printf("PWR: GPS duty cycle timeout\r\n");
        }
    } else {
        /* GPS is off, check if it's time to wake it */
        if ((now - lastGpsDutyTick) >= (dutySec * 1000)) {
            gpsSetPower(1);
            gpsDutyActive = 1;
            lastGpsDutyTick = now;
        }
    }
}

/* Main schedule check — called every second from CLI task */
static void powerScheduleCheck(void)
{
    /* Skip if not in autonomous mode or in dev mode */
    if (dev.pwr.devMode || !dev.pwr.scheduleActive) return;

    /* Skip if RTC not synced (no reliable time) */
    if (!dev.pwr.rtcSynced) return;

    /* Read current time from RTC */
    uint8_t hh, mm, ss;
    uint8_t day, month;
    uint16_t year;
    rtcGetTime(&hh, &mm, &ss);
    rtcGetDate(&day, &month, &year);
    uint16_t nowMinUTC = (uint16_t)(hh * 60 + mm);

    /* Get lat/lon for solar calc — prefer survey, fallback to live GPS */
    float lat = cfg.surveyLat;
    float lon = cfg.surveyLon;
    if (cfg.surveyCount == 0 && gpsData.valid) {
        lat = gpsData.latitude;
        lon = gpsData.longitude;
    }

    /* Evaluate schedule */
    schedule_result_t sched = schedule_evaluate(&cfg, nowMinUTC,
                                                 day, month, year, lat, lon);

    power_state_t curState = dev.pwr.state;

    if (sched.shouldRecord && curState == PWR_SCHEDULED_NONREC) {
        /* Time to start recording */
        powerEnterRecord();
    } else if (!sched.shouldRecord && curState == PWR_SCHEDULED_REC) {
        /* Recording window ended — stop and sleep */
        powerEnterNonRecord();

        /* Calculate sleep duration, chain if needed (RTC timer max 65535s) */
        uint32_t sleepSec = sched.secsUntilNext;
        if (sleepSec < 10) sleepSec = 10;  /* minimum sleep */

        while (sleepSec > 0 && dev.pwr.state == PWR_SCHEDULED_NONREC) {
            uint32_t chunk = (sleepSec > 65000) ? 65000 : sleepSec;

            wake_source_t ws = enterStop2((uint32_t)chunk);
            printf("\r\nPWR: Woke from Stop 2 (%s)\r\n",
                   ws == WAKE_ESP32 ? "ESP32" : "RTC");

            if (ws == WAKE_ESP32) {
                /* User connected via ESP32 — break out of sleep chain */
                dev.pwr.state = PWR_USER_CONNECTED;
                powerEnterRecord();  /* power everything up */
                return;
            }

            sleepSec -= chunk;

            /* Re-evaluate schedule in case we should start recording now */
            if (sleepSec > 0) {
                rtcGetTime(&hh, &mm, &ss);
                rtcGetDate(&day, &month, &year);
                nowMinUTC = (uint16_t)(hh * 60 + mm);
                sched = schedule_evaluate(&cfg, nowMinUTC,
                                           day, month, year, lat, lon);
                if (sched.shouldRecord) {
                    /* Remount and start recording */
                    powerEnterRecord();
                    return;
                }
            }
        }

        /* If we exited the loop naturally, re-evaluate once more */
        if (dev.pwr.state == PWR_SCHEDULED_NONREC) {
            /* Remount SD for next cycle */
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);
            osDelay(50);
            extern FATFS USERFatFS;
            extern char USERPath[];
            osMutexAcquire(fileMtxHandle, osWaitForever);
            if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
                sdMounted = 1;
            }
            osMutexRelease(fileMtxHandle);
        }
    }

    /* GPS duty cycling during recording */
    if (dev.pwr.state == PWR_SCHEDULED_REC) {
        gpsDutyCycle();
    }
}

/* USER CODE END Application */

