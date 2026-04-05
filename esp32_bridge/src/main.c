/*
 * ESP32-C3 Bridge — BLE Beacon + SPI Slave + WiFi-on-Demand + WebSocket
 *
 * Power states:
 *   LOW_POWER:    BLE advertising only, WiFi off, SPI slave idle (~1-3mA)
 *   NORMAL_POWER: BLE + WiFi AP + SPI active, full web UI (~80mA)
 *
 * State transitions:
 *   Boot → LOW_POWER (BLE beacon starts)
 *   BLE connect OR SPI activity → NORMAL_POWER (WiFi starts, STM32 woken if sleeping)
 *   BLE disconnect + 30s timeout → LOW_POWER (WiFi stops)
 *   STM32 sends sleep command → LOW_POWER
 *
 * Wiring (ESP32-C3 Super Mini):
 *   GPIO4  = SCK
 *   GPIO6  = MOSI (master out, slave in)
 *   GPIO5  = MISO (master in, slave out)
 *   GPIO7  = CS (also used as wake output when STM32 is sleeping)
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
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "ble_beacon.h"
#include "stm32_flash.h"

#define TAG "BRIDGE"
#define ESP_FW_VERSION "0.2.0"

/* ── Power State ───────────────────────────────────────────────── */

typedef enum {
    BRIDGE_LOW_POWER,     /* BLE only, WiFi off */
    BRIDGE_NORMAL_POWER,  /* BLE + WiFi + SPI */
} bridge_power_t;

static volatile bridge_power_t bridge_state = BRIDGE_NORMAL_POWER;
static volatile uint32_t disconnect_tick = 0;   /* tick when BLE disconnected */
static volatile bool stm32_sleeping = false;     /* true = STM32 in Stop 2 */
static bool wifi_started = false;

/* WiFi duty cycle for low-power mode: on 30s, deep sleep 60s.
 * During deep sleep the ESP32 draws ~5µA instead of 44mA.
 * 30s on-time allows for ESP32 boot (~3s) + phone WiFi scan + connect.
 * Disabled by default — WiFi stays on continuously. */
static bool wifi_duty_cycle = false;
#define WIFI_DUTY_ON_MS   30000
#define WIFI_DUTY_SLEEP_S 60
bool spi_initialized = false;
static TaskHandle_t spi_task_handle = NULL;
static volatile bool spi_task_stop = false;

#define BLE_DISCONNECT_TIMEOUT_MS 30000  /* 30s before stopping WiFi */

/* ── SPI Slave Config ──────────────────────────────────────────── */

#define SPI_HOST       SPI2_HOST
#define PIN_MOSI       6
#define PIN_MISO       5
#define PIN_SCK        4
#define PIN_CS         7
#define TRANSFER_SIZE  1024

WORD_ALIGNED_ATTR uint8_t spi_rxBuf[TRANSFER_SIZE];
WORD_ALIGNED_ATTR uint8_t spi_txBuf[TRANSFER_SIZE];

/* Last received SPI data (for WebSocket push) */
static char last_spi_msg[TRANSFER_SIZE + 1] = "(none)";
static uint32_t spi_transaction_count = 0;
static bool new_spi_data = false;

/* Last known battery/state from SPI JSON (for BLE advertising) */
static uint16_t last_battery_mv = 0;
static uint8_t  last_recording = 0;
static uint8_t  last_pwr_state = 0;

/* Pending command from browser → STM32 (sent in next SPI tx) */
static char pending_cmd[512] = "";
static portMUX_TYPE cmd_mux = portMUX_INITIALIZER_UNLOCKED;

/* ── WiFi AP Config ────────────────────────────────────────────── */

#define WIFI_CHANNEL   6
#define MAX_CONNECTIONS 4

/* Device name — used for both WiFi SSID and BLE name.
 * Default: "QT_XXXX" where XXXX = last 4 hex digits of MAC.
 * Updated to station ID when learned from STM32 via SPI. */
static char device_name[32] = "";

static esp_netif_t *wifi_ap_netif = NULL;

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

static int json_get_int(const char *json, const char *key, int def);
void save_duty_cycle(void);

/* ESP32 config: GET returns JSON, POST sets values */
static esp_err_t esp_config_get_handler(httpd_req_t *req)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"dutyCycle\":%d}", wifi_duty_cycle ? 1 : 0);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t esp_config_post_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    int val = json_get_int(buf, "dutyCycle", -1);
    if (val >= 0) {
        wifi_duty_cycle = (val != 0);
        save_duty_cycle();
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* Catch-all: return 204 for everything not matched by registered handlers.
 * This satisfies ALL captive portal probes (Android, iOS, Windows) so
 * the phone accepts the connection without showing a portal popup.
 * User types any URL → DNS resolves to us → root handler serves the page. */
static esp_err_t catchall_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
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

    /* Receive command from browser */
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    uint8_t buf[512];
    ws_pkt.payload = buf;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf));
    if (ret == ESP_OK && ws_pkt.len > 0 && ws_pkt.len < sizeof(buf)) {
        buf[ws_pkt.len] = '\0';
        portENTER_CRITICAL(&cmd_mux);
        strncpy(pending_cmd, (char *)buf, sizeof(pending_cmd) - 1);
        pending_cmd[sizeof(pending_cmd) - 1] = '\0';
        portEXIT_CRITICAL(&cmd_mux);
        ESP_LOGI(TAG, "WS cmd: %s", pending_cmd);
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS recv error: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

/* ── OTA Firmware Update Handler ────────────────────────────────── */

static esp_err_t ota_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA update started, content_len=%d", req->content_len);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buf[1024];
    int received = 0;
    int total = req->content_len;

    while (received < total) {
        int ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "OTA recv error at %d/%d", received, total);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write error");
            return ESP_FAIL;
        }

        received += ret;
        if ((received % (64 * 1024)) == 0 || received == total) {
            ESP_LOGI(TAG, "OTA progress: %d/%d (%d%%)",
                     received, total, received * 100 / total);
        }
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update complete (%d bytes), rebooting...", received);
    httpd_resp_sendstr(req, "OK");

    /* Reboot after short delay to let response send */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;  /* never reached */
}

/* ── STM32 Firmware Flash Handler ──────────────────────────────── */

/* Stage 1: Receive firmware via HTTP into the "stm32fw" flash partition.
 * Stage 2: Read back from flash and write to STM32 via SPI bootloader.
 * This avoids needing 330KB+ of heap — flash is the staging buffer. */

extern bool spi_initialized;
extern void spi_slave_init(void);
static void spi_task(void *arg);

/* STM32 flash progress — updated by stm32_flash, read by status endpoint */
static volatile int stm32_flash_pct = -1;  /* -1 = not flashing */

#include "esp_partition.h"
#define STM32FW_PARTITION_LABEL "stm32fw"

static void stm32_flash_progress(int pct)
{
    stm32_flash_pct = pct;
    /* Push progress over WebSocket so browser updates in real time */
    if (ws_server) {
        char json[48];
        snprintf(json, sizeof(json), "{\"stm32flash\":%d}", pct);
        ws_broadcast(json);
    }
}

static esp_err_t flash_status_handler(httpd_req_t *req)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"pct\":%d}", stm32_flash_pct);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t stm32_ota_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "STM32 flash started, content_len=%d", req->content_len);

    if (req->content_len <= 0 || req->content_len > 512 * 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware size");
        return ESP_FAIL;
    }

    /* Find the staging partition */
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x80, STM32FW_PARTITION_LABEL);
    if (!part) {
        ESP_LOGE(TAG, "stm32fw partition not found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No staging partition");
        return ESP_FAIL;
    }

    /* Erase the staging partition */
    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Partition erase failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erase failed");
        return ESP_FAIL;
    }

    /* Stage 1: Receive firmware and write to flash partition */
    int total = req->content_len;
    int received = 0;
    char buf[1024];

    while (received < total) {
        int ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Receive error at %d/%d", received, total);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }

        err = esp_partition_write(part, received, buf, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Partition write failed at %d: %s", received, esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        received += ret;
        if ((received % (64 * 1024)) == 0 || received == total) {
            ESP_LOGI(TAG, "Staged %d / %d bytes (%d%%)",
                     received, total, received * 100 / total);
        }
    }
    ESP_LOGI(TAG, "Firmware staged (%d bytes), validating...", received);

    /* Validate STM32 firmware header: first word = initial SP (0x2000xxxx),
     * second word = reset vector (0x0800xxxx). Rejects ESP32 firmware or
     * random data before attempting to flash. */
    {
        uint8_t hdr[8];
        esp_partition_read(part, 0, hdr, 8);
        uint32_t sp = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (hdr[3] << 24);
        uint32_t rv = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24);
        if ((sp & 0xFFF00000) != 0x20000000 || (rv & 0xFFF00000) != 0x08000000) {
            ESP_LOGE(TAG, "Not a valid STM32 firmware (SP=0x%08lx RV=0x%08lx)",
                     (unsigned long)sp, (unsigned long)rv);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                "Invalid firmware — this does not appear to be an STM32 binary");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "STM32 header valid (SP=0x%08lx RV=0x%08lx)",
                 (unsigned long)sp, (unsigned long)rv);
    }

    ESP_LOGI(TAG, "Starting STM32 flash");
    stm32_flash_pct = 0;

    /* Stage 2: Stop SPI task, free SPI slave, flash STM32 from partition */
    spi_task_stop = true;
    /* Wait for SPI task to exit (it checks the flag every iteration) */
    for (int i = 0; i < 50 && spi_task_handle != NULL; i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "SPI task stopped");

    if (spi_initialized) {
        spi_slave_free(SPI_HOST);
        spi_initialized = false;
    }

    int rc = stm32_flash_from_partition(part, (uint32_t)received, stm32_flash_progress);

    /* Re-initialize SPI slave and restart task */
    spi_slave_init();
    spi_task_stop = false;
    xTaskCreate(spi_task, "spi_task", 4096, NULL, 3, &spi_task_handle);

    if (rc != STM32_FLASH_OK) {
        stm32_flash_pct = -1;
        char errmsg[64];
        snprintf(errmsg, sizeof(errmsg), "Flash failed (error %d)", rc);
        ESP_LOGE(TAG, "%s", errmsg);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, errmsg);
        return ESP_FAIL;
    }

    stm32_flash_pct = 100;
    ESP_LOGI(TAG, "STM32 flash complete, device rebooted");
    httpd_resp_sendstr(req, "OK");
    stm32_flash_pct = -1;
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    if (ws_server) return ws_server;  /* already running */

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;  /* enable wildcard URI matching */
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

        httpd_uri_t ota = {
            .uri = "/ota",
            .method = HTTP_POST,
            .handler = ota_handler,
        };
        httpd_register_uri_handler(server, &ota);

        httpd_uri_t stm32_ota = {
            .uri = "/ota_stm32",
            .method = HTTP_POST,
            .handler = stm32_ota_handler,
        };
        httpd_register_uri_handler(server, &stm32_ota);

        httpd_uri_t flash_status = {
            .uri = "/flash_status",
            .method = HTTP_GET,
            .handler = flash_status_handler,
        };
        httpd_register_uri_handler(server, &flash_status);

        httpd_uri_t esp_cfg_get = {
            .uri = "/config_esp",
            .method = HTTP_GET,
            .handler = esp_config_get_handler,
        };
        httpd_register_uri_handler(server, &esp_cfg_get);

        httpd_uri_t esp_cfg_post = {
            .uri = "/config_esp",
            .method = HTTP_POST,
            .handler = esp_config_post_handler,
        };
        httpd_register_uri_handler(server, &esp_cfg_post);

        /* Catch-all: return 204 for unknown URLs (satisfies captive portal probes).
         * Must be registered LAST — wildcard matches everything. */
        httpd_uri_t catchall = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = catchall_handler,
        };
        httpd_register_uri_handler(server, &catchall);

        ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    }
    ws_server = server;
    return server;
}

static void stop_webserver(void)
{
    if (ws_server) {
        httpd_stop(ws_server);
        ws_server = NULL;
        memset(ws_fds, 0, sizeof(ws_fds));
        ESP_LOGI(TAG, "Web server stopped");
    }
}

/* ── WiFi AP Start/Stop ────────────────────────────────────────── */

static void wifi_init_netif(void)
{
    /* One-time init of netif + event loop (can't be undone) */
    static bool netif_inited = false;
    if (!netif_inited) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        wifi_ap_netif = esp_netif_create_default_wifi_ap();

        /* Use 192.168.9.x subnet */
        esp_netif_ip_info_t ip_info;
        esp_netif_str_to_ip4("192.168.9.1", &ip_info.ip);
        esp_netif_str_to_ip4("192.168.9.1", &ip_info.gw);
        esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask);
        esp_netif_dhcps_stop(wifi_ap_netif);
        esp_netif_set_ip_info(wifi_ap_netif, &ip_info);
        esp_netif_dhcps_start(wifi_ap_netif);

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        netif_inited = true;
    }
}

static void wifi_start(void)
{
    if (wifi_started) return;

    wifi_init_netif();

    wifi_config_t wifi_config = {
        .ap = {
            .channel = WIFI_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = MAX_CONNECTIONS,
        },
    };
    strncpy((char *)wifi_config.ap.ssid, device_name, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(device_name);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_started = true;
    ESP_LOGI(TAG, "WiFi AP started: SSID=%s, http://192.168.9.1", device_name);
}

/* Load device name from NVS. Returns true if found. */
static bool load_device_name(void)
{
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(device_name);
    esp_err_t err = nvs_get_str(h, "name", device_name, &len);
    nvs_close(h);
    if (err == ESP_OK && device_name[0]) {
        ESP_LOGI(TAG, "Loaded device name from NVS: %s", device_name);
        return true;
    }
    return false;
}

/* Save device name to NVS */
static void save_device_name(void)
{
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "name", device_name);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved device name to NVS: %s", device_name);
}

/* Load WiFi duty cycle setting from NVS */
static void load_duty_cycle(void)
{
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t val = 0;
    if (nvs_get_u8(h, "duty", &val) == ESP_OK) {
        wifi_duty_cycle = (val != 0);
        ESP_LOGI(TAG, "Loaded duty cycle from NVS: %s", wifi_duty_cycle ? "ON" : "OFF");
    }
    nvs_close(h);
}

/* Save WiFi duty cycle setting to NVS */
void save_duty_cycle(void)
{
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "duty", wifi_duty_cycle ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved duty cycle to NVS: %s", wifi_duty_cycle ? "ON" : "OFF");
}

/* Update WiFi SSID and BLE name to match station ID */
static void update_device_name(const char *name)
{
    if (strcmp(device_name, name) == 0) return;  /* no change */

    strncpy(device_name, name, sizeof(device_name) - 1);
    device_name[sizeof(device_name) - 1] = '\0';

    /* Persist to NVS so next boot uses this name immediately */
    save_device_name();

    /* Update BLE advertising name */
    ble_beacon_set_name(device_name);

    /* Restart WiFi AP with new SSID */
    if (wifi_started) {
        wifi_config_t wifi_config = {
            .ap = {
                .channel = WIFI_CHANNEL,
                .authmode = WIFI_AUTH_OPEN,
                .max_connection = MAX_CONNECTIONS,
            },
        };
        strncpy((char *)wifi_config.ap.ssid, device_name, sizeof(wifi_config.ap.ssid));
        wifi_config.ap.ssid_len = strlen(device_name);
        esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        ESP_LOGI(TAG, "WiFi SSID updated: %s", device_name);
    }
}

static void wifi_stop(void)
{
    if (!wifi_started) return;

    stop_webserver();
    esp_wifi_stop();
    wifi_started = false;
    ESP_LOGI(TAG, "WiFi AP stopped");
}

/* ── STM32 Wake (pull CS pin low for 10ms) ─────────────────────── */

static void stm32_wake(void)
{
    if (!stm32_sleeping) return;

    ESP_LOGI(TAG, "Waking STM32 via GPIO5 (CS) pulse");

    /* Temporarily reconfigure GPIO5 from SPI CS to GPIO output */
    if (spi_initialized) {
        spi_slave_free(SPI_HOST);
        spi_initialized = false;
    }

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* Pull LOW for 10ms to trigger EXTI0 on STM32 */
    gpio_set_level(PIN_CS, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_CS, 1);

    /* Wait for STM32 to wake and restore SPI */
    vTaskDelay(pdMS_TO_TICKS(100));
    stm32_sleeping = false;
}

/* ── STM32 Control Pins (high-Z until needed for flashing) ───── */

#define PIN_STM32_NRST   2
#define PIN_STM32_BOOT0  3

static void stm32_pins_init(void)
{
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

/* ── SPI Slave Init/Deinit ─────────────────────────────────────── */

void spi_slave_init(void)
{
    if (spi_initialized) return;

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
    spi_initialized = true;
    ESP_LOGI(TAG, "SPI slave initialized");
}

/* Parse a simple integer value from JSON: "key":123 */
static int json_get_int(const char *json, const char *key, int def)
{
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return def;
    p += strlen(search);
    return atoi(p);
}

/* Parse a string value from JSON: "key":"value" into buf (max buflen-1 chars) */
static bool json_get_str(const char *json, const char *key, char *buf, int buflen)
{
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (!end) return false;
    int len = end - p;
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
    return true;
}

/* ── SPI Slave Task ────────────────────────────────────────────── */

static void spi_task(void *arg)
{
    while (1) {
        if (spi_task_stop) {
            ESP_LOGI(TAG, "SPI task stopping");
            spi_task_handle = NULL;
            vTaskDelete(NULL);
            return;
        }
        if (!spi_initialized) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        memset(spi_txBuf, 0, TRANSFER_SIZE);
        portENTER_CRITICAL(&cmd_mux);
        if (pending_cmd[0]) {
            strncpy((char *)spi_txBuf, pending_cmd, TRANSFER_SIZE - 1);
            pending_cmd[0] = '\0';
        } else {
            snprintf((char *)spi_txBuf, TRANSFER_SIZE,
                     "{\"espFw\":\"%s\"}", ESP_FW_VERSION);
        }
        portEXIT_CRITICAL(&cmd_mux);
        memset(spi_rxBuf, 0, TRANSFER_SIZE);

        spi_slave_transaction_t trans = {
            .length = TRANSFER_SIZE * 8,
            .tx_buffer = spi_txBuf,
            .rx_buffer = spi_rxBuf,
        };

        esp_err_t ret = spi_slave_transmit(SPI_HOST, &trans, pdMS_TO_TICKS(1000));
        vTaskDelay(pdMS_TO_TICKS(1));  /* yield to lower-priority tasks */
        if (ret == ESP_OK) {
            spi_transaction_count++;

            int len = trans.trans_len / 8;
            if (len > TRANSFER_SIZE) len = TRANSFER_SIZE;
            if (len > 0 && spi_rxBuf[0] == '{') {
                /* JSON status from STM32 — store and flag for broadcast */
                memcpy(last_spi_msg, spi_rxBuf, len);
                last_spi_msg[len] = '\0';
                new_spi_data = true;

                /* Extract battery/state for BLE advertising */
                last_battery_mv = (uint16_t)json_get_int(last_spi_msg, "bat", 0);
                last_recording = (uint8_t)json_get_int(last_spi_msg, "rec", 0);
                last_pwr_state = (uint8_t)json_get_int(last_spi_msg, "pwrState", 0);

                /* Update device name from station ID if changed */
                {
                    char station[16];
                    if (json_get_str(last_spi_msg, "station", station, sizeof(station))
                        && station[0]) {
                        update_device_name(station);
                    }
                }

                /* Track STM32 sleep state for wake functionality */
                if (json_get_int(last_spi_msg, "sleeping", -1) == 1) {
                    stm32_sleeping = true;
                }

                /* SPI rx logging removed — runs 4x/sec, too noisy */
            }
        }
    }
}

/* ── WebSocket Push Task ───────────────────────────────────────── */

static void ws_push_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (new_spi_data && ws_server) {
            new_spi_data = false;
            /* Inject ESP32 firmware version into JSON before broadcasting.
             * Replace trailing '}' with ',"espFw":"x.y.z"}' */
            int len = strlen(last_spi_msg);
            if (len > 1 && last_spi_msg[len - 1] == '}') {
                snprintf(last_spi_msg + len - 1,
                         sizeof(last_spi_msg) - len + 1,
                         ",\"espFw\":\"%s\"}", ESP_FW_VERSION);
            }
            ws_broadcast(last_spi_msg);
        }

        /* Update BLE advertising data periodically */
        ble_beacon_update_data(last_battery_mv, last_recording, last_pwr_state);
    }
}

/* ── BLE Callbacks ─────────────────────────────────────────────── */

static void on_ble_connect(void)
{
    ESP_LOGI(TAG, "BLE client connected — transitioning to NORMAL_POWER");
    bridge_state = BRIDGE_NORMAL_POWER;
    disconnect_tick = 0;

    /* Wake STM32 if it's sleeping */
    if (stm32_sleeping) {
        stm32_wake();
    }
}

static void on_ble_disconnect(void)
{
    ESP_LOGI(TAG, "BLE client disconnected — starting 30s timeout");
    disconnect_tick = xTaskGetTickCount();
}

/* ── Power Management Task ─────────────────────────────────────── */

/* ── Captive Portal DNS Server ──────────────────────────────────── */

/* Minimal DNS server: responds to ALL queries with 192.168.9.1.
 * This makes the phone think it needs to "log in", triggering the
 * captive portal popup which loads our web UI automatically. */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS: socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in saddr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        ESP_LOGE(TAG, "DNS: bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS captive portal started on port 53");

    uint8_t buf[512];
    while (1) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client, &clen);
        if (len < 12) continue;  /* too short for DNS header */

        /* Build response: copy query, set response flags, append answer */
        uint8_t resp[512];
        memcpy(resp, buf, len);

        /* Set QR=1 (response), AA=1 (authoritative), RA=1 */
        resp[2] = 0x84;  /* QR=1, Opcode=0, AA=1, TC=0, RD=0 */
        resp[3] = 0x00;  /* RA=0, Z=0, RCODE=0 (no error) */

        /* Answer count = 1 */
        resp[6] = 0x00;
        resp[7] = 0x01;

        /* Append answer: pointer to name in query + type A + class IN + TTL + IP */
        int pos = len;
        resp[pos++] = 0xC0;  /* pointer to name at offset 12 */
        resp[pos++] = 0x0C;
        resp[pos++] = 0x00; resp[pos++] = 0x01;  /* type A */
        resp[pos++] = 0x00; resp[pos++] = 0x01;  /* class IN */
        resp[pos++] = 0x00; resp[pos++] = 0x00;
        resp[pos++] = 0x00; resp[pos++] = 0x0A;  /* TTL = 10s */
        resp[pos++] = 0x00; resp[pos++] = 0x04;  /* data length = 4 */
        resp[pos++] = 192;                        /* 192.168.9.1 */
        resp[pos++] = 168;
        resp[pos++] = 9;
        resp[pos++] = 1;

        sendto(sock, resp, pos, 0, (struct sockaddr *)&client, clen);
    }
}

/* ── Power Management Task ─────────────────────────────────────── */

static void power_mgmt_task(void *arg)
{
    uint32_t duty_cycle_tick = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* Check if any WiFi clients are connected */
        wifi_sta_list_t sta_list;
        int wifi_clients = 0;
        if (wifi_started && esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
            wifi_clients = sta_list.num;
        }

        /* Reset duty cycle timer while client is connected */
        if (wifi_clients > 0) {
            duty_cycle_tick = xTaskGetTickCount();
        }

        /* WiFi duty cycle: on 20s, then deep sleep 40s.
         * Deep sleep draws ~5µA vs 44mA with WiFi off + CPU idle.
         * On wake, ESP32 reboots — WiFi/NVS/BLE all reinitialize. */
        if (wifi_duty_cycle && wifi_clients == 0) {
            uint32_t elapsed = (xTaskGetTickCount() - duty_cycle_tick) * portTICK_PERIOD_MS;
            if (elapsed >= WIFI_DUTY_ON_MS) {
                ESP_LOGI(TAG, "Duty cycle: entering deep sleep for %ds", WIFI_DUTY_SLEEP_S);
                esp_deep_sleep(WIFI_DUTY_SLEEP_S * 1000000ULL);
                /* Does not return — ESP32 reboots on wake */
            }
        }
    }
}

/* ── Main ──────────────────────────────────────────────────────── */

void app_main(void)
{
    printf("\n=== ESP32-C3 QuailTracker Bridge v2 ===\n\n");

    /* FIRST: release STM32 BOOT0/NRST pins to high-Z */
    stm32_pins_init();

    /* Initialize NVS (required for WiFi + BLE) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Load device name: NVS first, then fall back to MAC-based default */
    if (!load_device_name()) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(device_name, sizeof(device_name), "QT_%02X%02X", mac[4], mac[5]);
        ESP_LOGI(TAG, "No saved name, using default: %s", device_name);
    }

    load_duty_cycle();

    /* Initialize network interface (needed before WiFi or BLE) */
    wifi_init_netif();

    /* Start BLE beacon (always active for device discovery) */
    ble_beacon_init(device_name, on_ble_connect, on_ble_disconnect);

    /* Start in NORMAL_POWER on boot so WiFi AP is available for setup.
     * Will transition to LOW_POWER after BLE disconnect timeout if no
     * user is connected, or when STM32 sends sleep command. */
    bridge_state = BRIDGE_NORMAL_POWER;
    wifi_start();
    start_webserver();
    spi_slave_init();

    xTaskCreate(spi_task, "spi_task", 4096, NULL, 3, &spi_task_handle);
    xTaskCreate(ws_push_task, "ws_push", 4096, NULL, 4, NULL);
    xTaskCreate(power_mgmt_task, "pwr_mgmt", 4096, NULL, 2, NULL);
    xTaskCreate(dns_task, "dns", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "Bridge ready — BLE Beacon + WiFi AP + SPI + WebSocket");
}
