/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  *  FatFs - Generic FAT file system module  R0.12c (C)ChaN, 2017
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

#ifndef _FFCONF
#define _FFCONF 68300	/* Revision ID */

/*-----------------------------------------------------------------------------/
/ Additional user header to be used
/-----------------------------------------------------------------------------*/
#include "main.h"
#include "stm32u5xx_hal.h"

/*-----------------------------------------------------------------------------/
/ Function Configurations
/-----------------------------------------------------------------------------*/

#define _FS_READONLY         0
#define _FS_MINIMIZE         0
#define _USE_STRFUNC         2
#define _USE_FIND            0
#define _USE_MKFS            1
#define _USE_FASTSEEK        1
#define	_USE_EXPAND		0
#define _USE_CHMOD		0
#define _USE_LABEL           1
#define _USE_FORWARD         0

/*-----------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/-----------------------------------------------------------------------------*/

#define _CODE_PAGE         850
#define _USE_LFN     1
#define _MAX_LFN     255
#define _LFN_UNICODE    0
#define _STRF_ENCODE    3
#define _FS_RPATH       0

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/----------------------------------------------------------------------------*/

#define _VOLUMES    1

/* USER CODE BEGIN Volumes */
#define _STR_VOLUME_ID          0
#define _VOLUME_STRS            "RAM","NAND","CF","SD1","SD2","USB1","USB2","USB3"
/* USER CODE END Volumes */

#define _MULTI_PARTITION     0
#define _MIN_SS    512
#define _MAX_SS    512
#define	_USE_TRIM      0
#define _FS_NOFSINFO    0

/*---------------------------------------------------------------------------/
/ System Configurations
/----------------------------------------------------------------------------*/

#define _FS_TINY    0
#define _FS_EXFAT	0
#define _FS_NORTC	0
#define _NORTC_MON	6
#define _NORTC_MDAY	4
#define _NORTC_YEAR	2015
#define _FS_LOCK    2
#define _FS_REENTRANT    0
#define _FS_TIMEOUT      1000
#define _SYNC_t          NULL

/* define the ff_malloc ff_free macros as standard malloc free */
#if !defined(ff_malloc) && !defined(ff_free)
#include <stdlib.h>
#define ff_malloc  malloc
#define ff_free  free
#endif

#endif /* _FFCONF */
