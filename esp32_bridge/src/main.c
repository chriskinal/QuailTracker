/*
 * ESP32-C3 Bridge — SPI Slave + WiFi AP + WebSocket Server
 *
 * - SPI slave receives status data from STM32 master
 * - WiFi soft AP: SSID "QuailTracker", no password, 192.168.9.1
 * - HTTP server serves web UI
 * - WebSocket pushes live updates to browser — no polling
 *
 * Wiring (XIAO ESP32-C3):
 *   GPIO8  = SCK
 *   GPIO10 = MOSI (master out, slave in)
 *   GPIO9  = MISO (master in, slave out)
 *   GPIO5  = CS
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#define TAG "BRIDGE"

/* ── SPI Slave Config ──────────────────────────────────────────── */

#define SPI_HOST       SPI2_HOST
#define PIN_MOSI       10
#define PIN_MISO       9
#define PIN_SCK        8
#define PIN_CS         5
#define TRANSFER_SIZE  512

WORD_ALIGNED_ATTR uint8_t spi_rxBuf[TRANSFER_SIZE];
WORD_ALIGNED_ATTR uint8_t spi_txBuf[TRANSFER_SIZE];

/* Last received SPI data (for WebSocket push) */
static char last_spi_msg[TRANSFER_SIZE + 1] = "(none)";
static uint32_t spi_transaction_count = 0;
static bool new_spi_data = false;

/* ── WiFi AP Config ────────────────────────────────────────────── */

#define WIFI_SSID      "QuailTracker"
#define WIFI_CHANNEL   6
#define MAX_CONNECTIONS 4

/* ── WebSocket Client Tracking ─────────────────────────────────── */

#define MAX_WS_CLIENTS 4
static int ws_fds[MAX_WS_CLIENTS] = {0};
static httpd_handle_t ws_server = NULL;

static void ws_add_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_fds[i] == 0) {
            ws_fds[i] = fd;
            ESP_LOGI(TAG, "WS client added: fd=%d", fd);
            return;
        }
    }
}

static void ws_remove_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_fds[i] == fd) {
            ws_fds[i] = 0;
            ESP_LOGI(TAG, "WS client removed: fd=%d", fd);
            return;
        }
    }
}

static void ws_broadcast(const char *json)
{
    if (!ws_server) return;

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_fds[i] != 0) {
            esp_err_t ret = httpd_ws_send_frame_async(ws_server, ws_fds[i], &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "WS send failed fd=%d: %s", ws_fds[i], esp_err_to_name(ret));
                ws_remove_client(ws_fds[i]);
            }
        }
    }
}

/* ── Web Page (generated from src/web/index.html via xxd) ──────── */

#include "web_data.h"

/* ── HTTP + WebSocket Handlers ─────────────────────────────────── */

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)src_web_index_html, src_web_index_html_len);
    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake — track this client */
        int fd = httpd_req_to_sockfd(req);
        ws_add_client(fd);
        ESP_LOGI(TAG, "WS handshake: fd=%d", fd);
        return ESP_OK;
    }

    /* Receive frame (we don't expect client-to-server messages yet) */
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    uint8_t buf[128];
    ws_pkt.payload = buf;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS recv error: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t ws = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .is_websocket = true,
        };
        httpd_register_uri_handler(server, &ws);

        ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    }
    ws_server = server;
    return server;
}

/* ── WiFi AP Init ──────────────────────────────────────────────── */

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();

    /* Use 192.168.9.x subnet */
    esp_netif_ip_info_t ip_info;
    esp_netif_str_to_ip4("192.168.9.1", &ip_info.ip);
    esp_netif_str_to_ip4("192.168.9.1", &ip_info.gw);
    esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask);
    esp_netif_dhcps_stop(ap);
    esp_netif_set_ip_info(ap, &ip_info);
    esp_netif_dhcps_start(ap);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = MAX_CONNECTIONS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP: SSID=%s, http://192.168.9.1", WIFI_SSID);
}

/* ── STM32 Control Pins (high-Z until needed for flashing) ───── */

#define PIN_STM32_BOOT0  4
#define PIN_STM32_NRST   3

static void stm32_pins_init(void)
{
    /* Configure BOOT0 and NRST as inputs (high-impedance) so they
     * don't interfere with STM32 normal operation.  Only drive them
     * as outputs when intentionally entering bootloader mode. */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_STM32_BOOT0) | (1ULL << PIN_STM32_NRST),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "STM32 BOOT0/NRST pins set to high-Z");
}

/* ── SPI Slave Init ────────────────────────────────────────────── */

static void spi_slave_init(void)
{
    spi_bus_config_t busCfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TRANSFER_SIZE,
    };

    spi_slave_interface_config_t slaveCfg = {
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 3,
        .flags = 0,
    };

    ESP_ERROR_CHECK(spi_slave_initialize(SPI_HOST, &busCfg, &slaveCfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI slave initialized");
}

/* ── SPI Slave Task ────────────────────────────────────────────── */

static void spi_task(void *arg)
{
    while (1) {
        memset(spi_txBuf, 0, TRANSFER_SIZE);
        memcpy(spi_txBuf, "PONG", 4);
        memset(spi_rxBuf, 0, TRANSFER_SIZE);

        spi_slave_transaction_t trans = {
            .length = TRANSFER_SIZE * 8,
            .tx_buffer = spi_txBuf,
            .rx_buffer = spi_rxBuf,
        };

        esp_err_t ret = spi_slave_transmit(SPI_HOST, &trans, portMAX_DELAY);
        if (ret == ESP_OK) {
            spi_transaction_count++;

            int len = trans.trans_len / 8;
            if (len > TRANSFER_SIZE) len = TRANSFER_SIZE;
            if (len > 0 && spi_rxBuf[0] == '{') {
                /* JSON status from STM32 — store and flag for broadcast */
                memcpy(last_spi_msg, spi_rxBuf, len);
                last_spi_msg[len] = '\0';
                new_spi_data = true;
                ESP_LOGI(TAG, "SPI rx len=%d json=%d: %s", len, (int)strlen(last_spi_msg), last_spi_msg);
            }
        }
    }
}

/* ── WebSocket Push Task ───────────────────────────────────────── */

static void ws_push_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (new_spi_data) {
            new_spi_data = false;
            /* Forward the JSON status directly from STM32 to browser */
            ws_broadcast(last_spi_msg);
        }
    }
}

/* ── Main ──────────────────────────────────────────────────────── */

void app_main(void)
{
    printf("\n=== ESP32-C3 QuailTracker Bridge ===\n\n");

    /* FIRST: release STM32 BOOT0/NRST pins to high-Z */
    stm32_pins_init();

    /* Initialize NVS (required for WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    wifi_init_softap();
    start_webserver();
    spi_slave_init();

    xTaskCreate(spi_task, "spi_task", 4096, NULL, 5, NULL);
    xTaskCreate(ws_push_task, "ws_push", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Bridge ready — WiFi AP + SPI + WebSocket");
}
