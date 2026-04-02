/*
 * stm32_flash.c — STM32 SPI bootloader flasher (AN4286 protocol)
 *
 * Protocol summary (ESP32 = SPI master, STM32 bootloader = SPI slave):
 *   1. Send sync byte 0x5A, receive 0xA5 (bootloader detected SPI)
 *   2. Get ACK: send 0x00, receive dummy; send dummy, receive ACK (0x79)
 *   3. Commands: SOF(0x5A) + cmd + ~cmd → wait ACK → send data → wait ACK
 *   4. Write Memory (0x31): 256 bytes at a time to 0x08000000+
 *   5. Erase (0x44): page-based or mass erase
 *   6. Go (0x21): jump to 0x08000000
 *
 * Minimum 15µs between SPI bytes (handled by ESP-IDF SPI driver overhead).
 */

#include "stm32_flash.h"
#include <string.h>
#include "driver/spi_master.h"
#include "esp_partition.h"
#include "esp_rom_sys.h"  /* for esp_rom_delay_us */
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "STM32_FLASH"

/* Pin assignments (must match main.c) */
#define PIN_SCK    4
#define PIN_MISO   5
#define PIN_MOSI   6
#define PIN_CS     7
#define PIN_NRST   2
#define PIN_BOOT0  3

/* SPI bootloader constants */
#define BL_SOF     0x5A
#define BL_ACK     0x79
#define BL_NACK    0x1F
#define BL_SYNC_RESP 0xA5
#define BL_DUMMY   0x00

#define CMD_GET          0x00
#define CMD_GET_VERSION  0x01
#define CMD_GET_ID       0x02
#define CMD_READ_MEM     0x11
#define CMD_GO            0x21
#define CMD_WRITE_MEM    0x31
#define CMD_ERASE        0x44

#define STM32_FLASH_BASE  0x08000000
#define STM32_FLASH_SIZE  (512 * 1024)  /* 512KB Bank 1 */
#define STM32_PAGE_SIZE   8192
#define WRITE_CHUNK_SIZE  256  /* max per Write Memory command */

#define SPI_HOST_BL  SPI2_HOST
#define SPI_SPEED_HZ 2000000  /* 2 MHz — conservative, bootloader supports up to 8 */

static spi_device_handle_t spi_dev = NULL;

/* ── Low-level SPI byte transfer ──────────────────────────────── */

static uint8_t spi_xfer_byte(uint8_t tx)
{
    uint8_t rx = 0;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &tx,
        .rx_buffer = &rx,
    };
    spi_device_transmit(spi_dev, &t);
    return rx;
}

/* ── Bootloader ACK polling (AN4286 Figure 2) ────────────────── */

static bool bl_get_ack(int timeout_ms)
{
    /* Per AN4286 Figure 2: send 0x00, receive dummy to start slave response.
     * Then poll: send dummy, check response for ACK(0x79) or NACK(0x1F).
     * The bootloader sends 0xA5 as dummy/busy while processing. */

    /* First exchange: send 0x00, ignore response */
    spi_xfer_byte(BL_DUMMY);

    /* Poll for ACK/NACK.  Use fast µs delay between polls — the bootloader
     * responds in microseconds for most commands.  Only erase needs long waits. */
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ticks < 1) timeout_ticks = 1;

    int polls = 0;
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        uint8_t resp = spi_xfer_byte(BL_DUMMY);
        if (resp == BL_ACK) {
            spi_xfer_byte(BL_ACK);
            return true;
        }
        if (resp == BL_NACK) {
            ESP_LOGE(TAG, "NACK after %d polls", polls);
            return false;
        }
        polls++;
        /* Fast poll: 50µs for normal commands, yield every 100 polls for erase */
        if ((polls % 100) == 0)
            vTaskDelay(1);
        else
            esp_rom_delay_us(50);
    }
    ESP_LOGE(TAG, "ACK timeout after %d ms (%d polls)", timeout_ms, polls);
    return false;
}

/* ── Send command frame (SOF + cmd + ~cmd) ────────────────────── */

static bool bl_send_cmd(uint8_t cmd)
{
    spi_xfer_byte(BL_SOF);
    spi_xfer_byte(cmd);
    spi_xfer_byte(~cmd);
    return bl_get_ack(1000);
}

/* ── Sync with bootloader (AN4286 Figure 3) ──────────────────── */

static bool bl_sync(void)
{
    ESP_LOGI(TAG, "Syncing with bootloader...");

    for (int attempt = 0; attempt < 50; attempt++) {
        uint8_t resp = spi_xfer_byte(BL_SOF);
        if (resp == BL_SYNC_RESP) {
            if (bl_get_ack(1000)) {
                ESP_LOGI(TAG, "Bootloader synced on attempt %d", attempt);
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGE(TAG, "Bootloader sync failed after 50 attempts");
    return false;
}

/* ── Erase flash pages ────────────────────────────────────────── */

static bool bl_erase(uint32_t num_pages)
{
    ESP_LOGI(TAG, "Erasing %lu pages...", (unsigned long)num_pages);

    if (!bl_send_cmd(CMD_ERASE)) {
        ESP_LOGE(TAG, "Erase command rejected");
        return false;
    }

    /* AN4286 Section 2.8: Extended Erase (0x44)
     * Send: number_of_pages-1 (2 bytes) + page_numbers (2 bytes each) + checksum
     * Or: 0xFFFF + 0x00 for mass erase
     * Checksum = XOR of all bytes sent after the command ACK */

    if (num_pages == 0xFFFF) {
        /* Mass erase */
        spi_xfer_byte(0xFF);
        spi_xfer_byte(0xFF);
        spi_xfer_byte(0x00);  /* checksum: 0xFF ^ 0xFF = 0x00 */
    } else {
        uint16_t n_minus_1 = (uint16_t)(num_pages - 1);
        uint8_t xor_check = (uint8_t)(n_minus_1 >> 8) ^ (uint8_t)(n_minus_1 & 0xFF);

        spi_xfer_byte((uint8_t)(n_minus_1 >> 8));
        spi_xfer_byte((uint8_t)(n_minus_1 & 0xFF));

        for (uint16_t page = 0; page < num_pages; page++) {
            spi_xfer_byte((uint8_t)(page >> 8));
            spi_xfer_byte((uint8_t)(page & 0xFF));
            xor_check ^= (uint8_t)(page >> 8);
            xor_check ^= (uint8_t)(page & 0xFF);
        }
        spi_xfer_byte(xor_check);

        ESP_LOGI(TAG, "Erase data sent (N-1=%u, pages=0..%lu, xor=0x%02x), waiting for ACK...",
                 n_minus_1, (unsigned long)(num_pages - 1), xor_check);
    }

    /* Erase can take 10-40 seconds depending on page count */
    if (!bl_get_ack(60000)) {
        ESP_LOGE(TAG, "Erase ACK timeout");
        return false;
    }

    ESP_LOGI(TAG, "Erase complete");
    return true;
}

/* ── Write Memory (256 bytes max per call) ────────────────────── */

static bool bl_write_memory(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > WRITE_CHUNK_SIZE) return false;

    if (!bl_send_cmd(CMD_WRITE_MEM))
        return false;

    /* Send address (4 bytes MSB first) + XOR checksum */
    uint8_t addr_bytes[4] = {
        (uint8_t)(addr >> 24), (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),  (uint8_t)(addr & 0xFF)
    };
    uint8_t xor_addr = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];
    for (int i = 0; i < 4; i++)
        spi_xfer_byte(addr_bytes[i]);
    spi_xfer_byte(xor_addr);

    if (!bl_get_ack(1000)) {
        ESP_LOGE(TAG, "Write address NACK at 0x%08lx", (unsigned long)addr);
        return false;
    }

    /* Send N-1 + data bytes + XOR checksum */
    uint8_t n_minus_1 = (uint8_t)(len - 1);
    uint8_t xor_data = n_minus_1;
    spi_xfer_byte(n_minus_1);

    for (uint16_t i = 0; i < len; i++) {
        spi_xfer_byte(data[i]);
        xor_data ^= data[i];
    }
    spi_xfer_byte(xor_data);

    if (!bl_get_ack(1000)) {
        ESP_LOGE(TAG, "Write data NACK at 0x%08lx", (unsigned long)addr);
        return false;
    }

    return true;
}

/* ── Go command — jump to address ─────────────────────────────── */

static bool bl_go(uint32_t addr)
{
    ESP_LOGI(TAG, "Sending Go command to 0x%08lx", (unsigned long)addr);

    if (!bl_send_cmd(CMD_GO))
        return false;

    /* Send address + XOR checksum */
    uint8_t addr_bytes[4] = {
        (uint8_t)(addr >> 24), (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),  (uint8_t)(addr & 0xFF)
    };
    uint8_t xor_addr = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];
    for (int i = 0; i < 4; i++)
        spi_xfer_byte(addr_bytes[i]);
    spi_xfer_byte(xor_addr);

    if (!bl_get_ack(1000)) {
        ESP_LOGE(TAG, "Go NACK");
        return false;
    }

    ESP_LOGI(TAG, "Go command accepted — STM32 booting new firmware");
    return true;
}

/* ── SPI master init/deinit ───────────────────────────────────── */

static int spi_master_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 256 + 8,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return STM32_FLASH_ERR_SPI;
    }

    spi_device_interface_config_t dev = {
        .mode = 0,               /* CPOL=0, CPHA=0 — AN4286 requirement */
        .clock_speed_hz = SPI_SPEED_HZ,
        .spics_io_num = -1,      /* Manual CS — bootloader needs CS held low */
        .queue_size = 1,
    };

    /* Configure CS as manual GPIO output, drive LOW for entire session */
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_cfg);
    gpio_set_level(PIN_CS, 0);  /* Assert CS LOW — stays low for entire flash */

    ret = spi_bus_add_device(SPI2_HOST, &dev, &spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return STM32_FLASH_ERR_SPI;
    }

    ESP_LOGI(TAG, "SPI master initialized (2 MHz, mode 0)");
    return STM32_FLASH_OK;
}

static void spi_master_deinit(void)
{
    /* Release CS HIGH before teardown */
    gpio_set_level(PIN_CS, 1);

    if (spi_dev) {
        spi_bus_remove_device(spi_dev);
        spi_dev = NULL;
    }
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "SPI master deinitialized");
}

/* ── Enter/exit bootloader mode ───────────────────────────────── */

static void enter_bootloader(void)
{
    /* Configure BOOT0 and NRST as outputs */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_BOOT0) | (1ULL << PIN_NRST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* Assert BOOT0 HIGH, then reset STM32 */
    gpio_set_level(PIN_BOOT0, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(PIN_NRST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NRST, 1);

    /* Wait for bootloader to initialize */
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "STM32 in bootloader mode (BOOT0=1)");
}

static void exit_bootloader(void)
{
    /* Release BOOT0 LOW, reset STM32 to boot from flash */
    gpio_set_level(PIN_BOOT0, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(PIN_NRST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NRST, 1);

    /* Return BOOT0/NRST to high-Z */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_BOOT0) | (1ULL << PIN_NRST),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "STM32 reset to normal boot (BOOT0=hi-Z)");
}

/* ── Main flash routine — reads firmware from ESP32 flash partition ── */

int stm32_flash_from_partition(const esp_partition_t *part, uint32_t fw_size)
{
    int rc = STM32_FLASH_OK;

    if (fw_size > STM32_FLASH_SIZE) {
        ESP_LOGE(TAG, "Firmware too large: %lu > %d",
                 (unsigned long)fw_size, STM32_FLASH_SIZE);
        return STM32_FLASH_ERR_SIZE;
    }

    ESP_LOGI(TAG, "Starting STM32 flash: %lu bytes from partition '%s'",
             (unsigned long)fw_size, part->label);

    /* Step 1: Enter bootloader mode */
    enter_bootloader();

    /* Step 2: Init SPI master */
    rc = spi_master_init();
    if (rc != STM32_FLASH_OK) goto cleanup;

    /* Step 3: Sync with bootloader */
    if (!bl_sync()) {
        rc = STM32_FLASH_ERR_SYNC;
        goto cleanup;
    }

    /* Step 4: Mass erase */
    {
        if (!bl_erase(0xFFFF)) {
            rc = STM32_FLASH_ERR_ERASE;
            goto cleanup;
        }
    }

    /* Step 5: Write firmware in 256-byte chunks, reading from partition */
    {
        uint32_t offset = 0;
        while (offset < fw_size) {
            uint8_t buf[WRITE_CHUNK_SIZE];
            memset(buf, 0xFF, sizeof(buf));

            uint16_t to_read = WRITE_CHUNK_SIZE;
            if (offset + to_read > fw_size)
                to_read = (uint16_t)(fw_size - offset);

            esp_err_t err = esp_partition_read(part, offset, buf, to_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Partition read failed at %lu: %s",
                         (unsigned long)offset, esp_err_to_name(err));
                rc = STM32_FLASH_ERR_READ;
                goto cleanup;
            }

            if (!bl_write_memory(STM32_FLASH_BASE + offset, buf, WRITE_CHUNK_SIZE)) {
                ESP_LOGE(TAG, "Write failed at offset 0x%lx", (unsigned long)offset);
                rc = STM32_FLASH_ERR_WRITE;
                goto cleanup;
            }

            offset += WRITE_CHUNK_SIZE;

            /* Feed watchdog + log every 16KB */
            if ((offset % (16 * 1024)) == 0 || offset >= fw_size) {
                esp_task_wdt_reset();
                ESP_LOGI(TAG, "Written %lu / %lu bytes (%lu%%)",
                         (unsigned long)(offset > fw_size ? fw_size : offset),
                         (unsigned long)fw_size,
                         (unsigned long)(offset * 100 / fw_size));
            }
        }
    }

    ESP_LOGI(TAG, "Write complete, sending Go command");

    /* Step 6: Go — jump to flash start */
    if (!bl_go(STM32_FLASH_BASE)) {
        rc = STM32_FLASH_ERR_GO;
        goto cleanup;
    }

cleanup:
    spi_master_deinit();

    if (rc != STM32_FLASH_OK) {
        ESP_LOGE(TAG, "Flash failed (error %d), resetting STM32 to normal boot", rc);
    }
    exit_bootloader();

    return rc;
}
