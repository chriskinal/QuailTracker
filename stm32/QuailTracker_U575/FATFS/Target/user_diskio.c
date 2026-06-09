/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
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

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "main.h"

/* Private typedef -----------------------------------------------------------*/
typedef enum {
    CT_UNKNOWN = 0,
    CT_MMC     = 1,
    CT_SD1     = 2,   /* SDv1 */
    CT_SD2     = 4,   /* SDv2 */
    CT_BLOCK   = 8,   /* Block addressing (SDHC/SDXC) */
} CardType_t;

/* Private define ------------------------------------------------------------*/
/* SD_CS_Pin and SD_CS_GPIO_Port defined in main.h (PA4, GPIOA) */

#define CS_LOW()   HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET)
#define CS_HIGH()  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET)

/* SD SPI commands */
#define CMD0    0   /* GO_IDLE_STATE */
#define CMD1    1   /* SEND_OP_COND (MMC) */
#define CMD8    8   /* SEND_IF_COND */
#define CMD9    9   /* SEND_CSD */
#define CMD10   10  /* SEND_CID */
#define CMD12   12  /* STOP_TRANSMISSION */
#define CMD16   16  /* SET_BLOCKLEN */
#define CMD17   17  /* READ_SINGLE_BLOCK */
#define CMD18   18  /* READ_MULTIPLE_BLOCK */
#define CMD24   24  /* WRITE_BLOCK */
#define CMD25   25  /* WRITE_MULTIPLE_BLOCK */
#define CMD55   55  /* APP_CMD */
#define CMD58   58  /* READ_OCR */
#define ACMD41  41  /* SD_SEND_OP_COND */

/* Private variables ---------------------------------------------------------*/
extern SPI_HandleTypeDef hspi1;
static volatile DSTATUS Stat = STA_NOINIT;
static uint8_t CardType;

/* Private SD SPI helpers ---------------------------------------------------*/
static volatile uint8_t spiDead = 0;  /* set if SPI times out — all ops bail */

static void SPI_Start(void)
{
    /* Enable SPI in infinite transfer mode (TSIZE=0) */
    if (!(hspi1.Instance->CR1 & SPI_CR1_SPE)) {
        hspi1.Instance->CR2 = 0;
        SET_BIT(hspi1.Instance->CR1, SPI_CR1_SPE);
        SET_BIT(hspi1.Instance->CR1, SPI_CR1_CSTART);
    }
}

static void SPI_Stop(void)
{
    /* Suspend and disable SPI */
    SET_BIT(hspi1.Instance->CR1, SPI_CR1_CSUSP);
    uint32_t t = 0;
    while ((hspi1.Instance->CR1 & SPI_CR1_CSTART) && ++t < 100000);
    CLEAR_BIT(hspi1.Instance->CR1, SPI_CR1_SPE);
    /* Flush RX FIFO */
    t = 0;
    while ((hspi1.Instance->SR & SPI_SR_RXP) && ++t < 1000)
        (void)*(volatile uint8_t *)&hspi1.Instance->RXDR;
    /* Clear flags */
    SET_BIT(hspi1.Instance->IFCR, 0xFFFFFFFF);
}

/* Hard-reset SPI peripheral via RCC and toggle GPIO to force pin re-sync */
static void SPI_Recover(void)
{
    /* RCC reset — unconditionally returns SPI1 to power-on state */
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();

    /* Toggle pins out of AF and back */
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_Delay(1);
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);

    /* Re-init SPI registers (RCC reset wiped them) */
    HAL_SPI_Init(&hspi1);

    spiDead = 0;
}

static void SPI_SetSlow(void)
{
    if (hspi1.Instance->CR1 & SPI_CR1_SPE) SPI_Stop();
    MODIFY_REG(hspi1.Instance->CFG1, SPI_CFG1_MBR_Msk, SPI_BAUDRATEPRESCALER_256);
}

static void SPI_SetFast(void)
{
    if (hspi1.Instance->CR1 & SPI_CR1_SPE) SPI_Stop();
    MODIFY_REG(hspi1.Instance->CFG1, SPI_CFG1_MBR_Msk, SPI_BAUDRATEPRESCALER_32);
}

/* Timeout ~10ms at 160MHz (each iteration is a few cycles) */
#define SPI_TIMEOUT 500000

static uint8_t SPI_TxRx(uint8_t data)
{
    if (spiDead) return 0xFF;
    SPI_Start();
    uint32_t t = 0;
    while (!(hspi1.Instance->SR & SPI_SR_TXP)) {
        if (++t > SPI_TIMEOUT) { spiDead = 1; return 0xFF; }
    }
    *(volatile uint8_t *)&hspi1.Instance->TXDR = data;
    t = 0;
    while (!(hspi1.Instance->SR & SPI_SR_RXP)) {
        if (++t > SPI_TIMEOUT) { spiDead = 1; return 0xFF; }
    }
    return *(volatile uint8_t *)&hspi1.Instance->RXDR;
}

static void SPI_TxMulti(const uint8_t *buf, uint16_t len)
{
    if (spiDead) return;
    SPI_Start();
    while (len--) {
        uint32_t t = 0;
        while (!(hspi1.Instance->SR & SPI_SR_TXP)) {
            if (++t > SPI_TIMEOUT) { spiDead = 1; return; }
        }
        *(volatile uint8_t *)&hspi1.Instance->TXDR = *buf++;
        t = 0;
        while (!(hspi1.Instance->SR & SPI_SR_RXP)) {
            if (++t > SPI_TIMEOUT) { spiDead = 1; return; }
        }
        (void)*(volatile uint8_t *)&hspi1.Instance->RXDR;
    }
}

static void SPI_RxMulti(uint8_t *buf, uint16_t len)
{
    if (spiDead) return;
    SPI_Start();
    while (len--) {
        uint32_t t = 0;
        while (!(hspi1.Instance->SR & SPI_SR_TXP)) {
            if (++t > SPI_TIMEOUT) { spiDead = 1; return; }
        }
        *(volatile uint8_t *)&hspi1.Instance->TXDR = 0xFF;
        t = 0;
        while (!(hspi1.Instance->SR & SPI_SR_RXP)) {
            if (++t > SPI_TIMEOUT) { spiDead = 1; return; }
        }
        *buf++ = *(volatile uint8_t *)&hspi1.Instance->RXDR;
    }
}

static uint8_t SD_ReadyWait(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t rx;
    do {
        rx = SPI_TxRx(0xFF);
        if (rx == 0xFF) return 1;
    } while ((HAL_GetTick() - start) < timeout_ms);
    return 0;
}

static void SD_Deselect(void)
{
    CS_HIGH();
    SPI_TxRx(0xFF);
}

static int SD_Select(void)
{
    CS_LOW();
    SPI_TxRx(0xFF);
    if (SD_ReadyWait(500)) return 1;
    SD_Deselect();
    return 0;
}

static uint8_t SD_SendCmd(uint8_t cmd, uint32_t arg)
{
    uint8_t res;

    if (cmd & 0x80) {
        /* ACMD: send CMD55 first */
        cmd &= 0x7F;
        res = SD_SendCmd(CMD55, 0);
        if (res > 1) return res;
    }

    /* Select card and wait ready (except CMD0/CMD12) */
    if (cmd != CMD12) {
        SD_Deselect();
        if (!SD_Select()) return 0xFF;
    }

    /* Send command packet */
    SPI_TxRx(0x40 | cmd);
    SPI_TxRx((uint8_t)(arg >> 24));
    SPI_TxRx((uint8_t)(arg >> 16));
    SPI_TxRx((uint8_t)(arg >> 8));
    SPI_TxRx((uint8_t)(arg));

    /* CRC - required for CMD0 and CMD8 */
    uint8_t crc = 0xFF;
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;
    SPI_TxRx(crc);

    /* Skip stuff byte for CMD12 */
    if (cmd == CMD12) SPI_TxRx(0xFF);

    /* Wait for response (up to 10 bytes) */
    uint8_t n = 10;
    do {
        res = SPI_TxRx(0xFF);
    } while ((res & 0x80) && --n);

    return res;
}

static int SD_RxDataBlock(uint8_t *buf, uint16_t len)
{
    uint8_t token;
    uint32_t start = HAL_GetTick();

    do {
        token = SPI_TxRx(0xFF);
    } while (token == 0xFF && (HAL_GetTick() - start) < 200);

    if (token != 0xFE) return 0;

    SPI_RxMulti(buf, len);
    SPI_TxRx(0xFF); /* Discard CRC */
    SPI_TxRx(0xFF);
    return 1;
}

static int SD_TxDataBlock(const uint8_t *buf, uint8_t token)
{
    if (!SD_ReadyWait(500)) return 0;

    SPI_TxRx(token);
    if (token != 0xFD) {
        SPI_TxMulti(buf, 512);
        SPI_TxRx(0xFF); /* Dummy CRC */
        SPI_TxRx(0xFF);

        uint8_t resp = SPI_TxRx(0xFF);
        if ((resp & 0x1F) != 0x05) return 0;
    }
    return 1;
}

void USER_disk_deinit(void)
{
    /* Reset ST's FatFS glue layer init flag so disk_initialize() actually calls us */
    extern Disk_drvTypeDef disk;
    disk.is_initialized[0] = 0;
    Stat = STA_NOINIT;
    CardType = 0;
}

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
    uint8_t n, cmd, ty, ocr[4];
    uint8_t retry;

    /* Reset SPI state and clear any prior error flags */
    SPI_Recover();

    for (retry = 0; retry < 3; retry++) {
    SPI_SetSlow();
    HAL_Delay(retry == 0 ? 10 : 200); /* Longer delay on retries for card power-up */

    CS_HIGH();
    for (n = 0; n < 10; n++) SPI_TxRx(0xFF); /* 80 clocks with CS high */

    ty = 0;
    if (SD_SendCmd(CMD0, 0) == 1) {
        uint32_t start = HAL_GetTick();
        if (SD_SendCmd(CMD8, 0x1AA) == 1) {
            /* SDv2 */
            for (n = 0; n < 4; n++) ocr[n] = SPI_TxRx(0xFF);
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                while ((HAL_GetTick() - start) < 1000 && SD_SendCmd(ACMD41 | 0x80, 1UL << 30));
                if ((HAL_GetTick() - start) < 1000 && SD_SendCmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = SPI_TxRx(0xFF);
                    ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
                }
            }
        } else {
            /* SDv1 or MMC */
            if (SD_SendCmd(ACMD41 | 0x80, 0) <= 1) {
                ty = CT_SD1; cmd = ACMD41 | 0x80;
            } else {
                ty = CT_MMC; cmd = CMD1;
            }
            while ((HAL_GetTick() - start) < 1000 && SD_SendCmd(cmd, 0));
            if ((HAL_GetTick() - start) >= 1000) ty = 0;
        }
    }
    if (ty && !(ty & CT_BLOCK)) {
        SD_SendCmd(CMD16, 512); /* Set block size to 512 */
    }

    CardType = ty;
    SD_Deselect();

    if (ty) {
        SPI_SetFast();
        Stat &= ~STA_NOINIT;
        break; /* Success — exit retry loop */
    }
    /* Stop SPI before retry */
    if (hspi1.Instance->CR1 & SPI_CR1_SPE) SPI_Stop();
    } /* end retry loop */
    return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
    return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & CT_BLOCK)) sector *= 512; /* Convert to byte address if needed */

    if (count == 1) {
        if (SD_SendCmd(CMD17, sector) == 0) {
            if (SD_RxDataBlock(buff, 512)) count = 0;
        }
    } else {
        if (SD_SendCmd(CMD18, sector) == 0) {
            do {
                if (!SD_RxDataBlock(buff, 512)) break;
                buff += 512;
            } while (--count);
            SD_SendCmd(CMD12, 0);
        }
    }
    SD_Deselect();
    return count ? RES_ERROR : RES_OK;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
    /* Published format progress: while f_mkfs runs on the format thread, tally
     * the bytes it writes so the web UI can show a live "N MB written" figure. */
    extern volatile uint8_t  sdFormatState;
    extern volatile uint32_t sdFormatBytes;

    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & CT_BLOCK)) sector *= 512;

    UINT reqCount = count;

    if (count == 1) {
        if (SD_SendCmd(CMD24, sector) == 0) {
            if (SD_TxDataBlock(buff, 0xFE)) count = 0;
        }
    } else {
        if (CardType & (CT_SD1 | CT_SD2)) {
            SD_SendCmd(ACMD41 | 0x80, 0); /* Dummy ACMD23 prep */
        }
        if (SD_SendCmd(CMD25, sector) == 0) {
            do {
                if (!SD_TxDataBlock(buff, 0xFC)) break;
                buff += 512;
            } while (--count);
            if (!SD_TxDataBlock(0, 0xFD)) count = 1; /* Stop token */
        }
    }
    SD_Deselect();
    if (sdFormatState == 1 && count == 0)
        sdFormatBytes += (uint32_t)reqCount * 512U;
    return count ? RES_ERROR : RES_OK;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
    DRESULT res = RES_ERROR;
    uint8_t csd[16];
    DWORD cs;

    if (pdrv) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        if (SD_Select()) res = RES_OK;
        SD_Deselect();
        break;

    case GET_SECTOR_COUNT:
        if (SD_SendCmd(CMD9, 0) == 0 && SD_RxDataBlock(csd, 16)) {
            if ((csd[0] >> 6) == 1) {
                /* SDv2 (CSD v2) */
                cs = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
                *(DWORD*)buff = cs << 10;
            } else {
                /* SDv1 or MMC (CSD v1) */
                uint8_t n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                cs = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
                *(DWORD*)buff = cs << (n - 9);
            }
            res = RES_OK;
        }
        SD_Deselect();
        break;

    case GET_SECTOR_SIZE:
        *(WORD*)buff = 512;
        res = RES_OK;
        break;

    case GET_BLOCK_SIZE:
        *(DWORD*)buff = 128; /* Erase block size in sectors */
        res = RES_OK;
        break;

    default:
        res = RES_PARERR;
    }
    return res;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */
