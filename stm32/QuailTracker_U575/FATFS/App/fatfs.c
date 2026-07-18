/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
  ******************************************************************************
  */
/* USER CODE END Header */
#include "fatfs.h"
#include "device_state.h"

uint8_t retUSER;    /* Return value for USER */
char USERPath[4];   /* USER logical drive path */
FATFS USERFatFS;    /* File system object for USER logical drive */
FIL USERFile;       /* File object for USER */

/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the USER driver ###########################*/
  retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  /* PPS-synced GPS time from device state */
  if (!dev.gps.ppsSynced || dev.gps.ppsUtcDate == 0)
    return 0;

  uint32_t yy = dev.gps.ppsUtcDate % 100;         /* 00-99 */
  uint32_t mm = (dev.gps.ppsUtcDate / 100) % 100;  /* 01-12 */
  uint32_t dd = dev.gps.ppsUtcDate / 10000;        /* 01-31 */
  uint32_t hh = dev.gps.ppsUtcTime / 10000;
  uint32_t mn = (dev.gps.ppsUtcTime / 100) % 100;
  uint32_t ss = dev.gps.ppsUtcTime % 100;

  /* FatFS packed time: bits [31:25]=year-1980, [24:21]=month, [20:16]=day,
   *                         [15:11]=hour, [10:5]=min, [4:0]=sec/2 */
  return ((DWORD)(yy + 20) << 25) | ((DWORD)mm << 21) | ((DWORD)dd << 16) |
         ((DWORD)hh << 11) | ((DWORD)mn << 5) | ((DWORD)(ss / 2));
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
