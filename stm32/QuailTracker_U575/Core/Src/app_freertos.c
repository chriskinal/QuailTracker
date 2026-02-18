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
#include "fatfs.h"
#include "user_diskio.h"
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

/* Functions from main.c */
extern int getChar(void);
extern void WAV_WriteHeader(FIL *fp, uint32_t sampleRate, uint32_t dataSize);
extern void printMenu(void);
extern void printStatus(void);
extern void startRecording(void);
extern void stopRecording(void);
extern int formatSD(void);
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
  /* add threads, ... */
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

/* USER CODE END Application */

