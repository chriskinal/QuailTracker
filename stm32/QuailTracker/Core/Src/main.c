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
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
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

DFSDM_Filter_HandleTypeDef hdfsdm1_filter0;
DFSDM_Channel_HandleTypeDef hdfsdm1_channel1;
DMA_HandleTypeDef hdma_dfsdm1_flt0;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
#define AUDIO_BUF_SIZE 1024
#define SAMPLE_RATE    50000

__attribute__((section(".RAM_D2"))) int32_t audioBuffer[AUDIO_BUF_SIZE];
volatile uint8_t halfComplete = 0;
volatile uint8_t fullComplete = 0;

/* Conversion buffer: 512 DFSDM samples -> 512 int16 samples = 1024 bytes */
int16_t pcmBuffer[AUDIO_BUF_SIZE / 2];

/* Recording state */
static FIL wavFile;
static uint8_t isRecording = 0;
static uint8_t sdMounted = 0;
static uint32_t totalDataBytes = 0;
static uint32_t fileCounter = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_DFSDM1_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Direct UART TX — bypasses HAL state machine entirely */
int _write(int file, char *ptr, int len)
{
    for (int i = 0; i < len; i++) {
        while (!(USART3->ISR & USART_ISR_TXE_TXFNF));
        USART3->TDR = (uint8_t)ptr[i];
    }
    return len;
}

/* Direct UART RX — non-blocking, clears errors */
static int getChar(void)
{
    /* Clear any error flags */
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    /* Check if data available */
    if (USART3->ISR & USART_ISR_RXNE_RXFNE) {
        return (int)(USART3->RDR & 0xFF);
    }
    return -1;
}

static void WAV_WriteHeader(FIL *fp, uint32_t sampleRate, uint32_t dataSize)
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

static void printMenu(void)
{
    printf("\r\n===== MENU =====\r\n");
    printf("1. Status\r\n");
    printf("2. Start Recording\r\n");
    printf("3. Stop Recording\r\n");
    printf("4. SD Card Info\r\n");
    printf("5. Format SD Card\r\n");
    printf("6. Eject SD Card\r\n");
    printf("7. Mount SD Card\r\n");
    printf("R. Toggle Recording\r\n");
    printf("================\r\n");
    printf("[%s] > ", isRecording ? "REC" : "IDLE");
}

static void printStatus(void)
{
    printf("\r\n========== STATUS ==========\r\n");

    printf("Audio:\r\n");
    printf("  DFSDM: Running (Sinc3, FOSR=64)\r\n");
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
        if (f_getfree("", &fre_clust, &fs) == FR_OK) {
            DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
            DWORD fre_sect = fre_clust * fs->csize;
            printf("  Total: %lu KB\r\n", (unsigned long)(tot_sect / 2));
            printf("  Free:  %lu KB\r\n", (unsigned long)(fre_sect / 2));
        }
    }

    printf("============================\r\n");
}

static void startRecording(void)
{
    if (!sdMounted) {
        printf("SD card not mounted!\r\n");
        return;
    }
    if (isRecording) {
        printf("Already recording!\r\n");
        return;
    }

    /* Generate filename: rec_000.wav, rec_001.wav, ... */
    char fname[20];
    snprintf(fname, sizeof(fname), "rec_%03lu.wav", (unsigned long)fileCounter);

    FRESULT fres = f_open(&wavFile, fname, FA_WRITE | FA_CREATE_ALWAYS);
    if (fres != FR_OK) {
        printf("f_open FAILED: %d\r\n", fres);
        return;
    }

    /* Write placeholder header (will be rewritten on stop) */
    WAV_WriteHeader(&wavFile, SAMPLE_RATE, 0);
    f_sync(&wavFile);

    totalDataBytes = 0;
    isRecording = 1;
    fileCounter++;

    printf("Recording to %s...\r\n", fname);
}

static void stopRecording(void)
{
    if (!isRecording) {
        printf("Not recording!\r\n");
        return;
    }

    isRecording = 0;

    /* Rewrite WAV header with actual data size */
    f_lseek(&wavFile, 0);
    WAV_WriteHeader(&wavFile, SAMPLE_RATE, totalDataBytes);
    f_close(&wavFile);

    uint32_t seconds = totalDataBytes / (SAMPLE_RATE * 2);
    printf("Recording stopped: %lu bytes (%lus)\r\n",
        (unsigned long)totalDataBytes, (unsigned long)seconds);
}

static int formatSD(void)
{
    extern FATFS USERFatFS;
    extern char USERPath[];

    printf("Formatting SD card (FAT32)...\r\n");

    /* Unmount first if mounted */
    if (sdMounted) {
        f_mount(NULL, USERPath, 0);
        sdMounted = 0;
    }

    /* Mount with no forced check (needed for f_mkfs) */
    FRESULT fres = f_mount(&USERFatFS, USERPath, 0);
    if (fres != FR_OK) {
        printf("f_mount failed: %d\r\n", fres);
        return 0;
    }

    /* Format as FAT32, default allocation unit */
    static uint8_t workBuf[512];
    fres = f_mkfs(USERPath, FM_FAT32, 0, workBuf, sizeof(workBuf));
    if (fres != FR_OK) {
        printf("f_mkfs failed: %d\r\n", fres);
        f_mount(NULL, USERPath, 0);
        return 0;
    }

    /* Remount to verify */
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART3_UART_Init();
  MX_DFSDM1_Init();
  MX_SPI1_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0); /* Disable stdout buffering — printf goes straight to UART */
  HAL_Delay(3000);

  printf("\r\n\r\n");
  printf("================================================\r\n");
  printf("  QuailTracker STM32 - PDM Audio Prototype\r\n");
  printf("  Nucleo-H723ZG + IM72D128 + SD Card\r\n");
  printf("================================================\r\n");

  /* Start DFSDM DMA */
  printf("DFSDM: ");
  if (HAL_DFSDM_FilterRegularStart_DMA(&hdfsdm1_filter0, audioBuffer, AUDIO_BUF_SIZE) != HAL_OK) {
      printf("FAILED\r\n");
  } else {
      printf("OK (50kHz, Sinc3)\r\n");
  }

  /* Mount SD card — auto-format if unrecognized */
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

  printMenu();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* Process serial commands */
    int c = getChar();
    if (c >= 0) {
        switch (c) {
        case '1':
            printStatus();
            printMenu();
            break;

        case '2':
            startRecording();
            printMenu();
            break;

        case '3':
            stopRecording();
            printMenu();
            break;

        case '4':
            if (sdMounted) {
                FATFS *fs;
                DWORD fre_clust;
                printf("\r\n=== SD Card Info ===\r\n");
                if (f_getfree("", &fre_clust, &fs) == FR_OK) {
                    DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
                    DWORD fre_sect = fre_clust * fs->csize;
                    printf("Total: %lu KB\r\n", (unsigned long)(tot_sect / 2));
                    printf("Free:  %lu KB\r\n", (unsigned long)(fre_sect / 2));
                }
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
                /* Wait for confirmation */
                int confirm = -1;
                while (confirm < 0) confirm = getChar();
                printf("%c\r\n", confirm);
                if (confirm == 'y' || confirm == 'Y') {
                    formatSD();
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
                f_mount(NULL, USERPath, 0);
                sdMounted = 0;
                printf("SD card ejected — safe to remove\r\n");
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
            }
            printMenu();
            break;
        }

        case 'r':
        case 'R':
            if (isRecording) {
                stopRecording();
            } else {
                startRecording();
            }
            printMenu();
            break;

        case '\r':
        case '\n':
            break;
        }
    }

    /* Background recording: write DMA buffers to SD */
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
            for (int i = 0; i < AUDIO_BUF_SIZE / 2; i++) {
                pcmBuffer[i] = (int16_t)(src[i] >> 4);
            }

            UINT bw;
            FRESULT fres = f_write(&wavFile, pcmBuffer, sizeof(pcmBuffer), &bw);
            if (fres != FR_OK) {
                printf("f_write FAILED: %d at %lu bytes\r\n", fres, (unsigned long)totalDataBytes);
                f_close(&wavFile);
                isRecording = 0;
            }
            totalDataBytes += bw;

            /* Sync every ~1 second */
            if ((totalDataBytes % (SAMPLE_RATE * 2)) < sizeof(pcmBuffer)) {
                f_sync(&wavFile);
            }
        }
    }
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 12;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DFSDM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DFSDM1_Init(void)
{

  /* USER CODE BEGIN DFSDM1_Init 0 */

  /* USER CODE END DFSDM1_Init 0 */

  /* USER CODE BEGIN DFSDM1_Init 1 */

  /* USER CODE END DFSDM1_Init 1 */
  hdfsdm1_filter0.Instance = DFSDM1_Filter0;
  hdfsdm1_filter0.Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
  hdfsdm1_filter0.Init.RegularParam.FastMode = ENABLE;
  hdfsdm1_filter0.Init.RegularParam.DmaMode = ENABLE;
  hdfsdm1_filter0.Init.FilterParam.SincOrder = DFSDM_FILTER_SINC3_ORDER;
  hdfsdm1_filter0.Init.FilterParam.Oversampling = 64;
  hdfsdm1_filter0.Init.FilterParam.IntOversampling = 1;
  if (HAL_DFSDM_FilterInit(&hdfsdm1_filter0) != HAL_OK)
  {
    Error_Handler();
  }
  hdfsdm1_channel1.Instance = DFSDM1_Channel1;
  hdfsdm1_channel1.Init.OutputClock.Activation = ENABLE;
  hdfsdm1_channel1.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
  hdfsdm1_channel1.Init.OutputClock.Divider = 20;
  hdfsdm1_channel1.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
  hdfsdm1_channel1.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
  hdfsdm1_channel1.Init.Input.Pins = DFSDM_CHANNEL_SAME_CHANNEL_PINS;
  hdfsdm1_channel1.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
  hdfsdm1_channel1.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
  hdfsdm1_channel1.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
  hdfsdm1_channel1.Init.Awd.Oversampling = 1;
  hdfsdm1_channel1.Init.Offset = 0;
  hdfsdm1_channel1.Init.RightBitShift = 2;
  if (HAL_DFSDM_ChannelInit(&hdfsdm1_channel1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DFSDM_FilterConfigRegChannel(&hdfsdm1_filter0, DFSDM_CHANNEL_1, DFSDM_CONTINUOUS_CONV_ON) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DFSDM1_Init 2 */

  /* USER CODE END DFSDM1_Init 2 */

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
  hspi1.Init.CRCPolynomial = 0x0;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);

  /*Configure GPIO pin : PD14 */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm_filter)
 {
     halfComplete = 1;
 }

 void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm_filter)
 {
     fullComplete = 1;
 }
/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  const char *msg = "\r\n!!! Error_Handler !!!\r\n";
  while (*msg) {
      while (!(USART3->ISR & USART_ISR_TXE_TXFNF));
      USART3->TDR = *msg++;
  }
  while (1) { }
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
