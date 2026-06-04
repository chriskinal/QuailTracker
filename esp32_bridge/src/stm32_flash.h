/*
 * stm32_flash.h — STM32 SPI bootloader flasher (AN4286 protocol)
 *
 * Implements the STM32 system memory bootloader SPI protocol.
 * ESP32 becomes SPI master, asserts BOOT0+NRST to enter bootloader,
 * erases flash, writes firmware, then boots into new code.
 */

#ifndef STM32_FLASH_H
#define STM32_FLASH_H

#include <stdint.h>
#include "esp_partition.h"

/* Flash STM32 from an ESP32 partition containing the firmware binary.
 * Caller must free SPI slave before calling.
 * Returns 0 on success, negative error code on failure.
 *
 * @param part       ESP32 partition containing firmware data
 * @param fw_size    Firmware size in bytes within the partition
 */
/* Progress callback: called with percentage 0-100 */
typedef void (*stm32_flash_progress_cb_t)(int pct);

int stm32_flash_from_partition(const esp_partition_t *part, uint32_t fw_size,
                               stm32_flash_progress_cb_t progress_cb);

/* CRC-32 over the first `size` bytes of a partition. Used to record a staged
 * image's integrity (e.g. in NVS) so recovery can confirm it before re-flashing.
 * Chaining convention is self-consistent — compare values produced by this fn only. */
uint32_t stm32_flash_crc32(const esp_partition_t *part, uint32_t size);

/* Error codes */
#define STM32_FLASH_OK           0
#define STM32_FLASH_ERR_SYNC    -1
#define STM32_FLASH_ERR_ERASE   -2
#define STM32_FLASH_ERR_WRITE   -3
#define STM32_FLASH_ERR_GO      -4
#define STM32_FLASH_ERR_SPI     -5
#define STM32_FLASH_ERR_SIZE    -6
#define STM32_FLASH_ERR_READ    -7
#define STM32_FLASH_ERR_VERIFY  -8

#endif /* STM32_FLASH_H */
