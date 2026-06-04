/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "SEGGER_RTT.h"
#include "fatfs.h"
#include "user_diskio.h"
#include "app_freertos.h"
#include "flac_encoder.h"
#include "device_state.h"
#include "ota_ab.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* Stereo MDF1: Filter0 = Left (falling edge), Filter1 = Right (rising edge) */
MDF_HandleTypeDef MdfHandle0;           /* Left channel */
MDF_HandleTypeDef MdfHandle1;           /* Right channel */
MDF_FilterConfigTypeDef MdfFilterConfig0;
MDF_FilterConfigTypeDef MdfFilterConfig1;
DMA_NodeTypeDef Node_GPDMA1_Channel0;      /* Left DMA linked-list node */
DMA_QListTypeDef List_GPDMA1_Channel0;
DMA_HandleTypeDef handle_GPDMA1_Channel0;
DMA_NodeTypeDef Node_GPDMA1_Channel1;      /* Right DMA linked-list node */
DMA_QListTypeDef List_GPDMA1_Channel1;
DMA_HandleTypeDef handle_GPDMA1_Channel1;

/* Keep old names as aliases for code that references them */
#define AdfHandle0       MdfHandle0
#define AdfFilterConfig0 MdfFilterConfig0

UART_HandleTypeDef husart1;
UART_HandleTypeDef husart3;

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;  /* ESP32-C3 bridge */

ADC_HandleTypeDef hadc1;

RTC_HandleTypeDef hrtc;

I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN PV */
#define AUDIO_BUF_SIZE 1024
#define SAMPLE_RATE    48000

int32_t audioBuffer[AUDIO_BUF_SIZE];      /* Left channel DMA buffer */
int32_t audioBufferR[AUDIO_BUF_SIZE];     /* Right channel DMA buffer */

/* Max hardware SCALE — above this, PDM DC offset causes CIC saturation on some mics.
 * The MDF signal chain is CIC→SCALE→HPF, so SCALE amplifies DC before HPF removes it. */
#define MDF_HW_GAIN_MAX  8   /* 24dB — safe from DC saturation */

/* Conversion buffers: 512 samples each (left and right channels) */
int32_t pcmBuffer[AUDIO_BUF_SIZE / 2];
int32_t pcmBufferR[AUDIO_BUF_SIZE / 2];  /* Right channel processing buffer */

/* PCM ring buffer — written by DMA ISR, read by audio task.
 * Must be in main.c so ISR callbacks can access it directly.
 * Power-of-2 size for fast masking.  16384 samples = 341ms at 48kHz. */
#define PCM_RING_SIZE  16384
#define PCM_RING_MASK  (PCM_RING_SIZE - 1)
int32_t pcmRing[PCM_RING_SIZE];          /* Left channel ring */
volatile uint32_t ringHead = 0;
uint32_t ringTail = 0;

int32_t pcmRingR[PCM_RING_SIZE];        /* Right channel ring */
volatile uint32_t ringHeadR = 0;
uint32_t ringTailR = 0;

/* Channel select: 0=Left (default), 1=Right — swaps which ring the audio task reads */
volatile uint8_t audioChannelRight = 0;

/* FLAC encoder instance (shared with app_freertos.c audio task) */
flac_enc_t flacEncoder;

/* Recording format: 0=FLAC, 1=WAV */
#define REC_FMT_FLAC 0
#define REC_FMT_WAV  1

/* ---- Aliases: old variable names → dev struct fields ---- */
#define ringOverruns   dev.rec.overruns
#define isRecording    dev.rec.active
#define sdMounted      dev.rec.sdMounted
#define audioStarted   dev.rec.audioStarted
#define totalDataBytes dev.rec.dataBytes
#define fileCounter    dev.rec.fileCounter
#define recStartTick   dev.rec.startTick
#define recFilename    dev.rec.filename
#define ppsCount       dev.gps.ppsCount
#define ppsTick        dev.gps.ppsTick
#define ppsUtcTime     dev.gps.ppsUtcTime
#define ppsUtcDate     dev.gps.ppsUtcDate
#define ppsSynced      dev.gps.ppsSynced
#define ppsLatitude    dev.gps.ppsLatitude
#define ppsLongitude   dev.gps.ppsLongitude
#define ppsAltitude    dev.gps.ppsAltitude
#define batteryMv      dev.env.batteryMv
#define sht30TempC100  dev.env.tempC100
#define sht30HumRH100  dev.env.humRH100
#define modelBufSize   dev.det.modelBufSize

/* UART RX queues — fed by RXNE interrupts, consumed by tasks */
osMessageQueueId_t gpsRxQueue;    /* USART1 — GPS */
osMessageQueueId_t consoleRxQueue; /* USART3 — Debug console */

/* Printf mutex — serializes _write() across FreeRTOS tasks */
osMutexId_t printMutex;

/* Recording state (shared with app_freertos.c tasks) */
FIL wavFile;

uint32_t battReadMv(void);

/* Station ID (copied from config by app_freertos.c, used for FLAC metadata) */
char deviceStationId[16] = "QT001";

/* Audio DMA callback tracking for PPS-sample correlation */
volatile uint32_t dmaCallbackCount = 0;
volatile uint32_t dmaCallbackTick = 0;

/* Recording metadata — latched at start, used for GUANO at stop */
static uint32_t recStartTime = 0;
static uint32_t recStartDate = 0;
static float    recStartLat = 0.0f;
static float    recStartLon = 0.0f;
static float    recStartAlt = 0.0f;
static uint8_t  recHasGps = 0;

/* PPS-sample correlation for TDOA */
static uint64_t recStartAbsSample = 0;   /* absolute sample when recording started */
static uint64_t recPpsFirstSample = 0;   /* recording-relative sample at first PPS */
static uint64_t recPpsLastSample = 0;    /* recording-relative sample at last PPS */
static uint32_t recPpsEdgesInRec = 0;    /* PPS edges observed during recording */

/* ---- Inference engine buffers ---- */

/* TFLite model loaded from SD /model/quail_model.tflite */
uint8_t modelBuf[56 * 1024] __attribute__((aligned(16)));

/* TFLite Micro tensor arena */
uint8_t tensorArena[112 * 1024] __attribute__((aligned(16)));

/* Absolute sample counter (monotonically increasing, never reset) */
volatile uint64_t absSampleCount = 0;

/* Set by HAL_GPIO_EXTI_Falling_Callback when an ESP32 CS wake pulse hits PB12.
 * Cleared by enterStop2() before WFI; checked after wake to determine source.
 * Defined later in this file alongside the EXTI callback. */
extern volatile uint8_t espWakePulseSeen;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void SystemPower_Config(void);
void MX_FREERTOS_Init(void);
static void MX_GPIO_Init(void);
static void MX_GPDMA1_Init(void);
static void MX_ICACHE_Init(void);
static void MX_SPI1_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_MDF1_Init(void);
/* USER CODE BEGIN PFP */
static void MX_ADC1_Init(void);
static void MX_RTC_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Survey accessors from app_freertos.c */
extern uint32_t configGetSurveyCount(void);
extern float configGetSurveyLat(void);
extern float configGetSurveyLon(void);
extern float configGetSurveyAlt(void);

/* Command IDs for audio task queue */
#define CMD_START_REC 1
#define CMD_STOP_REC  2

/* Blocking char read with timeout (ms). Returns byte or -1 on timeout.
 * Checks both USART3 (UART console) and RTT (J-Link debugger). */
int getChar(uint32_t timeoutMs)
{
    uint32_t start = HAL_GetTick();
    for (;;) {
        /* Check UART console first */
        uint8_t ch;
        if (osMessageQueueGet(consoleRxQueue, &ch, NULL, 0) == osOK)
            return (int)ch;
        /* Check RTT (returns -1 if no debugger or no key) */
        int c = SEGGER_RTT_GetKey();
        if (c >= 0) return c;
        if (timeoutMs != osWaitForever &&
            (HAL_GetTick() - start) >= timeoutMs)
            return -1;
        osDelay(10);
    }
}

int getCharGps(uint32_t timeoutMs)
{
    uint8_t ch;
    if (osMessageQueueGet(gpsRxQueue, &ch, NULL, timeoutMs) == osOK)
        return (int)ch;
    return -1;
}

void WAV_WriteHeader(FIL *fp, uint32_t sampleRate, uint32_t dataSize)
{
    uint8_t hdr[44];
    uint32_t fileSize = dataSize + 36;
    uint16_t channels = 2;  /* stereo for TDOA */
    uint16_t bitsPerSample = 24;
    uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;

    memcpy(&hdr[0], "RIFF", 4);
    memcpy(&hdr[4], &fileSize, 4);
    memcpy(&hdr[8], "WAVE", 4);

    memcpy(&hdr[12], "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(&hdr[16], &fmtSize, 4);
    uint16_t audioFmt = 1; /* PCM */
    memcpy(&hdr[20], &audioFmt, 2);
    memcpy(&hdr[22], &channels, 2);
    memcpy(&hdr[24], &sampleRate, 4);
    memcpy(&hdr[28], &byteRate, 4);
    memcpy(&hdr[32], &blockAlign, 2);
    memcpy(&hdr[34], &bitsPerSample, 2);

    memcpy(&hdr[36], "data", 4);
    memcpy(&hdr[40], &dataSize, 4);

    UINT bw;
    f_write(fp, hdr, 44, &bw);
}

void writeGuanoChunk(FIL *fp, uint32_t audioDataBytes)
{
    char buf[512];
    int len = 0;

    /* Required: GUANO version (must be first) */
    len += snprintf(buf + len, sizeof(buf) - len, "GUANO|Version: 1.0\n");

    /* GPS-dependent fields */
    if (recHasGps) {
        /* Timestamp: ISO 8601 UTC */
        uint32_t dd = recStartDate / 10000;
        uint32_t mm = (recStartDate / 100) % 100;
        uint32_t yy = recStartDate % 100;
        uint32_t hh = recStartTime / 10000;
        uint32_t mn = (recStartTime / 100) % 100;
        uint32_t ss = recStartTime % 100;
        len += snprintf(buf + len, sizeof(buf) - len,
                        "Timestamp: 20%02lu-%02lu-%02luT%02lu:%02lu:%02luZ\n",
                        (unsigned long)yy, (unsigned long)mm, (unsigned long)dd,
                        (unsigned long)hh, (unsigned long)mn, (unsigned long)ss);

        /* Loc Position: lat lon (decimal degrees, negative for S/W) */
        float lat = recStartLat, lon = recStartLon;
        int latNeg = (lat < 0); if (latNeg) lat = -lat;
        int lonNeg = (lon < 0); if (lonNeg) lon = -lon;
        int32_t lat_d = (int32_t)lat, lon_d = (int32_t)lon;
        int32_t lat_f = (int32_t)((lat - (float)lat_d) * 1000000.0f);
        int32_t lon_f = (int32_t)((lon - (float)lon_d) * 1000000.0f);
        len += snprintf(buf + len, sizeof(buf) - len,
                        "Loc Position: %s%ld.%06ld %s%ld.%06ld\n",
                        latNeg ? "-" : "", (long)lat_d, (long)lat_f,
                        lonNeg ? "-" : "", (long)lon_d, (long)lon_f);

        /* Loc Elevation: altitude in meters */
        {
            float alt = recStartAlt;
            int aN = (alt < 0); if (aN) alt = -alt;
            int32_t a_d = (int32_t)alt;
            int32_t a_f = (int32_t)((alt - (float)a_d) * 10.0f);
            len += snprintf(buf + len, sizeof(buf) - len,
                            "Loc Elevation: %s%ld.%01ld\n",
                            aN ? "-" : "", (long)a_d, (long)a_f);
        }
    }

    /* Device info */
    len += snprintf(buf + len, sizeof(buf) - len, "Make: QuailTracker\n");
    len += snprintf(buf + len, sizeof(buf) - len, "Model: STM32U575-ARU\n");
    len += snprintf(buf + len, sizeof(buf) - len,
                    "Firmware Version: " FW_VERSION "\n");
    len += snprintf(buf + len, sizeof(buf) - len,
                    "Samplerate: %lu\n", (unsigned long)SAMPLE_RATE);

    /* Duration from audio byte count (16-bit mono) */
    uint32_t totalSamples = audioDataBytes / 2;
    uint32_t durSec = totalSamples / SAMPLE_RATE;
    uint32_t durFrac = ((totalSamples % SAMPLE_RATE) * 1000) / SAMPLE_RATE;
    len += snprintf(buf + len, sizeof(buf) - len,
                    "Length: %lu.%03lu\n",
                    (unsigned long)durSec, (unsigned long)durFrac);

    /* Station ID */
    len += snprintf(buf + len, sizeof(buf) - len,
                    "QuailTracker|Station ID: %s\n", deviceStationId);

    /* Mic axis heading for stereo TDOA bearing */
    {
        extern uint16_t configGetMicHeading(void);
        uint16_t hdg = configGetMicHeading();
        if (hdg != 0xFFFF)
            len += snprintf(buf + len, sizeof(buf) - len,
                            "QuailTracker|Mic Heading: %u\n", (unsigned)hdg);
    }

    /* PPS-sample correlation for TDOA */
    if (recPpsEdgesInRec > 0) {
        len += snprintf(buf + len, sizeof(buf) - len,
                        "QuailTracker|PPS Sync Sample: %lu\n",
                        (unsigned long)recPpsFirstSample);
        len += snprintf(buf + len, sizeof(buf) - len,
                        "QuailTracker|PPS Edges: %lu\n",
                        (unsigned long)recPpsEdgesInRec);
        if (recPpsEdgesInRec >= 2) {
            uint32_t intervals = recPpsEdgesInRec - 1;
            uint64_t delta = recPpsLastSample - recPpsFirstSample;
            uint32_t whole = (uint32_t)(delta / intervals);
            uint32_t frac = (uint32_t)((delta % intervals) * 1000 / intervals);
            len += snprintf(buf + len, sizeof(buf) - len,
                            "QuailTracker|PPS Sample Rate: %lu.%03lu\n",
                            (unsigned long)whole, (unsigned long)frac);
        }
    }

    /* Write RIFF sub-chunk: "guan" + size + data [+ pad] */
    uint8_t chunkHdr[8];
    memcpy(chunkHdr, "guan", 4);
    uint32_t chunkSize = (uint32_t)len;
    memcpy(&chunkHdr[4], &chunkSize, 4);

    UINT bw;
    f_write(fp, chunkHdr, 8, &bw);
    f_write(fp, buf, len, &bw);

    /* RIFF chunks must be word-aligned (even byte boundary) */
    if (len & 1) {
        uint8_t pad = 0;
        f_write(fp, &pad, 1, &bw);
    }
}

/* Fixed total size for 2nd FLAC metadata block (PADDING at start, VORBIS_COMMENT at stop).
 * Must not change between start and stop so audio frame offsets stay valid. */
#define FLAC_META2_SIZE 512

/* Write FLAC PADDING metadata block (placeholder, replaced at stop with VORBIS_COMMENT) */
void writeFlacPaddingBlock(FIL *fp)
{
    uint8_t buf[FLAC_META2_SIZE];
    memset(buf, 0, sizeof(buf));
    uint32_t dataLen = FLAC_META2_SIZE - 4;
    buf[0] = 0x81; /* is_last=1, type=1 (PADDING) */
    buf[1] = (uint8_t)(dataLen >> 16);
    buf[2] = (uint8_t)(dataLen >> 8);
    buf[3] = (uint8_t)(dataLen);
    UINT bw;
    f_write(fp, buf, FLAC_META2_SIZE, &bw);
}

/* Write FLAC VORBIS_COMMENT metadata block with all metadata including PPS/TDOA.
 * Output is always exactly FLAC_META2_SIZE bytes (zero-padded).
 * Called at recording stop, when PPS-sample data is available. */
void writeFlacVorbisComment(FIL *fp)
{
    uint8_t buf[FLAC_META2_SIZE];
    memset(buf, 0, sizeof(buf));
    uint32_t pos = 4;  /* skip block header — fill in last */

    /* Vendor string (little-endian length + UTF-8 string) */
    const char *vendor = "QuailTracker " FW_VERSION;
    uint32_t vlen = strlen(vendor);
    buf[pos++] = (uint8_t)(vlen);
    buf[pos++] = (uint8_t)(vlen >> 8);
    buf[pos++] = 0;
    buf[pos++] = 0;
    memcpy(&buf[pos], vendor, vlen);
    pos += vlen;

    /* Build comment strings.
     * Worst case is 13 tags (DATE+LOCATION+STATION_ID+ARTIST+ENCODER+SAMPLERATE
     * +MIC_HEADING+TEMP+HUMIDITY+PPS_SYNC_UTC+PPS_SYNC_SAMPLE+PPS_EDGES+
     * PPS_SAMPLE_RATE). Size with headroom; bump if you add more. */
    char tags[16][80];
    int ntags = 0;

    if (recHasGps) {
        uint32_t dd = recStartDate / 10000;
        uint32_t mm = (recStartDate / 100) % 100;
        uint32_t yy = recStartDate % 100;
        uint32_t hh = recStartTime / 10000;
        uint32_t mn = (recStartTime / 100) % 100;
        uint32_t ss = recStartTime % 100;
        snprintf(tags[ntags++], 80,
                 "DATE=20%02lu-%02lu-%02luT%02lu:%02lu:%02luZ",
                 (unsigned long)yy, (unsigned long)mm, (unsigned long)dd,
                 (unsigned long)hh, (unsigned long)mn, (unsigned long)ss);

        float lat = recStartLat, lon = recStartLon, alt = recStartAlt;
        int latNeg = (lat < 0); if (latNeg) lat = -lat;
        int lonNeg = (lon < 0); if (lonNeg) lon = -lon;
        int altNeg = (alt < 0); if (altNeg) alt = -alt;
        int32_t lat_d = (int32_t)lat, lon_d = (int32_t)lon, alt_d = (int32_t)alt;
        int32_t lat_f = (int32_t)((lat - (float)lat_d) * 1000000.0f);
        int32_t lon_f = (int32_t)((lon - (float)lon_d) * 1000000.0f);
        int32_t alt_f = (int32_t)((alt - (float)alt_d) * 10.0f);
        snprintf(tags[ntags++], 80,
                 "LOCATION=%s%ld.%06ld %s%ld.%06ld %s%ld.%01ld",
                 latNeg ? "-" : "", (long)lat_d, (long)lat_f,
                 lonNeg ? "-" : "", (long)lon_d, (long)lon_f,
                 altNeg ? "-" : "", (long)alt_d, (long)alt_f);
    }

    snprintf(tags[ntags++], 80, "STATION_ID=%s", deviceStationId);
    snprintf(tags[ntags++], 80, "ARTIST=QuailTracker STM32U575-ARU");
    snprintf(tags[ntags++], 80, "ENCODER=QuailTracker " FW_VERSION);
    snprintf(tags[ntags++], 80, "SAMPLERATE=%lu", (unsigned long)SAMPLE_RATE);

    /* Mic axis heading for stereo TDOA bearing */
    {
        extern uint16_t configGetMicHeading(void);
        uint16_t hdg = configGetMicHeading();
        if (hdg != 0xFFFF)
            snprintf(tags[ntags++], 80, "MIC_HEADING=%u", (unsigned)hdg);
    }

    /* Temperature and humidity from SHT30 */
    {
        int32_t tW = sht30TempC100 / 100;
        int32_t tF = sht30TempC100 % 100;
        if (tF < 0) tF = -tF;
        uint32_t hW = sht30HumRH100 / 100;
        uint32_t hF = sht30HumRH100 % 100;
        snprintf(tags[ntags++], 80, "TEMPERATURE=%ld.%02ld", (long)tW, (long)tF);
        snprintf(tags[ntags++], 80, "HUMIDITY=%lu.%02lu",
                 (unsigned long)hW, (unsigned long)hF);
    }

    /* PPS-sample correlation for TDOA */
    if (recPpsEdgesInRec > 0) {
        /* UTC time of first PPS edge = recStartTime + 1 second
         * (recStartTime is the UTC of the PPS edge BEFORE recording started;
         *  the first PPS during recording is the next whole second) */
        if (recHasGps) {
            uint32_t dd = recStartDate / 10000;
            uint32_t mm = (recStartDate / 100) % 100;
            uint32_t yy = recStartDate % 100;
            uint32_t hh = recStartTime / 10000;
            uint32_t mn = (recStartTime / 100) % 100;
            uint32_t ss = (recStartTime % 100) + 1;
            if (ss >= 60) { ss = 0; mn++; }
            if (mn >= 60) { mn = 0; hh++; }
            if (hh >= 24) { hh = 0; } /* date rollover not handled — rare edge case */
            snprintf(tags[ntags++], 80,
                     "PPS_SYNC_UTC=20%02lu-%02lu-%02luT%02lu:%02lu:%02luZ",
                     (unsigned long)yy, (unsigned long)mm, (unsigned long)dd,
                     (unsigned long)hh, (unsigned long)mn, (unsigned long)ss);
        }
        snprintf(tags[ntags++], 80,
                 "PPS_SYNC_SAMPLE=%lu", (unsigned long)recPpsFirstSample);
        snprintf(tags[ntags++], 80,
                 "PPS_EDGES=%lu", (unsigned long)recPpsEdgesInRec);
        if (recPpsEdgesInRec >= 2) {
            /* Measured samples per second from PPS interval counting */
            uint32_t intervals = recPpsEdgesInRec - 1;
            uint64_t totalSamples = recPpsLastSample - recPpsFirstSample;
            uint32_t whole = (uint32_t)(totalSamples / intervals);
            uint32_t frac = (uint32_t)((totalSamples % intervals) * 1000 / intervals);
            snprintf(tags[ntags++], 80,
                     "PPS_SAMPLE_RATE=%lu.%03lu",
                     (unsigned long)whole, (unsigned long)frac);
        }
    }

    /* Comment count (little-endian) */
    buf[pos++] = (uint8_t)(ntags);
    buf[pos++] = (uint8_t)(ntags >> 8);
    buf[pos++] = 0;
    buf[pos++] = 0;

    /* Each comment: LE uint32 length + UTF-8 string */
    for (int i = 0; i < ntags; i++) {
        uint32_t clen = strlen(tags[i]);
        buf[pos++] = (uint8_t)(clen);
        buf[pos++] = (uint8_t)(clen >> 8);
        buf[pos++] = 0;
        buf[pos++] = 0;
        memcpy(&buf[pos], tags[i], clen);
        pos += clen;
    }

    /* Block header: is_last=1, type=4 (VORBIS_COMMENT), length = total - 4 */
    uint32_t dataLen = FLAC_META2_SIZE - 4;
    buf[0] = 0x84;
    buf[1] = (uint8_t)(dataLen >> 16);
    buf[2] = (uint8_t)(dataLen >> 8);
    buf[3] = (uint8_t)(dataLen);

    UINT bw;
    f_write(fp, buf, FLAC_META2_SIZE, &bw);
}

void printMenu(void)
{
    printf("\r\n===== MENU =====\r\n");
    printf("1. Status\r\n");
    printf("2. Start Recording\r\n");
    printf("3. Stop Recording\r\n");
    printf("4. SD Card Info\r\n");
    printf("5. Format SD Card\r\n");
    printf("6. Eject SD Card\r\n");
    printf("7. Mount SD Card\r\n");
    printf("8. GPS Status / Control\r\n");
    printf("9. Comms Status\r\n");
    printf("F. Toggle Format (%s)\r\n", dev.rec.format == REC_FMT_WAV ? "WAV" : "FLAC");
    printf("G. Toggle GPS Raw Output\r\n");
    printf("R. Toggle Recording\r\n");
    printf("S. Toggle GPS Survey-In (%s)\r\n",
           configGetSurveyCount() > 0 ? "has data" : "no data");
    printf("Z. Sleep (Stop 2)\r\n");
    printf("A/D. Toggle Dev Mode (%s)\r\n",
           dev.pwr.devMode ? "ON — always awake"
                           : "OFF — schedule runs when armed");
    {
        extern volatile uint8_t audioChannelRight;
        printf("C. Toggle Audio Channel (%s)\r\n",
               audioChannelRight ? "RIGHT" : "LEFT");
    }
    printf("D. Toggle Dev Mode (%s)\r\n",
           dev.pwr.devMode ? "ON" : "OFF");
    printf("================\r\n");
    {
        static const char *pwr_names[] = {"SLEEP", "REC", "USER", "DEV"};
        printf("[%s|%s] > ",
               isRecording ? "REC" : "IDLE",
               pwr_names[dev.pwr.state & 3]);
    }
    fflush(stdout);
}

void printStatus(void)
{
    printf("\r\n========== STATUS ==========\r\n");

    printf("Audio:\r\n");
    if (audioStarted) {
        printf("  ADF1: Running (Sinc4, FOSR=64)\r\n");
    } else {
        printf("  ADF1: Not started\r\n");
    }
    printf("  Sample Rate: %lu Hz\r\n", (unsigned long)SAMPLE_RATE);
    printf("  DMA Buffer: %d x 32-bit\r\n", AUDIO_BUF_SIZE);
    printf("  Ring Overruns: %lu\r\n", (unsigned long)ringOverruns);

    printf("Recording:\r\n");
    printf("  Format: %s\r\n", dev.rec.format == REC_FMT_WAV ? "WAV" : "FLAC");
    printf("  Active: %s\r\n", isRecording ? "Yes" : "No");
    if (isRecording) {
        uint32_t seconds = totalDataBytes / (SAMPLE_RATE * 6);
        printf("  Duration: %lus\r\n", (unsigned long)seconds);
        printf("  Size: %lu bytes\r\n", (unsigned long)totalDataBytes);
    }

    printf("SD Card:\r\n");
    printf("  Mounted: %s\r\n", sdMounted ? "Yes" : "No");
    if (sdMounted) {
        FATFS *fs;
        DWORD fre_clust;
        if (osMutexAcquire(fileMtxHandle, 200) == osOK) {
            if (f_getfree("", &fre_clust, &fs) == FR_OK) {
                DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
                DWORD fre_sect = fre_clust * fs->csize;
                printf("  Total: %lu KB\r\n", (unsigned long)(tot_sect / 2));
                printf("  Free:  %lu KB\r\n", (unsigned long)(fre_sect / 2));
            }
            osMutexRelease(fileMtxHandle);
        } else {
            printf("  (busy)\r\n");
        }
    }

    printf("Battery:\r\n");
    {
        uint32_t mv = battReadMv();
        printf("  Voltage: %lu.%03lu V\r\n",
               (unsigned long)(mv / 1000), (unsigned long)(mv % 1000));
        int pct = (int)(mv - 3000) * 100 / 1200;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        printf("  Level:   %d%%\r\n", pct);
    }

    printf("Solar:\r\n");
    {
        int chrg = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);  /* active low */
        int done = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1);  /* active low */
        const char *status;
        if (!chrg && done)       status = "Charging";
        else if (chrg && !done)  status = "Complete";
        else if (!chrg && !done) status = "Fault";
        else                     status = "No Solar / Standby";
        printf("  Status: %s (CHRG=%d DONE=%d)\r\n", status, chrg, done);
    }

    printf("Environment:\r\n");
    sht30Read();
    {
        int t = sht30TempC100;
        int sign = (t < 0) ? -1 : 1;
        int whole = t / 100;
        int frac = (t % 100) * sign;
        printf("  Temp:     %d.%02d C\r\n", whole, frac);
    }
    printf("  Humidity: %u.%u%%\r\n",
           (unsigned)(sht30HumRH100 / 100), (unsigned)(sht30HumRH100 / 10 % 10));

    printf("Firmware:\r\n");
    printf("  STM32:  %s\r\n", FW_VERSION);
    printf("  ESP32:  %s\r\n",
           dev.comms.espFwVersion[0] ? dev.comms.espFwVersion : "Unknown");
    printf("Comms:\r\n");
    printf("  ESP32:  %s\r\n", dev.comms.espReady ? "Ready" : "No response");
    printf("  SPI:    %lu transactions\r\n", (unsigned long)dev.comms.spiTransactions);
    {
        static const char *pwr_names[] = {"SLEEP", "REC", "USER", "DEV"};
        printf("  Power:  %s\r\n", pwr_names[dev.pwr.state & 3]);
    }

    printf("============================\r\n");
}

/* Create SD card directory structure and set volume label.
 * Idempotent — f_mkdir returns FR_EXIST if already present,
 * f_setlabel overwrites any existing label. */
void sdCreateDirs(void)
{
    f_mkdir("audio");
    f_mkdir("logs");
    f_mkdir("model");
    f_mkdir("firmware");
    f_setlabel("QTRKR");
}

void startRecording(void)
{
    if (!sdMounted) {
        printf("SD card not mounted!\r\n");
        return;
    }
    if (isRecording) {
        printf("Already recording!\r\n");
        return;
    }

    const char *ext = (dev.rec.format == REC_FMT_WAV) ? "wav" : "flac";
    char fname[48];
    /* Use RTC (kept current by GPS duty-cycle) for the filename rather than
     * the PPS-cached ppsUtcTime/Date — those lag real time when GPS lock is
     * intermittent, which produced wrong-time filenames and same-named
     * chunks. RTC's seconds-resolution is plenty for filenames; PPS-tied
     * sub-ms accuracy still flows into the GUANO metadata below. */
    if (dev.pwr.rtcSynced) {
        uint8_t rtcH, rtcM, rtcS, rtcD, rtcMo;
        uint16_t rtcY;
        rtcGetTime(&rtcH, &rtcM, &rtcS);
        rtcGetDate(&rtcD, &rtcMo, &rtcY);
        snprintf(fname, sizeof(fname), "audio/%04u%02u%02u_%02u%02u%02u_%s.%s",
                 (unsigned int)rtcY, (unsigned int)rtcMo, (unsigned int)rtcD,
                 (unsigned int)rtcH, (unsigned int)rtcM, (unsigned int)rtcS,
                 deviceStationId, ext);
    } else {
        snprintf(fname, sizeof(fname), "audio/rec_%03lu_%s.%s",
                 (unsigned long)fileCounter, deviceStationId, ext);
    }

    /* Latch GPS state for GUANO metadata.
     * Prefer surveyed position (sub-meter accuracy) over instantaneous fix. */
    if (ppsSynced && ppsUtcDate != 0) {
        recStartTime = ppsUtcTime;
        recStartDate = ppsUtcDate;

        if (configGetSurveyCount() > 0) {
            recStartLat = configGetSurveyLat();
            recStartLon = configGetSurveyLon();
            recStartAlt = configGetSurveyAlt();
        } else {
            recStartLat  = ppsLatitude;
            recStartLon  = ppsLongitude;
            recStartAlt  = ppsAltitude;
        }
        recHasGps = 1;
    } else {
        recHasGps = 0;
    }

    /* Latch absolute sample position for PPS-sample correlation (TDOA).
     * Disable PPS EXTI while writing 64-bit values — the ISR reads them
     * and a torn 64-bit write would produce garbage sample positions. */
    HAL_NVIC_DisableIRQ(EXTI8_IRQn);
    __DSB();
    uint32_t elapsed = HAL_GetTick() - dmaCallbackTick;
    if (elapsed > 11) elapsed = 11;
    recStartAbsSample = (uint64_t)dmaCallbackCount * 512 + elapsed * 48;
    recPpsEdgesInRec = 0;
    recPpsFirstSample = 0;
    recPpsLastSample = 0;
    HAL_NVIC_EnableIRQ(EXTI8_IRQn);

    strncpy(recFilename, fname, sizeof(recFilename) - 1);
    recFilename[sizeof(recFilename) - 1] = '\0';

    FRESULT fres = f_open(&wavFile, fname, FA_WRITE | FA_CREATE_ALWAYS);
    if (fres != FR_OK) {
        printf("f_open FAILED: %d\r\n", fres);
        recFilename[0] = '\0';
        return;
    }

    /* Write placeholder header (will be finalized on stop) */
    if (dev.rec.format == REC_FMT_WAV) {
        WAV_WriteHeader(&wavFile, SAMPLE_RATE, 0);
    } else {
        flac_enc_init(&flacEncoder);
        uint8_t hdr[FLAC_HEADER_SIZE];
        flac_enc_write_header(&flacEncoder, hdr);
        hdr[4] &= 0x7F;  /* STREAMINFO is NOT last — SEEKTABLE follows */
        UINT bw;
        f_write(&wavFile, hdr, FLAC_HEADER_SIZE, &bw);

        /* Write placeholder SEEKTABLE (patched at finalization) */
        static uint8_t seekBuf[FLAC_SEEKTABLE_BLOCK_SIZE];
        flac_enc_write_seektable_placeholder(&flacEncoder, seekBuf);
        f_write(&wavFile, seekBuf, FLAC_SEEKTABLE_BLOCK_SIZE, &bw);

        writeFlacPaddingBlock(&wavFile); /* placeholder, replaced at stop */

        /* Mark where audio frames begin (after all metadata blocks) */
        flac_enc_set_audio_start(&flacEncoder, f_tell(&wavFile));
    }
    f_sync(&wavFile);

    totalDataBytes = 0;
    dev.audio.clipCount = 0;

    /* Sync both ring buffers — discard any accumulated data so L and R
     * start from the same point in time.  Without this, the right channel
     * can lag by hundreds of ms due to DMA callback timing differences. */
    ringTail = ringHead;
    ringTailR = ringHeadR;

    isRecording = 1;
    recStartTick = HAL_GetTick();
    fileCounter++;

    /* Update health stats */
    extern void healthUpdateRecStart(const char *filename);
    healthUpdateRecStart(fname);

    printf("Recording to %s...\r\n", fname);
    { extern void diagLog(const char *); char msg[80];
      snprintf(msg, sizeof(msg), "REC start: %s", fname); diagLog(msg); }
}

void stopRecording(void)
{
    if (!isRecording) {
        printf("Not recording!\r\n");
        return;
    }

    isRecording = 0;
    recFilename[0] = '\0';

    if (dev.rec.format == REC_FMT_WAV) {
        /* Append GUANO metadata chunk after audio data */
        writeGuanoChunk(&wavFile, totalDataBytes);

        /* Rewrite WAV header with actual audio data size */
        f_lseek(&wavFile, 0);
        WAV_WriteHeader(&wavFile, SAMPLE_RATE, totalDataBytes);

        /* Fix RIFF container size to include GUANO chunk */
        uint32_t riffSize = f_size(&wavFile) - 8;
        f_lseek(&wavFile, 4);
        UINT bw;
        f_write(&wavFile, &riffSize, 4, &bw);

        f_close(&wavFile);

        uint32_t seconds = totalDataBytes / (SAMPLE_RATE * 6);
        printf("Recording stopped: %lu bytes (%lus)\r\n",
            (unsigned long)totalDataBytes, (unsigned long)seconds);
    } else {
        /* Flush any remaining partial FLAC block */
        uint32_t flushBytes = flac_enc_flush(&flacEncoder);
        if (flushBytes > 0) {
            UINT bw;
            f_write(&wavFile, flacEncoder.outBuf, flushBytes, &bw);
            totalDataBytes += bw;
            flac_enc_notify_write(&flacEncoder, bw);
        }

        /* Rewrite STREAMINFO + SEEKTABLE + VORBIS_COMMENT at file offset 0 */
        f_lseek(&wavFile, 0);
        uint8_t hdr[FLAC_HEADER_SIZE];
        flac_enc_finalize_header(&flacEncoder, hdr);
        hdr[4] &= 0x7F;  /* NOT last — SEEKTABLE follows */
        UINT bw;
        f_write(&wavFile, hdr, FLAC_HEADER_SIZE, &bw);

        /* Finalize SEEKTABLE with real byte offsets */
        static uint8_t seekBuf[FLAC_SEEKTABLE_BLOCK_SIZE];
        flac_enc_finalize_seektable(&flacEncoder, seekBuf);
        f_write(&wavFile, seekBuf, FLAC_SEEKTABLE_BLOCK_SIZE, &bw);

        writeFlacVorbisComment(&wavFile); /* replaces PADDING with real metadata */

        f_close(&wavFile);

        uint32_t seconds = (uint32_t)(flacEncoder.totalSamples / SAMPLE_RATE);
        uint32_t rawSize = (uint32_t)(flacEncoder.totalSamples * FLAC_CHANNELS * (FLAC_BITS_PER_SAMPLE / 8));
        uint32_t ratio = rawSize > 0 ? (totalDataBytes * 100) / rawSize : 0;
        printf("Recording stopped: %lu bytes (%lus, %lu%% of raw)\r\n",
            (unsigned long)totalDataBytes, (unsigned long)seconds,
            (unsigned long)ratio);
    }

    /* Update health stats with completed recording */
    {
        uint32_t secs = (dev.rec.format == REC_FMT_WAV)
            ? totalDataBytes / (SAMPLE_RATE * 6)
            : (uint32_t)(flacEncoder.totalSamples / SAMPLE_RATE);
        extern void healthUpdateRecStop(uint32_t bytes, uint32_t durationSecs);
        healthUpdateRecStop(totalDataBytes, secs);
        extern void diagLog(const char *);
        char msg[80];
        snprintf(msg, sizeof(msg), "REC stop: %lu bytes %lus",
                 (unsigned long)totalDataBytes, (unsigned long)secs);
        diagLog(msg);
    }

    /* Report PPS-sample correlation.
     * Snapshot the 64-bit values with PPS EXTI disabled to avoid torn reads. */
    HAL_NVIC_DisableIRQ(EXTI8_IRQn);
    __DSB();
    uint32_t ppsEdges = recPpsEdgesInRec;
    uint64_t ppsFirst = recPpsFirstSample;
    uint64_t ppsLast  = recPpsLastSample;
    HAL_NVIC_EnableIRQ(EXTI8_IRQn);

    if (ppsEdges >= 2) {
        uint32_t intervals = ppsEdges - 1;
        uint64_t delta = ppsLast - ppsFirst;
        uint32_t whole = (uint32_t)(delta / intervals);
        uint32_t frac = (uint32_t)((delta % intervals) * 1000 / intervals);
        printf("PPS: %lu edges, measured rate %lu.%03lu Hz\r\n",
            (unsigned long)ppsEdges,
            (unsigned long)whole, (unsigned long)frac);
    } else if (ppsEdges == 1) {
        printf("PPS: 1 edge (need 2+ for rate measurement)\r\n");
    } else {
        printf("PPS: no edges during recording\r\n");
    }
}

/* Split recording into a new file (chunk boundary).
 * Finalizes the current file and starts a new one seamlessly.
 * The ring buffer keeps filling during the swap so no audio is lost. */
void chunkRecording(void)
{
    if (!isRecording) return;

    uint32_t seconds = 0;
    if (dev.rec.format == REC_FMT_WAV) {
        seconds = totalDataBytes / (SAMPLE_RATE * 6);
    } else {
        seconds = (uint32_t)(flacEncoder.totalSamples / SAMPLE_RATE);
    }

    printf("Chunk: closing after %lus, starting new file...\r\n",
           (unsigned long)seconds);

    /* Temporarily clear isRecording so stopRecording doesn't reset filename
     * used for logging, but we still need to finalize the file. */
    stopRecording();

    /* Immediately start a new recording with a fresh timestamp */
    startRecording();
}

int formatSD(void)
{
    extern FATFS USERFatFS;
    extern char USERPath[];

    printf("Formatting SD card (FAT32)...\r\n");

    if (sdMounted) {
        f_mount(NULL, USERPath, 0);
        sdMounted = 0;
    }

    FRESULT fres = f_mount(&USERFatFS, USERPath, 0);
    if (fres != FR_OK) {
        printf("f_mount failed: %d\r\n", fres);
        return 0;
    }

    static uint8_t workBuf[512];
    fres = f_mkfs(USERPath, FM_FAT32, 0, workBuf, sizeof(workBuf));
    if (fres != FR_OK) {
        printf("f_mkfs failed: %d\r\n", fres);
        f_mount(NULL, USERPath, 0);
        return 0;
    }

    f_mount(NULL, USERPath, 0);
    fres = f_mount(&USERFatFS, USERPath, 1);
    if (fres != FR_OK) {
        printf("Mount after format failed: %d\r\n", fres);
        return 0;
    }

    sdMounted = 1;
    sdCreateDirs();
    printf("Format complete, SD card ready\r\n");
    return 1;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* Earliest possible LED blink — before any clock/peripheral init.
   * Running on default MSI 4MHz here. Proves MCU boots. */
  {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Pin = GPIO_PIN_13;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &gi);
    for (int i = 0; i < 5; i++) {
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);
      HAL_Delay(200);
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
      HAL_Delay(200);
    }
  }
  /* USER CODE END Init */

  /* Configure the System Power */
  SystemPower_Config();
  HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13); HAL_Delay(300); /* blink 1: power ok */

  /* Configure the system clock */
  SystemClock_Config();
  HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13); HAL_Delay(300); /* blink 2: clock ok */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13); HAL_Delay(300); /* blink 3: gpio ok */
  MX_GPDMA1_Init();
  HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13); HAL_Delay(300); /* blink 4: dma ok */
  MX_ICACHE_Init();
  HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13); HAL_Delay(300); /* blink 5: icache ok */
  MX_SPI1_Init();
  MX_SPI2_Init();
  HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13); HAL_Delay(300); /* blink 6: spi ok */
  MX_USART1_UART_Init();
  HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13); HAL_Delay(300); /* blink 7: usart1 ok */
  MX_USART3_UART_Init();
  MX_MDF1_Init();
  HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13); HAL_Delay(300); /* blink 9: adf ok */
  /* USER CODE BEGIN 2 */
  /* Quick LED heartbeat — 3 blinks to confirm all inits passed */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
  HAL_Delay(200);
  for (int i = 0; i < 3; i++) {
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_Delay(100);
  }

  /* Explicit RTT init + test write before first printf */
  SEGGER_RTT_Init();
  SEGGER_RTT_WriteString(0, "\r\n[RTT OK]\r\n");

  setvbuf(stdout, NULL, _IONBF, 0);

  /* SPI2 ping-pong test with ESP32-C3 bridge */
  {
      uint8_t tx[64] = {0}, rx[64] = {0};
      memcpy(tx, "PING", 4);
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);  /* CS low */
      HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(&hspi2, tx, rx, 64, 1000);
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET);    /* CS high */
      printf("SPI2 test: %s (RX: \"%.*s\")\r\n",
             s == HAL_OK ? "OK" : "FAIL", 8, (char *)rx);
  }

  printf("\r\n\r\n");
  printf("================================================\r\n");
  printf("  QuailTracker U575 - PDM Audio Recorder\r\n");
  printf("  STM32U575  v%s  [FreeRTOS]\r\n", FW_VERSION);
  printf("  SYSCLK: %lu MHz\r\n",
         (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000UL));
  printf("================================================\r\n");

  /* ADC1 — battery voltage on PC0 / IN1 */
  MX_ADC1_Init();

  /* RTC — for Stop 2 wake-up timer */
  MX_RTC_Init();

  /* A/B OTA confirm/rollback: as early as possible after the RTC (backup
   * registers) is up. If a freshly-swapped image is on trial and has run out
   * of boot attempts without checking in, this rolls back to the previous bank
   * (resets — does not return). */
  ota_ab_boot_check();

  /* I2C1 — SHT30 temperature/humidity sensor (PB6/PB7) */
  MX_I2C1_Init();
  sht30Read();  /* initial reading */

  /* Start MDF1 stereo DMA acquisition before scheduler */
  {
    MdfFilterConfig0.DecimationRatio = 64;
    MdfFilterConfig0.Gain = 6;
    MdfFilterConfig1.DecimationRatio = 64;
    MdfFilterConfig1.Gain = 6;

    MDF_DmaConfigTypeDef dmaConfigL = {0};
    dmaConfigL.Address = (uint32_t)audioBuffer;
    dmaConfigL.DataLength = AUDIO_BUF_SIZE * 4;
    dmaConfigL.MsbOnly = DISABLE;

    MDF_DmaConfigTypeDef dmaConfigR = {0};
    dmaConfigR.Address = (uint32_t)audioBufferR;
    dmaConfigR.DataLength = AUDIO_BUF_SIZE * 4;
    dmaConfigR.MsbOnly = DISABLE;

    printf("MDF1: ");
    uint8_t ok = 1;
    /* Arm both filters — they wait for TRGO in SYNC mode */
    if (HAL_MDF_AcqStart_DMA(&MdfHandle0, &MdfFilterConfig0, &dmaConfigL) != HAL_OK) {
        printf("L-FAILED ");
        ok = 0;
    }
    if (HAL_MDF_AcqStart_DMA(&MdfHandle1, &MdfFilterConfig1, &dmaConfigR) != HAL_OK) {
        printf("R-FAILED ");
        ok = 0;
    }
    if (ok) {
        /* Fire TRGO — both filters start simultaneously */
        HAL_MDF_GenerateTrgo(&MdfHandle0);
        audioStarted = 1;
        printf("OK stereo SYNC (48kHz, Sinc4, gain=%d)\r\n", (int)MdfFilterConfig0.Gain);
    } else {
        printf("\r\n");
    }
  }


  /* Init FatFS and mount SD card */
  MX_FATFS_Init();

  extern FATFS USERFatFS;
  extern char USERPath[];
  printf("SD Card: ");
  if (HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin) != GPIO_PIN_RESET) {
      printf("No card detected\r\n");
  } else if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
      sdMounted = 1;
      sdCreateDirs();
      printf("Mounted\r\n");
  } else {
      printf("Not readable, formatting...\r\n");
      if (formatSD()) {
          printf("SD Card: Ready\r\n");
      } else {
          printf("SD Card: Mount failed\r\n");
      }
  }
  /* Create UART RX queues (must exist before RXNE interrupts are enabled) */
  gpsRxQueue = osMessageQueueNew(128, sizeof(uint8_t), NULL);
  consoleRxQueue = osMessageQueueNew(128, sizeof(uint8_t), NULL);

  /* Enable RXNE interrupts — ISRs push bytes into queues above.
   * Priority 6 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5), safe for FreeRTOS API. */
  __HAL_UART_ENABLE_IT(&husart1, UART_IT_RXNE);
  HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);



  __HAL_UART_ENABLE_IT(&husart3, UART_IT_RXNE);
  HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* Printf mutex must be created after osKernelInitialize so FreeRTOS heap exists.
   * Before this point, _write() sees printMutex==NULL and skips locking (safe: single-threaded). */
  printMutex = osMutexNew(NULL);

  /* Call init function for freertos objects (in app_freertos.c) */
  MX_FREERTOS_Init();

  /* Initialize SystemView before starting scheduler */
  extern void SEGGER_SYSVIEW_Conf(void);
  SEGGER_SYSVIEW_Conf();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* Unreachable — osKernelStart() never returns. Tasks run in app_freertos.c */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE
                              |RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON_RTC_ONLY;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_4;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 80;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_0;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Power Configuration
  * @retval None
  */
static void SystemPower_Config(void)
{

  /*
   * Disable the internal Pull-Up in Dead Battery pins of UCPD peripheral
   */
  HAL_PWREx_DisableUCPDDeadBattery();

  /*
   * Nucleo board uses SMPS variant — production PCB uses LDO variant.
   * LDO is the default after reset, no ConfigSupply call needed.
   */
/* USER CODE BEGIN PWR */
/* USER CODE END PWR */
}

/**
  * @brief ADF1 Initialization Function
  * @param None
  * @retval None
  */
/* MDF1 Stereo Init — two filters on same SDI0, opposite clock edges.
 * Filter0 = Left mic  (PCB silkscreen "Left",  L/R sense = GND, rising edge)
 * Filter1 = Right mic (PCB silkscreen "Right", L/R sense = VDD, falling edge)
 * PE9 = MDF1_CCK0 (clock), PD3 = MDF1_SDI0 (data), both AF6.
 * NOTE: edge-to-channel mapping matches the PCB silkscreen (validated on
 * the bench), not the IM72D128 datasheet's nominal L/R convention. */
static void MX_MDF1_Init(void)
{
    /* Filter0 — Left channel (rising edge, GND-side mic) */
    MdfHandle0.Instance = MDF1_Filter0;
    MdfHandle0.Init.CommonParam.ProcClockDivider = 52;  /* 160MHz/52 = 3.077MHz PDM clock */
    MdfHandle0.Init.CommonParam.OutputClock.Activation = ENABLE;
    MdfHandle0.Init.CommonParam.OutputClock.Pins = MDF_OUTPUT_CLOCK_0;
    MdfHandle0.Init.CommonParam.OutputClock.Divider = 1;
    MdfHandle0.Init.CommonParam.OutputClock.Trigger.Activation = ENABLE;
    MdfHandle0.Init.CommonParam.OutputClock.Trigger.Source = MDF_CLOCK_TRIG_TRGO;
    MdfHandle0.Init.CommonParam.OutputClock.Trigger.Edge = MDF_CLOCK_TRIG_RISING_EDGE;
    MdfHandle0.Init.SerialInterface.Activation = ENABLE;
    MdfHandle0.Init.SerialInterface.Mode = MDF_SITF_LF_MASTER_SPI_MODE;
    MdfHandle0.Init.SerialInterface.ClockSource = MDF_SITF_CCK0_SOURCE;
    MdfHandle0.Init.SerialInterface.Threshold = 4;
    MdfHandle0.Init.FilterBistream = MDF_BITSTREAM0_RISING;
    if (HAL_MDF_Init(&MdfHandle0) != HAL_OK) {
        printf("MDF1 Filter0 (L): Init FAILED\r\n");
        return;
    }

    /* Filter1 — Right channel (falling edge, VDD-side mic).
     * Serial interface disabled — Filter1 reads from SITF0 via bitstream mixer.
     * Only Filter0 owns the serial interface and output clock. */
    MdfHandle1.Instance = MDF1_Filter1;
    MdfHandle1.Init.CommonParam.ProcClockDivider = 52;
    MdfHandle1.Init.CommonParam.OutputClock.Activation = DISABLE;
    MdfHandle1.Init.CommonParam.OutputClock.Pins = MDF_OUTPUT_CLOCK_0;
    MdfHandle1.Init.CommonParam.OutputClock.Divider = 1;
    MdfHandle1.Init.CommonParam.OutputClock.Trigger.Activation = DISABLE;
    MdfHandle1.Init.SerialInterface.Activation = DISABLE;
    MdfHandle1.Init.SerialInterface.Mode = MDF_SITF_LF_MASTER_SPI_MODE;
    MdfHandle1.Init.SerialInterface.ClockSource = MDF_SITF_CCK0_SOURCE;
    MdfHandle1.Init.SerialInterface.Threshold = 4;
    MdfHandle1.Init.FilterBistream = MDF_BITSTREAM0_FALLING;
    if (HAL_MDF_Init(&MdfHandle1) != HAL_OK) {
        printf("MDF1 Filter1 (R): Init FAILED\r\n");
        return;
    }

    /* Filter configurations — identical for both channels, SYNC mode.
     * Both filters wait for TRGO trigger, ensuring sample-aligned start. */
    MdfFilterConfig0.DataSource = MDF_DATA_SOURCE_BSMX;
    MdfFilterConfig0.Delay = 0;
    MdfFilterConfig0.CicMode = MDF_ONE_FILTER_SINC4;
    MdfFilterConfig0.DecimationRatio = 64;
    MdfFilterConfig0.Gain = 0;
    MdfFilterConfig0.ReshapeFilter.Activation = DISABLE;
    MdfFilterConfig0.HighPassFilter.Activation = ENABLE;
    MdfFilterConfig0.HighPassFilter.CutOffFrequency = MDF_HPF_CUTOFF_0_000625FPCM;
    MdfFilterConfig0.SoundActivity.Activation = DISABLE;
    MdfFilterConfig0.AcquisitionMode = MDF_MODE_SYNC_CONT;
    MdfFilterConfig0.Trigger.Source = MDF_FILTER_TRIG_TRGO;
    MdfFilterConfig0.Trigger.Edge = MDF_FILTER_TRIG_RISING_EDGE;
    MdfFilterConfig0.FifoThreshold = MDF_FIFO_THRESHOLD_NOT_EMPTY;
    MdfFilterConfig0.DiscardSamples = 0;

    /* Copy same config for right channel */
    memcpy(&MdfFilterConfig1, &MdfFilterConfig0, sizeof(MdfFilterConfig1));

    printf("MDF1: Stereo init OK (L=rising, R=falling, PE9/PD3, SYNC)\r\n");
}

/**
  * @brief GPDMA1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPDMA1_Init(void)
{

  /* USER CODE BEGIN GPDMA1_Init 0 */

  /* USER CODE END GPDMA1_Init 0 */

  /* Peripheral clock enable */
  __HAL_RCC_GPDMA1_CLK_ENABLE();

  /* GPDMA1 interrupt Init */
    HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);
    HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);

  /* USER CODE BEGIN GPDMA1_Init 1 */

  /* USER CODE END GPDMA1_Init 1 */
  /* USER CODE BEGIN GPDMA1_Init 2 */

  /* USER CODE END GPDMA1_Init 2 */

}

/**
  * @brief ICACHE Initialization Function
  * @param None
  * @retval None
  */
static void MX_ICACHE_Init(void)
{

  /* USER CODE BEGIN ICACHE_Init 0 */

  /* USER CODE END ICACHE_Init 0 */

  /* USER CODE BEGIN ICACHE_Init 1 */

  /* USER CODE END ICACHE_Init 1 */

  /** Enable instruction cache in 1-way (direct mapped cache)
  */
  if (HAL_ICACHE_ConfigAssociativityMode(ICACHE_1WAY) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ICACHE_Enable() != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ICACHE_Init 2 */

  /* USER CODE END ICACHE_Init 2 */

}

/**
  * @brief USART1 Initialization Function — GPS (ATGM336H, 9600 baud, PA9/PA10)
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  husart1.Instance = USART1;
  husart1.Init.BaudRate = 9600;
  husart1.Init.WordLength = UART_WORDLENGTH_8B;
  husart1.Init.StopBits = UART_STOPBITS_1;
  husart1.Init.Parity = UART_PARITY_NONE;
  husart1.Init.Mode = UART_MODE_TX_RX;
  husart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  husart1.Init.OverSampling = UART_OVERSAMPLING_16;
  husart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  husart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  husart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&husart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART3 Initialization Function — Debug console (115200 baud, PD8/PD9)
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{
  husart3.Instance = USART3;
  husart3.Init.BaudRate = 115200;
  husart3.Init.WordLength = UART_WORDLENGTH_8B;
  husart3.Init.StopBits = UART_STOPBITS_1;
  husart3.Init.Parity = UART_PARITY_NONE;
  husart3.Init.Mode = UART_MODE_TX_RX;
  husart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  husart3.Init.OverSampling = UART_OVERSAMPLING_16;
  husart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  husart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  husart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&husart3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  SPI_AutonomousModeConfTypeDef HAL_SPI_AutonomousMode_Cfg_Struct = {0};

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x7;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi1.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi1.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_SPI_AutonomousMode_Cfg_Struct.TriggerState = SPI_AUTO_MODE_DISABLE;
  HAL_SPI_AutonomousMode_Cfg_Struct.TriggerSelection = SPI_GRP1_GPDMA_CH0_TCF_TRG;
  HAL_SPI_AutonomousMode_Cfg_Struct.TriggerPolarity = SPI_TRIG_POLARITY_RISING;
  if (HAL_SPIEx_SetConfigAutonomousMode(&hspi1, &HAL_SPI_AutonomousMode_Cfg_Struct) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Init — ESP32-C3 bridge
  * PB13=SCK, PB15=MOSI, PB14=MISO, PB12=CS (GPIO software)
  * These pins match the STM32U575 ROM bootloader SPI2 pinout (AN2606).
  */
static void MX_SPI2_Init(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;

    /* PB13 = SPI2_SCK */
    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PB14 = SPI2_MISO */
    gpio.Pin = GPIO_PIN_14;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PB15 = SPI2_MOSI */
    gpio.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PB12 = CS (GPIO output, start high/deselected) */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Alternate = 0;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* SPI2: master, 8-bit, mode 0, 5 MHz (160MHz/32) */
    hspi2.Instance = SPI2;
    hspi2.Init.Mode = SPI_MODE_MASTER;
    hspi2.Init.Direction = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi2.Init.NSS = SPI_NSS_SOFT;
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    hspi2.Init.IOSwap = SPI_IO_SWAP_DISABLE;
    hspi2.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
    hspi2.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
    if (HAL_SPI_Init(&hspi2) != HAL_OK)
        Error_Handler();
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* PD13 — Status LED (active high) */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* PD11 — PERIPH_VCC EN (active high, SD+SHT30 on at boot) */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* PB0 — Solar CHRG status (active low, open drain + 10k pull-up) */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;  /* external 10k pull-up */
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* PB1 — Solar DONE status (active low, open drain + 10k pull-up) */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;  /* external 10k pull-up */
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* PD12 — GPS_VCC EN (active high, GPS on at boot) */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* PD14 — GPS WAKE (high = running, pulse low to wake from backup) */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* PD15 — GPS nRESET (active low, not in reset) */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* PA8 — GPS PPS input, rising-edge EXTI */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_NVIC_SetPriority(EXTI8_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI8_IRQn);

  /* PC4 — SD card detect (active low: low = inserted, pull-up for no card) */
  GPIO_InitStruct.Pin = SD_CD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SD_CD_GPIO_Port, &GPIO_InitStruct);
  HAL_NVIC_SetPriority(EXTI4_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* ADC1 init — CubeMX-generated config for PC0/IN1 battery voltage */
static uint32_t vddaMv = 3300;  /* Measured VDDA — updated at init via VREFINT */

/* Read a single ADC channel (helper) */
static uint32_t adcReadRaw(uint32_t channel, uint32_t sampTime)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = sampTime;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) return 0;
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return 0;
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return 0;
    }
    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return raw;
}

static void MX_ADC1_Init(void)
{
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_14B;
    hadc1.Init.GainCompensation = 0;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
    hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
    hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        printf("ADC1: Init FAILED\r\n");
        return;
    }

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        printf("ADC1: Calibration FAILED\r\n");
    }

    /* Measure actual VDDA using factory-calibrated VREFINT (14-bit). */
    uint32_t vref_raw = adcReadRaw(ADC_CHANNEL_VREFINT, ADC_SAMPLETIME_391CYCLES);
    if (vref_raw > 0) {
        uint16_t vrefCal = *VREFINT_CAL_ADDR;
        vddaMv = (VREFINT_CAL_VREF * (uint32_t)vrefCal) / vref_raw;
        printf("ADC1: VDDA=%lu mV (VREFINT raw=%lu, cal=%u)\r\n",
               (unsigned long)vddaMv, (unsigned long)vref_raw, vrefCal);
    }

    printf("ADC1: OK\r\n");
}

/* Read battery voltage via ADC1 CH1 (PC0).
 * 1M/1M divider: VBAT = ADC_mV * 2.  Returns millivolts.
 *
 * Sample time must be long: 500 kΩ Thevenin source from the divider sees
 * τ ≈ 2.5 µs into the ADC's S&H cap, so 68 cycles (1.7 µs at 40 MHz ADC
 * clock) doesn't let the cap settle and the reading comes in ~10% low.
 * 814 cycles (20.4 µs, ~8τ) settles to within 14-bit accuracy. */
uint32_t battReadMv(void)
{
    uint32_t raw = adcReadRaw(ADC_CHANNEL_1, ADC_SAMPLETIME_814CYCLES);
    if (raw > 0)
        batteryMv = (raw * vddaMv * 2) / 16383;
    return batteryMv;
}

/* ---- I2C1 init (SHT30 on PB6/PB7) ---- */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x30909DEC;  /* 100 kHz @ 160 MHz (from CubeMX) */
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        printf("I2C1: Init FAILED\r\n");
        return;
    }
    /* Enable analog filter, disable digital filter */
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLED) != HAL_OK) {
        printf("I2C1: Filter FAILED\r\n");
        return;
    }
    printf("I2C1: OK (100kHz, SHT30 @ 0x44)\r\n");
}

/* ---- SHT30 CRC-8 (poly 0x31, init 0xFF) ---- */
static uint8_t sht30Crc(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
    }
    return crc;
}

/* Read SHT30 single-shot, high repeatability, no clock stretch.
 * Updates sht30TempC100 and sht30HumRH100.  Silently keeps old values on error. */
void sht30Read(void)
{
    uint8_t cmd[2] = { 0x24, 0x00 };
    if (HAL_I2C_Master_Transmit(&hi2c1, 0x44 << 1, cmd, 2, 100) != HAL_OK)
        return;

    HAL_Delay(16);  /* 15 ms max for high repeatability */

    uint8_t rx[6];
    if (HAL_I2C_Master_Receive(&hi2c1, 0x44 << 1, rx, 6, 100) != HAL_OK)
        return;

    /* Verify CRC on both words */
    if (sht30Crc(rx, 2) != rx[2] || sht30Crc(rx + 3, 2) != rx[5])
        return;

    uint16_t rawT = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t rawH = ((uint16_t)rx[3] << 8) | rx[4];

    /* temp = -45 + 175 * rawT / 65535  →  in 0.01 °C units */
    sht30TempC100 = (int16_t)(-4500 + (int32_t)17500 * rawT / 65535);
    /* hum = 100 * rawH / 65535  →  in 0.01 %RH units */
    sht30HumRH100 = (uint16_t)((uint32_t)10000 * rawH / 65535);
}

static void MX_RTC_Init(void)
{
    RTC_PrivilegeStateTypeDef privilegeState = {0};

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 255;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    hrtc.Init.OutPutPullUp = RTC_OUTPUT_PULLUP_NONE;
    hrtc.Init.BinMode = RTC_BINARY_NONE;
    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        printf("RTC: Init FAILED\r\n");
        return;
    }

    privilegeState.rtcPrivilegeFull = RTC_PRIVILEGE_FULL_NO;
    privilegeState.backupRegisterPrivZone = RTC_PRIVILEGE_BKUP_ZONE_NONE;
    privilegeState.backupRegisterStartZone2 = RTC_BKP_DR0;
    privilegeState.backupRegisterStartZone3 = RTC_BKP_DR0;
    if (HAL_RTCEx_PrivilegeModeSet(&hrtc, &privilegeState) != HAL_OK) {
        printf("RTC: Privilege FAILED\r\n");
        return;
    }

    /* Initial wake-up timer config (overridden at runtime by enterStop2) */
    if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 0, RTC_WAKEUPCLOCK_RTCCLK_DIV16, 0) != HAL_OK) {
        printf("RTC: WakeUp FAILED\r\n");
        return;
    }

    printf("RTC: OK (LSE 32.768kHz)\r\n");
}

/* ---- RTC time sync from GPS ---- */
void rtcSyncFromGps(void)
{
    extern device_state_t dev;
    gps_data_t gps = dev.gps.fix;
    if (!gps.valid || gps.utc_date == 0) return;

    /* Parse HHMMSS → hours/minutes/seconds */
    RTC_TimeTypeDef sTime = {0};
    sTime.Hours   = (uint8_t)(gps.utc_time / 10000);
    sTime.Minutes = (uint8_t)((gps.utc_time / 100) % 100);
    sTime.Seconds = (uint8_t)(gps.utc_time % 100);
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;

    /* Parse DDMMYY → day/month/year */
    RTC_DateTypeDef sDate = {0};
    sDate.Date  = (uint8_t)(gps.utc_date / 10000);
    sDate.Month = (uint8_t)((gps.utc_date / 100) % 100);
    sDate.Year  = (uint8_t)(gps.utc_date % 100);  /* 2-digit year */

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    dev.pwr.rtcSynced = 1;
    dev.pwr.lastGpsSyncTick = HAL_GetTick();
}

/* ---- RTC read helpers ---- */
void rtcGetTime(uint8_t *hours, uint8_t *minutes, uint8_t *seconds)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};  /* must read date after time per RM */
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    *hours   = sTime.Hours;
    *minutes = sTime.Minutes;
    *seconds = sTime.Seconds;
}

void rtcGetDate(uint8_t *day, uint8_t *month, uint16_t *year)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    *day   = sDate.Date;
    *month = sDate.Month;
    *year  = 2000 + (uint16_t)sDate.Year;
}

wake_source_t enterStop2(uint32_t seconds)
{
    if (seconds == 0 || seconds > 65535) return WAKE_RTC;

    uint8_t wasAudioStarted = audioStarted;

    /* Stop MDF1 stereo DMA if running */
    if (audioStarted) {
        HAL_MDF_AcqStop_DMA(&MdfHandle0);
        HAL_MDF_AcqStop_DMA(&MdfHandle1);
        audioStarted = 0;
    }

    /* Disable UART RXNE interrupts and clear error flags.
     * Switching pins to analog disconnects USART RX, which can set ORE/FE.
     * On STM32U5, RXNEIE also enables ORE interrupt — a set ORE would
     * re-assert the USART NVIC line immediately after any pending clear,
     * causing WFI to return instantly.  Clear errors + drain RDR first. */
    __HAL_UART_DISABLE_IT(&husart1, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&husart3, UART_IT_RXNE);
    USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    (void)USART1->RDR;  /* drain stale RXNE */
    (void)USART3->RDR;

    /* Set all peripheral-facing GPIOs to analog (hi-Z) to prevent
     * back-powering unpowered modules through ESD protection diodes.
     * MODER = 0b11 per pin = analog mode (highest impedance). */
    uint32_t moder_a = GPIOA->MODER;
    uint32_t moder_b = GPIOB->MODER;
    uint32_t moder_d = GPIOD->MODER;
    GPIOA->MODER |= (0x3u << (4*2))   /* PA4  SD_CS               */
                   | (0x3u << (5*2))   /* PA5  SPI1_SCK  (SD CLK)  */
                   | (0x3u << (6*2))   /* PA6  SPI1_MISO (SD MISO) */
                   | (0x3u << (7*2))   /* PA7  SPI1_MOSI (SD MOSI) */
                   | (0x3u << (9*2))   /* PA9  USART1_TX (GPS RX)  */
                   | (0x3u << (10*2)); /* PA10 USART1_RX (GPS TX)  */
    GPIOB->MODER |= (0x3u << (6*2))   /* PB6  I2C1_SCL  (SHT30)  */
                   | (0x3u << (7*2));  /* PB7  I2C1_SDA  (SHT30)  */
    GPIOD->MODER |= (0x3u << (8*2))   /* PD8  USART3_TX (console) */
                   | (0x3u << (9*2));  /* PD9  USART3_RX (console) */
    /* Drive GPS control pins LOW to prevent ESD back-powering SW_VCC.
     * Keep as outputs (don't change MODER) — floating PD12 turns on
     * the power switch, floating PD14 could wake GPS from backup. */
    uint32_t odr_d = GPIOD->ODR;
    GPIOD->BSRR = (1u << (11+16))   /* PD11 PERIPH_VCC EN → LOW */
                 | (1u << (12+16))   /* PD12 GPS_VCC EN    → LOW */
                 | (1u << (14+16))   /* PD14 GPS_WAKE      → LOW */
                 | (1u << (15+16));  /* PD15 GPS_nRESET    → LOW */

    /* --- Phase 2: Configure PB12 (SPI2 CS) as EXTI input for ESP32 wake ---
     * Save PB12 MODER, then reconfigure as input with pull-up.
     * ESP32 pulls PB12 LOW for 10ms to wake STM32. */
    uint32_t pb12_moder_save = GPIOB->MODER & (0x3u << (12*2));
    uint32_t pb12_pupdr_save = GPIOB->PUPDR & (0x3u << (12*2));
    GPIOB->MODER &= ~(0x3u << (12*2));        /* PB12 = input */
    GPIOB->PUPDR &= ~(0x3u << (12*2));
    GPIOB->PUPDR |=  (0x1u << (12*2));        /* PB12 = pull-up */

    /* Mux EXTI line 12 to port B (default is port A → PA12, which is
     * floating and never transitions). On STM32U5, EXTI->EXTICR[3]
     * holds lines 12-15, 8 bits per line. Port B = 0x01.
     * Without this mux, the wake pulse on PB12 is invisible to EXTI. */
    uint32_t exticr3_save = EXTI->EXTICR[3];
    EXTI->EXTICR[3] = (exticr3_save & ~0xFFu) | 0x01u;

    /* Enable EXTI12 falling edge (PB12) for ESP32 wake */
    EXTI->FTSR1 |= EXTI_FTSR1_FT12;          /* falling edge trigger */
    EXTI->IMR1  |= EXTI_IMR1_IM12;            /* unmask EXTI line 12 */

    /* Force the heartbeat LED OFF before sleep so it's a clean indicator:
     * dark = sleeping, lit = awake/faulted. Otherwise it freezes wherever
     * the 1Hz toggle landed and ON-during-sleep looks identical to a
     * stuck-on-fault. */
    GPIOD->BSRR = (1u << (13 + 16));

    /* Disable SysTick + HAL timebase (TIM17) to stop periodic ticks */
    SysTick->CTRL &= ~(SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk);
    HAL_SuspendTick();

    /* Deactivate previous wake-up timer, then reconfigure for requested seconds.
     * Use WP disable/enable around HAL calls to work around STM32U5 HAL v1.8.0
     * which doesn't manage write protection in SetWakeUpTimer_IT. */
    __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, seconds - 1,
                                 RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
    __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);

    /* EXTI line 19 = RTC wake-up timer (configurable event on U5) */
    EXTI->RTSR1 |= EXTI_RTSR1_RT19;
    EXTI->IMR1  |= EXTI_IMR1_IM19;

    /* Save all NVIC interrupt enables, then disable everything except
     * the RTC IRQ (EXTI line 19) and EXTI0 (ESP32 CS wake).
     * WFI only wakes on enabled+pending interrupts. */
    uint32_t nvic_iser_save[8];
    for (uint32_t i = 0; i < 8u; i++) {
        nvic_iser_save[i] = NVIC->ISER[i];
        NVIC->ICER[i] = 0xFFFFFFFFu;       /* disable all IRQs */
    }
    NVIC_EnableIRQ(RTC_IRQn);               /* enable RTC wake */
    NVIC_EnableIRQ(EXTI12_IRQn);            /* enable ESP32 CS wake */

    /* Clear ALL pending: SysTick, PendSV, NVIC, and EXTI (both edges) */
    SCB->ICSR  = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    for (uint32_t i = 0; i < 8u; i++)
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    EXTI->RPR1 = 0xFFFFFFFFu;              /* clear all rising pending  */
    EXTI->FPR1 = 0xFFFFFFFFu;              /* clear all falling pending */
    espWakePulseSeen = 0;                  /* reset before sleep */
    __DSB();
    __ISB();

    /* Enter Stop 2 */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* --- CPU resumes here after RTC or EXTI12 wake-up ---
     * Note: by the time we get here, the EXTI12 IRQ handler has already
     * run and cleared EXTI->FPR1 bit 12. We rely on espWakePulseSeen
     * which is set by HAL_GPIO_EXTI_Falling_Callback. */
    wake_source_t wakeSource = espWakePulseSeen ? WAKE_ESP32 : WAKE_RTC;

    /* Restore PLL / 160MHz system clock */
    SystemClock_Config();

    /* Re-enable SysTick + HAL timebase (TIM17) */
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    HAL_ResumeTick();

    /* Deactivate RTC wake-up timer */
    __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);

    /* Disable EXTI12 (PB12) — no longer needed as wake source */
    EXTI->FTSR1 &= ~EXTI_FTSR1_FT12;
    EXTI->IMR1  &= ~EXTI_IMR1_IM12;
    EXTI->FPR1   = EXTI_FPR1_FPIF12;   /* clear pending */

    /* Restore EXTICR mux for line 12 to its original (default port A) value */
    EXTI->EXTICR[3] = exticr3_save;

    /* Restore PB12 to its original mode (SPI2 CS output) */
    GPIOB->PUPDR &= ~(0x3u << (12*2));
    GPIOB->PUPDR |= pb12_pupdr_save;
    GPIOB->MODER &= ~(0x3u << (12*2));
    GPIOB->MODER |= pb12_moder_save;

    /* Clear UART error flags accumulated during sleep (ORE from RX pins
     * disconnected from AF) and drain stale RDR before restoring GPIOs. */
    USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    (void)USART1->RDR;
    (void)USART3->RDR;

    /* Restore peripheral GPIO states (saved before sleep) */
    GPIOD->ODR = odr_d;
    GPIOA->MODER = moder_a;
    GPIOB->MODER = moder_b;
    GPIOD->MODER = moder_d;

    /* GPIOs restored — USART RX pins reconnected, may generate new ORE.
     * Clear errors again + drain RDR before restoring NVIC enables. */
    USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    (void)USART1->RDR;
    (void)USART3->RDR;

    /* Restore all NVIC interrupt enables (saved before sleep) */
    for (uint32_t i = 0; i < 8u; i++)
        NVIC->ISER[i] = nvic_iser_save[i];

    /* Re-enable UART RXNE interrupts */
    __HAL_UART_ENABLE_IT(&husart1, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&husart3, UART_IT_RXNE);

    /* Restart MDF1 stereo DMA if it was running */
    if (wasAudioStarted) {
        extern int32_t audioBuffer[];
        extern int32_t audioBufferR[];
        MDF_DmaConfigTypeDef dmaL = {0};
        dmaL.Address = (uint32_t)audioBuffer;
        dmaL.DataLength = AUDIO_BUF_SIZE * 4;
        dmaL.MsbOnly = DISABLE;
        MDF_DmaConfigTypeDef dmaR = {0};
        dmaR.Address = (uint32_t)audioBufferR;
        dmaR.DataLength = AUDIO_BUF_SIZE * 4;
        dmaR.MsbOnly = DISABLE;
        if (HAL_MDF_AcqStart_DMA(&MdfHandle0, &MdfFilterConfig0, &dmaL) == HAL_OK &&
            HAL_MDF_AcqStart_DMA(&MdfHandle1, &MdfFilterConfig1, &dmaR) == HAL_OK) {
            HAL_MDF_GenerateTrgo(&MdfHandle0);
            audioStarted = 1;
        }
    }

    return wakeSource;
}

/* MDF1 stereo DMA callbacks — copy PCM data into L/R ring buffers from ISR.
 * This runs in ~5µs at 160MHz (512 shift+store ops) and guarantees data is
 * captured before the circular DMA overwrites the buffer.  The previous
 * approach (boolean flags + task-level copy) lost data when f_sync blocked
 * the audio task for >10.67ms — the flags couldn't track multiple callbacks
 * and the DMA buffer got overwritten before the task could read it. */
void HAL_MDF_AcqHalfCpltCallback(MDF_HandleTypeDef *hmdf)
{
    if (hmdf->Instance == MDF1_Filter0) {
        /* Left channel — first half of DMA buffer */
        dmaCallbackCount++;
        dmaCallbackTick = HAL_GetTick();
        absSampleCount += AUDIO_BUF_SIZE / 2;

        uint32_t h = ringHead;
        if ((h - ringTail) + (AUDIO_BUF_SIZE / 2) > PCM_RING_SIZE) {
            ringOverruns++;
        } else {
            const int32_t *src = &audioBuffer[0];
            for (int i = 0; i < AUDIO_BUF_SIZE / 2; i++) {
                pcmRing[h & PCM_RING_MASK] = (int32_t)(src[i] >> 8);
                h++;
            }
            ringHead = h;
        }
        osSemaphoreRelease(audioDmaSemHandle);
    } else if (hmdf->Instance == MDF1_Filter1) {
        /* Right channel — first half of DMA buffer */
        uint32_t h = ringHeadR;
        if ((h - ringTailR) + (AUDIO_BUF_SIZE / 2) <= PCM_RING_SIZE) {
            const int32_t *src = &audioBufferR[0];
            for (int i = 0; i < AUDIO_BUF_SIZE / 2; i++) {
                pcmRingR[h & PCM_RING_MASK] = (int32_t)(src[i] >> 8);
                h++;
            }
            ringHeadR = h;
        }
    }
}

void HAL_MDF_AcqCpltCallback(MDF_HandleTypeDef *hmdf)
{
    if (hmdf->Instance == MDF1_Filter0) {
        /* Left channel — second half of DMA buffer */
        dmaCallbackCount++;
        dmaCallbackTick = HAL_GetTick();
        absSampleCount += AUDIO_BUF_SIZE / 2;

        uint32_t h = ringHead;
        if ((h - ringTail) + (AUDIO_BUF_SIZE / 2) > PCM_RING_SIZE) {
            ringOverruns++;
        } else {
            const int32_t *src = &audioBuffer[AUDIO_BUF_SIZE / 2];
            for (int i = 0; i < AUDIO_BUF_SIZE / 2; i++) {
                pcmRing[h & PCM_RING_MASK] = (int32_t)(src[i] >> 8);
                h++;
            }
            ringHead = h;
        }
        osSemaphoreRelease(audioDmaSemHandle);
    } else if (hmdf->Instance == MDF1_Filter1) {
        /* Right channel — second half of DMA buffer */
        uint32_t h = ringHeadR;
        if ((h - ringTailR) + (AUDIO_BUF_SIZE / 2) <= PCM_RING_SIZE) {
            const int32_t *src = &audioBufferR[AUDIO_BUF_SIZE / 2];
            for (int i = 0; i < AUDIO_BUF_SIZE / 2; i++) {
                pcmRingR[h & PCM_RING_MASK] = (int32_t)(src[i] >> 8);
                h++;
            }
            ringHeadR = h;
        }
    }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    /* SD card detect: rising edge = card removed */
    if (GPIO_Pin == SD_CD_Pin) {
        extern osThreadId_t cliTaskHandle;
        osThreadFlagsSet(cliTaskHandle, 0x20);  /* unmount request */
        return;
    }
    if (GPIO_Pin == GPIO_PIN_8) {
        uint32_t tick = HAL_GetTick();
        ppsTick = tick;
        ppsCount++;

        /* Estimate absolute sample position at this PPS edge:
         * dmaCallbackCount * 512 = samples at last DMA callback
         * (tick - dmaCallbackTick) * 48 = ~samples since last callback
         * Gives ~±1ms (±48 sample) accuracy per edge */
        uint32_t elapsed = tick - dmaCallbackTick;
        if (elapsed > 11) elapsed = 11; /* clamp to one callback period */
        uint64_t absSample = (uint64_t)dmaCallbackCount * 512 + elapsed * 48;

        /* Track PPS edges during recording for TDOA */
        if (isRecording) {
            uint64_t recSample = absSample - recStartAbsSample;
            if (recPpsEdgesInRec == 0)
                recPpsFirstSample = recSample;
            recPpsLastSample = recSample;
            recPpsEdgesInRec++;
        }
    }
}

/* Set by EXTI12 falling callback so enterStop2() can detect the ESP32 wake
 * pulse even though the HAL IRQ handler clears EXTI->FPR1 bit 12 before
 * we get a chance to read it. */
volatile uint8_t espWakePulseSeen = 0;

/* SD card detect: falling edge = card inserted (active low) */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SD_CD_Pin) {
        extern osThreadId_t cliTaskHandle;
        osThreadFlagsSet(cliTaskHandle, 0x10);  /* mount request */
    } else if (GPIO_Pin == GPIO_PIN_12) {
        /* ESP32 CS wake pulse on PB12 — record it for enterStop2() */
        espWakePulseSeen = 1;
    }
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM17 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM17)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  /* Blink red LED (PG2) rapidly to signal error */
  __HAL_RCC_GPIOG_CLK_ENABLE();
  GPIOG->MODER = (GPIOG->MODER & ~(3UL << (2 * 2))) | (1UL << (2 * 2));
  while (1)
  {
    GPIOG->ODR ^= (1UL << 2);
    for (volatile uint32_t i = 0; i < 200000; i++);
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
