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
#include "fatfs.h"
#include "user_diskio.h"
#include "app_freertos.h"
#include "flac_encoder.h"
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

COM_InitTypeDef BspCOMInit;
MDF_HandleTypeDef AdfHandle0;
MDF_FilterConfigTypeDef AdfFilterConfig0;
DMA_NodeTypeDef Node_GPDMA1_Channel0;
DMA_QListTypeDef List_GPDMA1_Channel0;
DMA_HandleTypeDef handle_GPDMA1_Channel0;

UART_HandleTypeDef hlpuart1;

SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
#define AUDIO_BUF_SIZE 1024
#define SAMPLE_RATE    48000

int32_t audioBuffer[AUDIO_BUF_SIZE];
volatile uint8_t halfComplete = 0;
volatile uint8_t fullComplete = 0;

/* Conversion buffer: 512 samples -> 512 int16 samples = 1024 bytes */
int16_t pcmBuffer[AUDIO_BUF_SIZE / 2];

/* FLAC encoder instance (shared with app_freertos.c audio task) */
flac_enc_t flacEncoder;

/* Recording format: 0=FLAC, 1=WAV */
#define REC_FMT_FLAC 0
#define REC_FMT_WAV  1
uint8_t recFormat = REC_FMT_FLAC;

/* Recording state (shared with app_freertos.c tasks) */
FIL wavFile;
uint8_t isRecording = 0;
uint8_t sdMounted = 0;
uint8_t audioStarted = 0;
uint32_t totalDataBytes = 0;
uint32_t fileCounter = 0;

/* PPS time sync state (shared with app_freertos.c GPS task) */
volatile uint32_t ppsCount = 0;
volatile uint32_t ppsTick = 0;      /* HAL tick at last PPS edge */
uint32_t ppsUtcTime = 0;            /* HHMMSS latched from NMEA after PPS */
uint32_t ppsUtcDate = 0;            /* DDMMYY latched from NMEA after PPS */
volatile uint8_t ppsSynced = 0;     /* 1 when PPS + valid NMEA time available */
float ppsLatitude = 0.0f;
float ppsLongitude = 0.0f;

/* Recording metadata — latched at start, used for GUANO at stop */
static uint32_t recStartTime = 0;
static uint32_t recStartDate = 0;
static float    recStartLat = 0.0f;
static float    recStartLon = 0.0f;
static uint8_t  recHasGps = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void SystemPower_Config(void);
void MX_FREERTOS_Init(void);
static void MX_GPIO_Init(void);
static void MX_GPDMA1_Init(void);
static void MX_ICACHE_Init(void);
static void MX_SPI1_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_ADF1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define FW_VERSION "0.4.1"

/* Command IDs for audio task queue */
#define CMD_START_REC 1
#define CMD_STOP_REC  2

/* Direct UART RX - non-blocking, clears errors (VCP = USART1 on Nucleo-U575ZI-Q) */
int getChar(void)
{
    USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    if (USART1->ISR & USART_ISR_RXNE_RXFNE) {
        return (int)(USART1->RDR & 0xFF);
    }
    return -1;
}

/* Direct LPUART1 RX - non-blocking, clears errors (GPS on Nucleo-U575ZI-Q) */
int getCharGps(void)
{
    LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    if (LPUART1->ISR & USART_ISR_RXNE_RXFNE) {
        return (int)(LPUART1->RDR & 0xFF);
    }
    return -1;
}

void WAV_WriteHeader(FIL *fp, uint32_t sampleRate, uint32_t dataSize)
{
    uint8_t hdr[44];
    uint32_t fileSize = dataSize + 36;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
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
    char buf[256];
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
    printf("8. GPS Status\r\n");
    printf("F. Toggle Format (%s)\r\n", recFormat == REC_FMT_WAV ? "WAV" : "FLAC");
    printf("G. Toggle GPS Raw Output\r\n");
    printf("R. Toggle Recording\r\n");
    printf("================\r\n");
    printf("[%s] > ", isRecording ? "REC" : "IDLE");
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

    printf("Recording:\r\n");
    printf("  Active: %s\r\n", isRecording ? "Yes" : "No");
    if (isRecording) {
        uint32_t seconds = totalDataBytes / (SAMPLE_RATE * 2);
        printf("  Duration: %lus\r\n", (unsigned long)seconds);
        printf("  Size: %lu bytes\r\n", (unsigned long)totalDataBytes);
    }

    printf("SD Card:\r\n");
    printf("  Mounted: %s\r\n", sdMounted ? "Yes" : "No");
    if (sdMounted) {
        FATFS *fs;
        DWORD fre_clust;
        osMutexAcquire(fileMtxHandle, osWaitForever);
        if (f_getfree("", &fre_clust, &fs) == FR_OK) {
            DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
            DWORD fre_sect = fre_clust * fs->csize;
            printf("  Total: %lu KB\r\n", (unsigned long)(tot_sect / 2));
            printf("  Free:  %lu KB\r\n", (unsigned long)(fre_sect / 2));
        }
        osMutexRelease(fileMtxHandle);
    }

    printf("============================\r\n");
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

    const char *ext = (recFormat == REC_FMT_WAV) ? "wav" : "flac";
    char fname[32];
    if (ppsSynced && ppsUtcDate != 0) {
        /* ppsUtcDate = DDMMYY, ppsUtcTime = HHMMSS */
        uint32_t dd = ppsUtcDate / 10000;
        uint32_t mm = (ppsUtcDate / 100) % 100;
        uint32_t yy = ppsUtcDate % 100;
        uint32_t hh = ppsUtcTime / 10000;
        uint32_t mn = (ppsUtcTime / 100) % 100;
        uint32_t ss = ppsUtcTime % 100;
        snprintf(fname, sizeof(fname), "20%02lu%02lu%02lu_%02lu%02lu%02lu.%s",
                 (unsigned long)yy, (unsigned long)mm, (unsigned long)dd,
                 (unsigned long)hh, (unsigned long)mn, (unsigned long)ss, ext);
    } else {
        snprintf(fname, sizeof(fname), "rec_%03lu.%s", (unsigned long)fileCounter, ext);
    }

    /* Latch GPS state for GUANO metadata */
    if (ppsSynced && ppsUtcDate != 0) {
        recStartTime = ppsUtcTime;
        recStartDate = ppsUtcDate;
        recStartLat  = ppsLatitude;
        recStartLon  = ppsLongitude;
        recHasGps = 1;
    } else {
        recHasGps = 0;
    }

    FRESULT fres = f_open(&wavFile, fname, FA_WRITE | FA_CREATE_ALWAYS);
    if (fres != FR_OK) {
        printf("f_open FAILED: %d\r\n", fres);
        return;
    }

    /* Write placeholder header (will be finalized on stop) */
    if (recFormat == REC_FMT_WAV) {
        WAV_WriteHeader(&wavFile, SAMPLE_RATE, 0);
    } else {
        flac_enc_init(&flacEncoder);
        uint8_t hdr[FLAC_HEADER_SIZE];
        flac_enc_write_header(&flacEncoder, hdr);
        UINT bw;
        f_write(&wavFile, hdr, FLAC_HEADER_SIZE, &bw);
    }
    f_sync(&wavFile);

    totalDataBytes = 0;
    isRecording = 1;
    fileCounter++;

    printf("Recording to %s...\r\n", fname);
}

void stopRecording(void)
{
    if (!isRecording) {
        printf("Not recording!\r\n");
        return;
    }

    isRecording = 0;

    if (recFormat == REC_FMT_WAV) {
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

        uint32_t seconds = totalDataBytes / (SAMPLE_RATE * 2);
        printf("Recording stopped: %lu bytes (%lus)\r\n",
            (unsigned long)totalDataBytes, (unsigned long)seconds);
    } else {
        /* Flush any remaining partial FLAC block */
        uint32_t flushBytes = flac_enc_flush(&flacEncoder);
        if (flushBytes > 0) {
            UINT bw;
            f_write(&wavFile, flacEncoder.outBuf, flushBytes, &bw);
            totalDataBytes += bw;
        }

        /* Rewrite STREAMINFO at file offset 0 with final values */
        uint8_t hdr[FLAC_HEADER_SIZE];
        flac_enc_finalize_header(&flacEncoder, hdr);
        f_lseek(&wavFile, 0);
        UINT bw;
        f_write(&wavFile, hdr, FLAC_HEADER_SIZE, &bw);

        f_close(&wavFile);

        uint32_t seconds = (uint32_t)(flacEncoder.totalSamples / SAMPLE_RATE);
        uint32_t rawSize = (uint32_t)(flacEncoder.totalSamples * 2);
        uint32_t ratio = rawSize > 0 ? (totalDataBytes * 100) / rawSize : 0;
        printf("Recording stopped: %lu bytes (%lus, %lu%% of raw)\r\n",
            (unsigned long)totalDataBytes, (unsigned long)seconds,
            (unsigned long)ratio);
    }
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

  /* USER CODE END Init */

  /* Configure the System Power */
  SystemPower_Config();

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_ICACHE_Init();
  MX_SPI1_Init();
  MX_LPUART1_UART_Init();
  MX_ADF1_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);

  /* BSP init must happen before scheduler — COM1 needed for printf */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_BLUE);
  BSP_LED_Init(LED_RED);
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  printf("\r\n\r\n");
  printf("================================================\r\n");
  printf("  QuailTracker U575 - PDM Audio Recorder\r\n");
  printf("  Nucleo-U575ZI-Q  v%s  [FreeRTOS]\r\n", FW_VERSION);
  printf("  SYSCLK: %lu MHz\r\n",
         (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000UL));
  printf("================================================\r\n");

  /* Start ADF1 DMA acquisition before scheduler */
  {
    AdfFilterConfig0.DecimationRatio = 64;
    AdfFilterConfig0.Gain = 6;

    MDF_DmaConfigTypeDef dmaConfig = {0};
    dmaConfig.Address = (uint32_t)audioBuffer;
    dmaConfig.DataLength = AUDIO_BUF_SIZE * 4;
    dmaConfig.MsbOnly = DISABLE;

    printf("ADF1: ");
    if (HAL_MDF_AcqStart_DMA(&AdfHandle0, &AdfFilterConfig0, &dmaConfig) != HAL_OK) {
        printf("FAILED\r\n");
    } else {
        audioStarted = 1;
        printf("OK (48kHz, Sinc4, gain=%d)\r\n", (int)AdfFilterConfig0.Gain);
    }
  }

  /* Init FatFS and mount SD card */
  MX_FATFS_Init();

  extern FATFS USERFatFS;
  extern char USERPath[];
  printf("SD Card: ");
  if (f_mount(&USERFatFS, USERPath, 1) == FR_OK) {
      sdMounted = 1;
      printf("Mounted\r\n");
  } else {
      printf("Not readable, formatting...\r\n");
      if (formatSD()) {
          printf("SD Card: Ready\r\n");
      } else {
          printf("SD Card: No card detected\r\n");
      }
  }
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();
  /* Call init function for freertos objects (in app_freertos.c) */
  MX_FREERTOS_Init();

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_BLUE);
  BSP_LED_Init(LED_RED);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

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

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
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
   * Switch to SMPS regulator instead of LDO
   */
  if (HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }
/* USER CODE BEGIN PWR */
/* USER CODE END PWR */
}

/**
  * @brief ADF1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADF1_Init(void)
{

  /* USER CODE BEGIN ADF1_Init 0 */

  /* USER CODE END ADF1_Init 0 */

  /* USER CODE BEGIN ADF1_Init 1 */

  /* USER CODE END ADF1_Init 1 */

  /**
    AdfHandle0 structure initialization and HAL_MDF_Init function call
  */
  AdfHandle0.Instance = ADF1_Filter0;
  AdfHandle0.Init.CommonParam.ProcClockDivider = 52;
  AdfHandle0.Init.CommonParam.OutputClock.Activation = ENABLE;
  AdfHandle0.Init.CommonParam.OutputClock.Pins = MDF_OUTPUT_CLOCK_0;
  AdfHandle0.Init.CommonParam.OutputClock.Divider = 1;
  AdfHandle0.Init.CommonParam.OutputClock.Trigger.Activation = DISABLE;
  AdfHandle0.Init.SerialInterface.Activation = ENABLE;
  AdfHandle0.Init.SerialInterface.Mode = MDF_SITF_LF_MASTER_SPI_MODE;
  AdfHandle0.Init.SerialInterface.ClockSource = MDF_SITF_CCK0_SOURCE;
  AdfHandle0.Init.SerialInterface.Threshold = 4;
  AdfHandle0.Init.FilterBistream = MDF_BITSTREAM0_FALLING;
  if (HAL_MDF_Init(&AdfHandle0) != HAL_OK)
  {
    Error_Handler();
  }

  /**
    AdfFilterConfig0 structure initialization

    WARNING : only structure is filled, no specific init function call for filter
  */
  AdfFilterConfig0.DataSource = MDF_DATA_SOURCE_BSMX;
  AdfFilterConfig0.Delay = 0;
  AdfFilterConfig0.CicMode = MDF_ONE_FILTER_SINC4;
  AdfFilterConfig0.DecimationRatio = 64;
  AdfFilterConfig0.Gain = 0;
  AdfFilterConfig0.ReshapeFilter.Activation = DISABLE;
  AdfFilterConfig0.HighPassFilter.Activation = DISABLE;
  AdfFilterConfig0.SoundActivity.Activation = DISABLE;
  AdfFilterConfig0.AcquisitionMode = MDF_MODE_ASYNC_CONT;
  AdfFilterConfig0.FifoThreshold = MDF_FIFO_THRESHOLD_NOT_EMPTY;
  AdfFilterConfig0.DiscardSamples = 0;
  /* USER CODE BEGIN ADF1_Init 2 */

  /* USER CODE END ADF1_Init 2 */

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
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  hlpuart1.FifoMode = UART_FIFOMODE_DISABLE;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */
  /* Override for L76K GPS (9600 baud).
   * LPUART BRR is 20-bit: at 160MHz/DIV1, 9600 needs BRR=4,266,667 (overflow!).
   * DIV8 → 20MHz effective clock → BRR=533,333 (fits). */
  hlpuart1.Init.BaudRate = 9600;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV8;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK) {
      printf("LPUART1 9600 init FAILED!\r\n");
  }
  /* USER CODE END LPUART1_Init 2 */

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
  /* PA8 — GPS PPS input, rising-edge EXTI */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_NVIC_SetPriority(EXTI8_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI8_IRQn);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* ADF1 DMA callbacks — signal audio task via semaphore */
void HAL_MDF_AcqHalfCpltCallback(MDF_HandleTypeDef *hmdf)
{
    halfComplete = 1;
    osSemaphoreRelease(audioDmaSemHandle);
}

void HAL_MDF_AcqCpltCallback(MDF_HandleTypeDef *hmdf)
{
    fullComplete = 1;
    osSemaphoreRelease(audioDmaSemHandle);
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_8) {
        ppsTick = HAL_GetTick();
        ppsCount++;
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
