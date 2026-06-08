/*
 * ESP32-C3 Bridge — SPI Slave + WiFi AP + WebSocket
 *
 * WiFi AP is on continuously by default. Two opt-in power-saving modes
 * (mutually exclusive, configured via the web UI / NVS):
 *   - WiFi duty cycle: 30 s on / 60 s deep sleep
 *   - Laser wake:      sleep until phototransistor on GPIO0 fires
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
#include "nvs.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "stm32_flash.h"
#include "spi_protocol.h"

#define TAG "BRIDGE"
#define ESP_FW_VERSION "0.5.0"

static bool wifi_started = false;

/* WiFi duty cycle for low-power mode: on 30s, deep sleep 60s.
 * During deep sleep the ESP32 draws ~5µA instead of 44mA.
 * 30s on-time allows for ESP32 boot (~3s) + phone WiFi scan + connect.
 * Disabled by default — WiFi stays on continuously. */
static bool wifi_duty_cycle = false;
static bool laser_wake_enabled = false;
#define WIFI_DUTY_ON_MS    30000
#define WIFI_DUTY_SLEEP_S  60
#define LASER_IDLE_SLEEP_S 30   /* seconds idle before deep sleep in laser wake mode */
#define BOOT_GRACE_MS      180000  /* 3 min always-on after boot for WiFi setup */
#define CLIENT_GRACE_MS    60000   /* keep AP alive 60s after last client disconnect */
#define PIN_LASER_WAKE     0   /* GPIO0: phototransistor for laser wake */
bool spi_initialized = false;
static TaskHandle_t spi_task_handle = NULL;
static volatile bool spi_task_stop = false;

/* ── SPI Slave Config ──────────────────────────────────────────── */

#define SPI_HOST       SPI2_HOST
#define PIN_MOSI       6
#define PIN_MISO       5
#define PIN_SCK        4
#define PIN_CS         7
#define TRANSFER_SIZE  1024

WORD_ALIGNED_ATTR uint8_t spi_rxBuf[TRANSFER_SIZE];
WORD_ALIGNED_ATTR uint8_t spi_txBuf[TRANSFER_SIZE];

/* Last received SPI data — binary state + config cache */
static uint32_t spi_transaction_count = 0;
static bool new_spi_data = false;

/* Local binary caches (populated from SPI RX frames) */
static device_config_t local_cfg;
static spi_state_t     local_state;
static uint32_t        local_cfg_seq = 0;
static bool            stm32_synced = false;
static bool            first_boot = true;

/* Monotonic µs of the last valid SPI frame from the STM32 — drives liveness /
 * auto-recovery. 0 = never seen since boot. */
static volatile int64_t last_spi_ok_us = 0;

/* Monotonic µs until which the STM32's SPI silence is EXPECTED because it told us
 * (pwr_sleepSecs) it is in scheduled Stop 2 sleep. While now < this, the self-heal
 * watchdog must NOT treat silence as a dead STM. 0 = STM is awake / no notice. */
static volatile int64_t stm32_sleep_until_us = 0;
#define STM32_WD_SLEEP_MARGIN_S 120  /* grace past a scheduled-sleep window for the STM to wake + re-report */

/* Refuse to start a flash below this battery level so an update can always
 * finish. 0 = battery unknown (no STM32 telemetry yet) → allowed. */
#define FLASH_MIN_BATT_MV  3500

/* ── Staged-image metadata (NVS) for self-healing recovery ─────────
 * The stm32fw partition retains the last staged image; we persist its size +
 * CRC so the watchdog can confirm integrity before a recovery re-flash. */
static void staged_meta_save(uint32_t size, uint32_t crc)
{
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "stmfw_sz", size);
    nvs_set_u32(h, "stmfw_crc", crc);
    nvs_commit(h);
    nvs_close(h);
}

static bool staged_meta_load(uint32_t *size, uint32_t *crc)
{
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = (nvs_get_u32(h, "stmfw_sz", size) == ESP_OK) &&
              (nvs_get_u32(h, "stmfw_crc", crc) == ESP_OK);
    nvs_close(h);
    return ok && *size > 0;
}

/* ── Audio streaming state ────────────────────────────────────── */

#define AUDIO_RING_SIZE 4096   /* ~256ms at 8kHz */
#define AUDIO_RING_MASK (AUDIO_RING_SIZE - 1)
static int16_t audio_ring[AUDIO_RING_SIZE];
static volatile uint32_t audio_ring_head = 0;
static volatile uint32_t audio_ring_tail = 0;
static volatile bool audio_streaming = false;

/* Audio WebSocket clients (separate from status WS) */
#define MAX_AUDIO_CLIENTS 2
static int audio_ws_fds[MAX_AUDIO_CLIENTS] = {0};

static void audio_ws_add(int fd) {
    for (int i = 0; i < MAX_AUDIO_CLIENTS; i++) {
        if (audio_ws_fds[i] == 0) { audio_ws_fds[i] = fd; return; }
    }
}

static void audio_ws_remove(int fd) {
    for (int i = 0; i < MAX_AUDIO_CLIENTS; i++) {
        if (audio_ws_fds[i] == fd) { audio_ws_fds[i] = 0; return; }
    }
}

static int audio_ws_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_AUDIO_CLIENTS; i++)
        if (audio_ws_fds[i] != 0) n++;
    return n;
}

/* Command queue: browser → SPI (ring buffer, 8 deep) */
#define CMD_QUEUE_SIZE 8
static qt_spi_cmd_t cmd_queue[CMD_QUEUE_SIZE];
static volatile uint8_t cmd_head = 0, cmd_tail = 0;
static portMUX_TYPE cmd_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t cmd_seq_counter = 0;

static void cmd_enqueue(uint8_t cmd_type, const uint8_t *payload, uint8_t payload_len)
{
    portENTER_CRITICAL(&cmd_mux);
    uint8_t next = (cmd_head + 1) % CMD_QUEUE_SIZE;
    if (next != cmd_tail) {  /* not full */
        memset(&cmd_queue[cmd_head], 0, sizeof(qt_spi_cmd_t));
        cmd_queue[cmd_head].cmd = cmd_type;
        cmd_queue[cmd_head].seq = ++cmd_seq_counter;
        if (payload && payload_len > 0) {
            if (payload_len > 56) payload_len = 56;
            memcpy(cmd_queue[cmd_head].payload, payload, payload_len);
        }
        cmd_head = next;
    }
    portEXIT_CRITICAL(&cmd_mux);
}

static bool cmd_dequeue(qt_spi_cmd_t *out)
{
    portENTER_CRITICAL(&cmd_mux);
    if (cmd_tail == cmd_head) {
        portEXIT_CRITICAL(&cmd_mux);
        return false;
    }
    memcpy(out, &cmd_queue[cmd_tail], sizeof(qt_spi_cmd_t));
    cmd_tail = (cmd_tail + 1) % CMD_QUEUE_SIZE;
    portEXIT_CRITICAL(&cmd_mux);
    return true;
}

/* Legacy JSON helpers — kept for parsing config from browser */

/* ── WiFi AP Config ────────────────────────────────────────────── */

#define MAX_CONNECTIONS 4

/* Pick a non-overlapping 2.4 GHz channel from {1, 6, 11} based on the
 * SoftAP MAC. Deterministic per device, spreads units across channels
 * without an RF survey. */
static uint8_t wifi_pick_channel(void)
{
    static uint8_t cached = 0;
    if (cached) return cached;
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK) {
        static const uint8_t ch_set[] = {1, 6, 11};
        /* XOR only the NIC-unique bytes; OUI is constant across Espressif parts. */
        uint8_t h = mac[3] ^ mac[4] ^ mac[5];
        cached = ch_set[h % 3];
    } else {
        cached = 6;
    }
    return cached;
}

/* Device name — used as the WiFi SSID.
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
                ws_remove_client(ws_fds[i]);
            }
        }
    }
}

/* ── Web Page (generated from src/web/index.html via xxd) ──────── */

#include "web_data.h"

/* ── HTTP + WebSocket Handlers ─────────────────────────────────── */

/* Called when any HTTP/WS socket closes — clean up stale WS clients */
static void on_sock_close(httpd_handle_t hd, int sockfd)
{
    ws_remove_client(sockfd);
    audio_ws_remove(sockfd);
    /* Auto-stop streaming when last audio client disconnects */
    if (audio_streaming && audio_ws_count() == 0) {
        uint8_t payload[2] = {0, 0};  /* channel=0, enable=0 */
        cmd_enqueue(SPI_CMD_AUDIO_STREAM, payload, 2);
        audio_streaming = false;
        ESP_LOGI(TAG, "Audio: auto-stopped (last client gone)");
    }
    close(sockfd);
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)src_web_index_html, src_web_index_html_len);
    return ESP_OK;
}

static int json_get_int(const char *json, const char *key, int def);
void save_duty_cycle(void);
void save_laser_wake(void);

/* State API: returns current config + state as JSON for initial page load */
static esp_err_t api_state_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    /* Build a compact JSON with key fields for initial UI population */
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"synced\":%d,\"station\":\"%s\",\"gain\":%d,\"fmt\":%d,"
        "\"bat\":%u,\"rec\":%d,\"pwrState\":%d}",
        (int)stm32_synced, local_cfg.stationId,
        (int)local_cfg.gain, (int)local_cfg.recFormat,
        (unsigned)local_state.env_battMv,
        (int)local_state.rec_active,
        (int)local_state.pwr_state);
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* ESP32 config: GET returns JSON, POST sets values */
static esp_err_t esp_config_get_handler(httpd_req_t *req)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"dutyCycle\":%d,\"laserWake\":%d}",
             wifi_duty_cycle ? 1 : 0, laser_wake_enabled ? 1 : 0);
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
        if (wifi_duty_cycle) laser_wake_enabled = false;  /* mutually exclusive */
        save_duty_cycle();
    }
    val = json_get_int(buf, "laserWake", -1);
    if (val >= 0) {
        laser_wake_enabled = (val != 0);
        if (laser_wake_enabled) wifi_duty_cycle = false;  /* mutually exclusive */
        save_laser_wake();
        save_duty_cycle();
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* iOS captive-portal probe handler. iOS hits captive.apple.com/hotspot-detect.html
 * (DNS-hijacked to us) and expects the EXACT body below with 200 OK to consider
 * the network "has internet". Anything else → iOS treats it as a flaky/captive
 * network and aggressively drops it after a few minutes, even when the user is
 * actively browsing. Returning Success here keeps iOS happy. */
static esp_err_t ios_success_handler(httpd_req_t *req)
{
    static const char body[] =
        "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, body, sizeof(body) - 1);
    return ESP_OK;
}

/* Catch-all: return 204 for everything not matched by registered handlers.
 * Android and Windows are happy with 204; iOS uses the dedicated handler above. */
static esp_err_t catchall_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Map browser JSON command string to SPI command enum + config update */
static void ws_process_command(const char *json)
{
    ESP_LOGI(TAG, "WS cmd: %s", json);

    /* Action commands → enqueue as SPI commands */
    if (strstr(json, "rec_toggle"))     { cmd_enqueue(SPI_CMD_REC_TOGGLE, NULL, 0); return; }
    if (strstr(json, "sd_mount"))       { cmd_enqueue(SPI_CMD_SD_MOUNT, NULL, 0); return; }
    if (strstr(json, "sd_eject"))       { cmd_enqueue(SPI_CMD_SD_EJECT, NULL, 0); return; }
    if (strstr(json, "sd_format"))      { cmd_enqueue(SPI_CMD_SD_FORMAT, NULL, 0); return; }
    if (strstr(json, "survey_start"))   { cmd_enqueue(SPI_CMD_SURVEY_START, NULL, 0); return; }
    if (strstr(json, "survey_clear"))   { cmd_enqueue(SPI_CMD_SURVEY_CLEAR, NULL, 0); return; }
    if (strstr(json, "schedule_on"))    { cmd_enqueue(SPI_CMD_SCHEDULE_ON, NULL, 0); return; }
    if (strstr(json, "schedule_off"))   { cmd_enqueue(SPI_CMD_SCHEDULE_OFF, NULL, 0); return; }
    if (strstr(json, "dev_mode"))       { cmd_enqueue(SPI_CMD_DEV_MODE, NULL, 0); return; }
    if (strstr(json, "model_reload"))   { cmd_enqueue(SPI_CMD_MODEL_RELOAD, NULL, 0); return; }

    /* TZ refresh from browser. Heartbeat-style — updates RAM only, doesn't
     * bump cfg_seq, doesn't persist. STM32 picks up via SPI_CMD_SET_TZ; we
     * also patch local_cfg so the next round-trip carries it back to STM32
     * if it loses the RAM copy on reboot. */
    if (strstr(json, "update_tz")) {
        char *p;
        qt_spi_tz_payload_t tz = {0};
        if ((p = strstr(json, "\"utcOff\":")) != NULL)
            tz.utcOffsetMin = (int16_t)atoi(p + 9);
        if ((p = strstr(json, "\"nextOff\":")) != NULL)
            tz.nextOffsetMin = (int16_t)atoi(p + 10);
        if ((p = strstr(json, "\"nextTr\":")) != NULL)
            tz.nextTransitionUtc = (uint32_t)strtoul(p + 9, NULL, 10);
        local_cfg.utcOffsetMin = tz.utcOffsetMin;
        local_cfg.nextOffsetMin = tz.nextOffsetMin;
        local_cfg.nextTransitionUtc = tz.nextTransitionUtc;
        cmd_enqueue(SPI_CMD_SET_TZ, (const uint8_t *)&tz, sizeof(tz));
        return;
    }

    if (strstr(json, "audio_stream")) {
        char *p;
        uint8_t ch = 0, en = 1;
        if ((p = strstr(json, "\"ch\":")) != NULL)
            ch = (uint8_t)atoi(p + 5);
        if ((p = strstr(json, "\"en\":")) != NULL)
            en = (uint8_t)atoi(p + 5);
        uint8_t payload[2] = {ch, en};
        cmd_enqueue(SPI_CMD_AUDIO_STREAM, payload, 2);
        audio_streaming = (en != 0);
        if (!audio_streaming) {
            /* Reset ring buffer on stop */
            audio_ring_head = 0;
            audio_ring_tail = 0;
        }
        ESP_LOGI(TAG, "Audio stream: ch=%d en=%d", ch, en);
        return;
    }

    /* Config changes → update local_cfg + bump seq so STM32 adopts */
    if (strstr(json, "set_config")) {
        char *p;
        if ((p = strstr(json, "\"stId\":\"")) != NULL) {
            p += 8;
            const char *end = strchr(p, '"');
            if (end) {
                int len = end - p;
                if (len > 15) len = 15;
                memcpy(local_cfg.stationId, p, len);
                local_cfg.stationId[len] = '\0';
            }
        }
        if ((p = strstr(json, "\"gain\":")) != NULL)
            local_cfg.gain = (uint8_t)atoi(p + 7);
        if ((p = strstr(json, "\"fmt\":")) != NULL)
            local_cfg.recFormat = (uint8_t)atoi(p + 6);
        if ((p = strstr(json, "\"hpf\":")) != NULL)
            local_cfg.bpfLow = (uint16_t)atoi(p + 6);
        if ((p = strstr(json, "\"lpf\":")) != NULL)
            local_cfg.bpfHigh = (uint16_t)atoi(p + 6);
        if ((p = strstr(json, "\"chunk\":")) != NULL)
            local_cfg.chunkMinutes = (uint8_t)atoi(p + 8);
        if ((p = strstr(json, "\"trigEn\":")) != NULL)
            local_cfg.trigEnabled = (uint8_t)atoi(p + 9);
        if ((p = strstr(json, "\"trigDb\":")) != NULL)
            local_cfg.trigDb = (int8_t)atoi(p + 9);
        if ((p = strstr(json, "\"trigPre\":")) != NULL)
            local_cfg.trigPre = (uint8_t)atoi(p + 10);
        if ((p = strstr(json, "\"trigPost\":")) != NULL)
            local_cfg.trigPost = (uint8_t)atoi(p + 11);
        if ((p = strstr(json, "\"lowBat\":")) != NULL)
            local_cfg.lowBatPct = (uint8_t)atoi(p + 9);
        if ((p = strstr(json, "\"autoStop\":")) != NULL)
            local_cfg.autoStop = (uint8_t)atoi(p + 11);
        if ((p = strstr(json, "\"micHdg\":")) != NULL)
            local_cfg.micHeading = (uint16_t)atoi(p + 9);

        local_cfg_seq++;
        local_cfg.cfg_seq = local_cfg_seq;
        ESP_LOGI(TAG, "Config updated (seq=%lu)", (unsigned long)local_cfg_seq);
        return;
    }

    if (strstr(json, "set_detect")) {
        char *p;
        if ((p = strstr(json, "\"mission\":")) != NULL)
            local_cfg.missionMode = (uint8_t)atoi(p + 10);
        if ((p = strstr(json, "\"conf\":")) != NULL)
            local_cfg.detConfThresh = (uint8_t)atoi(p + 7);
        if ((p = strstr(json, "\"winStep\":")) != NULL)
            local_cfg.detWindowStep = (uint8_t)atoi(p + 10);

        local_cfg_seq++;
        local_cfg.cfg_seq = local_cfg_seq;
        cmd_enqueue(SPI_CMD_SET_DETECT, NULL, 0);
        return;
    }

    if (strstr(json, "set_schedule")) {
        char *p;
        if ((p = strstr(json, "\"sunEn\":")) != NULL)
            local_cfg.sunriseEnabled = (uint8_t)atoi(p + 8);
        if ((p = strstr(json, "\"sunB\":")) != NULL)
            local_cfg.sunriseBefore = (uint16_t)atoi(p + 7);
        if ((p = strstr(json, "\"sunA\":")) != NULL)
            local_cfg.sunriseAfter = (uint16_t)atoi(p + 7);
        if ((p = strstr(json, "\"setEn\":")) != NULL)
            local_cfg.sunsetEnabled = (uint8_t)atoi(p + 8);
        if ((p = strstr(json, "\"setB\":")) != NULL)
            local_cfg.sunsetBefore = (uint16_t)atoi(p + 7);
        if ((p = strstr(json, "\"setA\":")) != NULL)
            local_cfg.sunsetAfter = (uint16_t)atoi(p + 7);
        if ((p = strstr(json, "\"nWin\":")) != NULL)
            local_cfg.numWindows = (uint8_t)atoi(p + 7);
        if ((p = strstr(json, "\"wins\":[")) != NULL) {
            p += 8;
            for (int i = 0; i < local_cfg.numWindows * 2 && i < 16; i++) {
                local_cfg.windows[i] = (uint16_t)atoi(p);
                char *next = strchr(p, ',');
                if (next) p = next + 1; else break;
            }
        }

        /* Capture browser TZ alongside the schedule save so the persisted
         * cfg matches the windows the user just typed. update_tz can refresh
         * these later between saves without bumping cfg_seq. */
        if ((p = strstr(json, "\"utcOff\":")) != NULL)
            local_cfg.utcOffsetMin = (int16_t)atoi(p + 9);
        if ((p = strstr(json, "\"nextOff\":")) != NULL)
            local_cfg.nextOffsetMin = (int16_t)atoi(p + 10);
        if ((p = strstr(json, "\"nextTr\":")) != NULL)
            local_cfg.nextTransitionUtc = (uint32_t)strtoul(p + 9, NULL, 10);

        local_cfg_seq++;
        local_cfg.cfg_seq = local_cfg_seq;
        return;
    }
}

static volatile bool stm32_wake_pending = false;  /* set from any task; spi_task drains */

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake — track this client */
        int fd = httpd_req_to_sockfd(req);
        ws_add_client(fd);
        ESP_LOGI(TAG, "WS handshake: fd=%d — queuing STM32 wake", fd);
        /* Don't call stm32_wake() inline here. It does spi_slave_free()
         * + 10ms vTaskDelay, which blocks the HTTP server response and
         * races with spi_task. iOS sees the WS handshake stall, aborts
         * the connection, and drops the AP. Defer to spi_task instead. */
        stm32_wake_pending = true;
        return ESP_OK;
    }

    /* Receive command from browser — two-step: get length, then payload. */
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS recv header error: %s", esp_err_to_name(ret));
        return ESP_OK;
    }
    if (ws_pkt.len == 0 || ws_pkt.len >= 512) {
        ESP_LOGW(TAG, "WS frame len=%d (skip)", (int)ws_pkt.len);
        return ESP_OK;
    }

    uint8_t *buf = calloc(1, ws_pkt.len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "WS malloc fail (%d bytes)", (int)ws_pkt.len);
        return ESP_OK;
    }
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret == ESP_OK && ws_pkt.len > 0) {
        buf[ws_pkt.len] = '\0';
        ws_process_command((char *)buf);
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS recv payload error: %s", esp_err_to_name(ret));
    }
    free(buf);
    return ESP_OK;
}

/* ── OTA Firmware Update Handler ────────────────────────────────── */

static esp_err_t ota_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA update started, content_len=%d", req->content_len);

    /* Read first chunk and validate ESP32 image magic byte (0xE9) */
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        ws_broadcast("{\"otaErr\":\"No data received\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }
    if ((uint8_t)buf[0] != 0xE9) {
        ESP_LOGE(TAG, "Not ESP32 firmware (magic=0x%02X, expected 0xE9)", (uint8_t)buf[0]);
        ws_broadcast("{\"otaErr\":\"Invalid firmware — not an ESP32 binary\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ws_broadcast("{\"otaErr\":\"No OTA partition\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        ws_broadcast("{\"otaErr\":\"OTA begin failed\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    /* Write first chunk that we already read */
    err = esp_ota_write(ota_handle, buf, ret);
    if (err != ESP_OK) {
        esp_ota_abort(ota_handle);
        ws_broadcast("{\"otaErr\":\"Write error\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }
    int received = ret;
    int total = req->content_len;

    /* Continue receiving and writing */
    while (received < total) {
        ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(ota_handle);
            ws_broadcast("{\"otaErr\":\"Receive error\"}");
            httpd_resp_sendstr(req, "ERR");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, ret);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            ws_broadcast("{\"otaErr\":\"Write error\"}");
            httpd_resp_sendstr(req, "ERR");
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
        ws_broadcast("{\"otaErr\":\"Firmware validation failed\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        ws_broadcast("{\"otaErr\":\"Set boot partition failed\"}");
        httpd_resp_sendstr(req, "ERR");
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

/* Stop the SPI slave task, flash the STM32 from `part`, restart SPI.
 * Shared by the web OTA handler and the auto-recovery watchdog. */
static int do_stm32_flash(const esp_partition_t *part, uint32_t size)
{
    stm32_flash_pct = 0;

    spi_task_stop = true;
    for (int i = 0; i < 50 && spi_task_handle != NULL; i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "SPI task stopped for flash");

    if (spi_initialized) {
        spi_slave_free(SPI_HOST);
        spi_initialized = false;
    }

    int rc = stm32_flash_from_partition(part, size, stm32_flash_progress);

    /* Re-initialize SPI slave and restart task */
    spi_slave_init();
    spi_task_stop = false;
    xTaskCreate(spi_task, "spi_task", 4096, NULL, 3, &spi_task_handle);

    /* Grant the freshly-flashed image a check-in grace window: stamp "now" so
     * the watchdog waits DEAD_US for it to talk before judging it dead. A good
     * image checks in within ~seconds (refreshing this); a bad one never does
     * and goes stale → recovery retry. */
    last_spi_ok_us = esp_timer_get_time();
    stm32_flash_pct = (rc == STM32_FLASH_OK) ? 100 : -1;
    return rc;
}

static esp_err_t stm32_ota_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "STM32 flash started, content_len=%d", req->content_len);

    /* Battery gate: refuse to start if we know the battery is too low to finish.
     * env_battMv == 0 means no telemetry yet (e.g. bench) → allow. */
    if (local_state.env_battMv != 0 && local_state.env_battMv < FLASH_MIN_BATT_MV) {
        ESP_LOGW(TAG, "Flash refused: battery %u mV < %d mV",
                 local_state.env_battMv, FLASH_MIN_BATT_MV);
        ws_broadcast("{\"stm32Err\":\"Battery too low to flash safely\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    if (req->content_len <= 0 || req->content_len > 512 * 1024) {
        ws_broadcast("{\"stm32Err\":\"Invalid firmware size\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    /* Read first chunk and validate STM32 header before doing anything */
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret < 8) {
        ws_broadcast("{\"stm32Err\":\"No data received\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    /* Validate: SP = 0x2000xxxx, reset vector = 0x0800xxxx */
    uint32_t sp = (uint8_t)buf[0] | ((uint8_t)buf[1] << 8) |
                  ((uint8_t)buf[2] << 16) | ((uint8_t)buf[3] << 24);
    uint32_t rv = (uint8_t)buf[4] | ((uint8_t)buf[5] << 8) |
                  ((uint8_t)buf[6] << 16) | ((uint8_t)buf[7] << 24);
    if ((sp & 0xFFF00000) != 0x20000000 || (rv & 0xFFF00000) != 0x08000000) {
        ESP_LOGE(TAG, "Not STM32 firmware (SP=0x%08lx RV=0x%08lx)",
                 (unsigned long)sp, (unsigned long)rv);
        ws_broadcast("{\"stm32Err\":\"Invalid firmware — not an STM32 binary\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    /* Find the staging partition */
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x80, STM32FW_PARTITION_LABEL);
    if (!part) {
        ws_broadcast("{\"stm32Err\":\"No staging partition\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    /* Erase the staging partition */
    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        ws_broadcast("{\"stm32Err\":\"Partition erase failed\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }

    /* Write first chunk that we already read */
    err = esp_partition_write(part, 0, buf, ret);
    if (err != ESP_OK) {
        ws_broadcast("{\"stm32Err\":\"Write failed\"}");
        httpd_resp_sendstr(req, "ERR");
        return ESP_FAIL;
    }
    int received = ret;
    int total = req->content_len;

    /* Stage 1: Continue receiving firmware and writing to flash partition */
    while (received < total) {
        ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ws_broadcast("{\"stm32Err\":\"Receive error\"}");
            httpd_resp_sendstr(req, "ERR");
            return ESP_FAIL;
        }

        err = esp_partition_write(part, received, buf, ret);
        if (err != ESP_OK) {
            ws_broadcast("{\"stm32Err\":\"Write failed\"}");
            httpd_resp_sendstr(req, "ERR");
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
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_sendstr(req, "Invalid firmware — not an STM32 binary");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "STM32 header valid (SP=0x%08lx RV=0x%08lx)",
                 (unsigned long)sp, (unsigned long)rv);
    }

    /* Persist staged-image metadata BEFORE flashing, so if this flash is
     * interrupted (power loss) the watchdog can recover from the retained
     * partition on next boot. */
    {
        uint32_t crc = stm32_flash_crc32(part, (uint32_t)received);
        staged_meta_save((uint32_t)received, crc);
        ESP_LOGI(TAG, "Staged image meta saved (size=%d crc=0x%08lx)",
                 received, (unsigned long)crc);
    }

    ESP_LOGI(TAG, "Starting STM32 flash");

    int rc = do_stm32_flash(part, (uint32_t)received);

    if (rc != STM32_FLASH_OK) {
        char errmsg[64];
        snprintf(errmsg, sizeof(errmsg), "Flash failed (error %d)", rc);
        ESP_LOGE(TAG, "%s", errmsg);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, errmsg);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "STM32 flash complete, device rebooted");
    httpd_resp_sendstr(req, "OK");
    stm32_flash_pct = -1;
    return ESP_OK;
}


/* ── Audio WebSocket Handler ────────────────────────────────────── */

static esp_err_t ws_audio_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        audio_ws_add(fd);
        ESP_LOGI(TAG, "Audio WS handshake: fd=%d", fd);
        return ESP_OK;
    }

    /* Receive command from browser (start/stop/channel change) */
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0 || ws_pkt.len >= 256)
        return ESP_OK;

    uint8_t *buf = calloc(1, ws_pkt.len + 1);
    if (!buf) return ESP_OK;
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret == ESP_OK) {
        buf[ws_pkt.len] = '\0';
        ws_process_command((char *)buf);
    }
    free(buf);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    if (ws_server) return ws_server;  /* already running */

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    config.lru_purge_enable = true;    /* close oldest connection when full */
    config.close_fn = on_sock_close;   /* clean up WS client list on disconnect */
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

        httpd_uri_t ws_audio = {
            .uri = "/ws_audio",
            .method = HTTP_GET,
            .handler = ws_audio_handler,
            .is_websocket = true,
        };
        httpd_register_uri_handler(server, &ws_audio);

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

        httpd_uri_t api_state = {
            .uri = "/api/state",
            .method = HTTP_GET,
            .handler = api_state_handler,
        };
        httpd_register_uri_handler(server, &api_state);

        /* iOS-specific probe URLs — must return 200 OK with the "Success"
         * HTML body, otherwise iOS treats the network as broken/captive
         * and drops it within a few minutes. */
        const char *ios_uris[] = {
            "/hotspot-detect.html",
            "/library/test/success.html",
            "/success.html",
            NULL
        };
        httpd_uri_t ios_probe = {
            .method = HTTP_GET,
            .handler = ios_success_handler,
        };
        for (int i = 0; ios_uris[i]; i++) {
            ios_probe.uri = ios_uris[i];
            httpd_register_uri_handler(server, &ios_probe);
        }

        /* Android / Windows probe URLs — 204 No Content is the expected
         * "no captive portal" response. */
        const char *probe_uris[] = {
            "/generate_204",
            "/gen_204",
            "/ncsi.txt",
            "/connecttest.txt",
            "/redirect",
            NULL
        };
        httpd_uri_t probe = {
            .method = HTTP_GET,
            .handler = catchall_handler,
        };
        for (int i = 0; probe_uris[i]; i++) {
            probe.uri = probe_uris[i];
            httpd_register_uri_handler(server, &probe);
        }

        ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    }
    ws_server = server;
    return server;
}

/* ── WiFi AP Start/Stop ────────────────────────────────────────── */

static void stm32_wake(void);  /* fwd decl — defined later in file */

/* WiFi AP event handler: wake STM32 whenever a station joins so the
 * user's incoming config commands land on a live STM32. */
static void wifi_ap_event_handler(void *arg, esp_event_base_t base,
                                  int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "AP STA connected (aid=%d) — queuing STM32 wake", e->aid);
        /* Defer to spi_task — same reason as ws_handler. */
        stm32_wake_pending = true;
    }
}

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

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ap_event_handler, NULL, NULL));

        netif_inited = true;
    }
}

static void wifi_start(void)
{
    if (wifi_started) return;

    wifi_init_netif();

    wifi_config_t wifi_config = {
        .ap = {
            .channel = wifi_pick_channel(),
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
    ESP_LOGI(TAG, "WiFi AP started: SSID=%s, ch=%u, http://192.168.9.1",
             device_name, wifi_pick_channel());
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

/* Load laser wake setting from NVS */
static void load_laser_wake(void)
{
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t val = 0;
    if (nvs_get_u8(h, "laser", &val) == ESP_OK) {
        laser_wake_enabled = (val != 0);
        ESP_LOGI(TAG, "Loaded laser wake from NVS: %s", laser_wake_enabled ? "ON" : "OFF");
    }
    nvs_close(h);
}

/* Save laser wake setting to NVS */
void save_laser_wake(void)
{
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "laser", laser_wake_enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved laser wake to NVS: %s", laser_wake_enabled ? "ON" : "OFF");
}

/* Sync the saved device name (WiFi SSID) to the station ID.
 *
 * The new name is persisted to NVS and applied to the AP on the NEXT boot — the
 * boot path reads it via load_device_name(). We deliberately do NOT reconfigure
 * the live SoftAP here: esp_wifi_set_config() on a running AP deauths every
 * connected client, which produced the "drops ~1 min after connecting" bug — the
 * STM32's first SPI config frame (≈30-60 s post-boot) carries its stationId, and
 * renaming the live AP kicked the web-UI client mid-session (e.g. mid STM flash).
 * Deferring to reboot keeps the provisioning session stable. */
static void update_device_name(const char *name)
{
    if (name == NULL || name[0] == '\0') return;     /* ignore unset/blank */
    if (strcmp(device_name, name) == 0) return;      /* no change */

    strncpy(device_name, name, sizeof(device_name) - 1);
    device_name[sizeof(device_name) - 1] = '\0';

    /* Persist so the next reboot brings the AP up with this SSID. */
    save_device_name();
    ESP_LOGI(TAG, "Device name set to '%s' — WiFi SSID applies on next reboot", device_name);
}

/* ── STM32 Wake (pull CS pin low for 10ms) ─────────────────────── */

/* Pulse CS LOW for 10 ms to trigger EXTI on STM32. Called whenever a
 * user connects to the ESP32 AP — we don't track STM32 sleep state, so
 * the pulse is unconditional. If STM32 is already awake the EXTI is
 * benign; spi_task re-inits SPI after we tear it down here. */
static void stm32_wake(void)
{
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

    /* Pull LOW for 10ms to trigger EXTI on STM32 */
    gpio_set_level(PIN_CS, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_CS, 1);

    /* Brief settle, then re-arm SPI slave so STM32's next poll lands. */
    vTaskDelay(pdMS_TO_TICKS(100));
    spi_slave_init();
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

/* ── SPI Slave Task ────────────────────────────────────────────── */

static void spi_task(void *arg)
{
    /* Send ESP_VERSION on first exchange so STM32 knows our firmware */
    cmd_enqueue(SPI_CMD_ESP_VERSION, (const uint8_t *)ESP_FW_VERSION,
                strlen(ESP_FW_VERSION));

    while (1) {
        if (spi_task_stop) {
            ESP_LOGI(TAG, "SPI task stopping");
            spi_task_handle = NULL;
            vTaskDelete(NULL);
            return;
        }

        /* Drain deferred wake requests. Other tasks set stm32_wake_pending
         * (WS handshake, WiFi STA_CONNECTED) and we run the actual SPI
         * teardown / GPIO pulse / re-init here, in our own context, so
         * we don't race with ourselves and don't block the caller. */
        if (stm32_wake_pending) {
            stm32_wake_pending = false;
            stm32_wake();
        }

        if (!spi_initialized) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Build TX frame */
        spi_frame_t *tx = (spi_frame_t *)spi_txBuf;
        memset(tx, 0, TRANSFER_SIZE);

        tx->header.magic = SPI_FRAME_MAGIC;
        tx->header.frame_version = SPI_FRAME_VERSION;
        tx->header.cfg_seq = local_cfg_seq;

        uint16_t flags = 0;
        if (first_boot) {
            flags |= SPI_FLAG_BOOT;
            first_boot = false;
        }
        if (local_cfg_seq > 0)
            flags |= SPI_FLAG_CONFIG_DIRTY;

        /* Copy local config into frame */
        memcpy(&tx->config, &local_cfg, sizeof(device_config_t));

        /* Dequeue command — keep in TX frame for up to 5 exchanges
         * to survive SPI CRC errors. STM32 dedup via cmd_seq. */
        static qt_spi_cmd_t pending_cmd = {0};
        static int pending_ttl = 0;

        qt_spi_cmd_t new_cmd;
        if (cmd_dequeue(&new_cmd)) {
            memcpy(&pending_cmd, &new_cmd, sizeof(qt_spi_cmd_t));
            pending_ttl = 5;
        }
        if (pending_ttl > 0) {
            memcpy(&tx->command, &pending_cmd, sizeof(qt_spi_cmd_t));
            flags |= SPI_FLAG_CMD_PENDING;
            pending_ttl--;
        }

        tx->header.flags = flags;
        tx->header.crc16 = spi_frame_crc(tx);

        memset(spi_rxBuf, 0, TRANSFER_SIZE);

        spi_slave_transaction_t trans = {
            .length = TRANSFER_SIZE * 8,
            .tx_buffer = spi_txBuf,
            .rx_buffer = spi_rxBuf,
        };

        esp_err_t ret = spi_slave_transmit(SPI_HOST, &trans, pdMS_TO_TICKS(1000));
        vTaskDelay(pdMS_TO_TICKS(1));

        if (ret == ESP_OK) {
            spi_transaction_count++;

            spi_frame_t *rx = (spi_frame_t *)spi_rxBuf;

            if (rx->header.magic == SPI_FRAME_MAGIC) {
                /* Validate CRC */
                uint16_t expected_crc = spi_frame_crc(rx);
                if (expected_crc != rx->header.crc16) {
                    ESP_LOGW(TAG, "SPI CRC mismatch");
                    continue;
                }

                /* Valid frame → STM32 is alive and running its app */
                int64_t _now_us = esp_timer_get_time();
                last_spi_ok_us = _now_us;

                /* Scheduled-sleep notice: if the STM says it's about to Stop 2 for
                 * N seconds, the ensuing silence is EXPECTED — gate the watchdog
                 * until N + margin elapses (margin covers wake + the first frame
                 * after the window, e.g. powering up to record). 0 = awake. */
                if (rx->state.pwr_sleepSecs > 0)
                    stm32_sleep_until_us = _now_us +
                        ((int64_t)rx->state.pwr_sleepSecs + STM32_WD_SLEEP_MARGIN_S) * 1000000LL;
                else
                    stm32_sleep_until_us = 0;

                /* Config sync: adopt if STM32 seq is higher */
                if (rx->header.cfg_seq > local_cfg_seq) {
                    memcpy(&local_cfg, &rx->config, sizeof(device_config_t));
                    local_cfg_seq = rx->header.cfg_seq;
                    stm32_synced = true;
                    ESP_LOGI(TAG, "Config synced from STM32 (seq=%lu)",
                             (unsigned long)local_cfg_seq);
                }

                /* Cache state for web UI */
                if (rx->header.flags & SPI_FLAG_STATE_VALID) {
                    memcpy(&local_state, &rx->state, sizeof(spi_state_t));
                    new_spi_data = true;
                }

                /* Extract audio samples from reserved region */
                if (audio_streaming) {
                    const spi_audio_payload_t *ap =
                        (const spi_audio_payload_t *)rx->_reserved;
                    if (ap->audio_active && ap->num_samples > 0 &&
                        ap->num_samples <= 214) {
                        uint32_t h = audio_ring_head;
                        for (uint16_t i = 0; i < ap->num_samples; i++) {
                            audio_ring[h & AUDIO_RING_MASK] = ap->samples[i];
                            h++;
                        }
                        audio_ring_head = h;
                    }
                }

                /* Sync WiFi SSID to station ID when it changes */
                update_device_name(local_cfg.stationId);
            }
            /* Backward compat: legacy JSON from old STM32 firmware */
            else if (spi_rxBuf[0] == '{') {
                char *msg = (char *)spi_rxBuf;
                msg[TRANSFER_SIZE - 1] = '\0';
                new_spi_data = true;
            }
        }
    }
}

/* ── WebSocket Push Task ───────────────────────────────────────── */

/* Solar state names for JSON */
static const char *solar_names[] = { "Standby", "Charging", "Complete", "Fault" };

static void ws_push_task(void *arg)
{
    static char json_buf[1500];

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (new_spi_data && ws_server) {
            new_spi_data = false;

            const spi_state_t *s = &local_state;
            const device_config_t *c = &local_cfg;

            /* Build GPS time string */
            char gpsTime[12] = "--:--:--";
            if (s->gps_utcTime) {
                uint8_t hh = (uint8_t)(s->gps_utcTime / 10000);
                uint8_t mm = (uint8_t)((s->gps_utcTime / 100) % 100);
                uint8_t ss = (uint8_t)(s->gps_utcTime % 100);
                snprintf(gpsTime, sizeof(gpsTime), "%02d:%02d:%02d",
                         (int)hh, (int)mm, (int)ss);
            }

            /* Build windows array string */
            char winBuf[128] = "";
            {
                int pos = 0;
                for (int i = 0; i < c->numWindows && i < 8; i++) {
                    if (i > 0) winBuf[pos++] = ',';
                    pos += snprintf(winBuf + pos, sizeof(winBuf) - pos,
                                    "%u,%u", c->windows[i*2], c->windows[i*2+1]);
                }
            }

            /* Solar name */
            const char *solar = (s->solar_state < 4) ? solar_names[s->solar_state] : "Unknown";

            /* Temp formatting: sht30 is in 0.01°C */
            int tWhole = s->env_tempC100 / 100;
            int tFrac = (s->env_tempC100 % 100);
            if (tFrac < 0) tFrac = -tFrac;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(json_buf, sizeof(json_buf),
                "{\"bat\":%u,\"temp\":%d.%02d,\"hum\":%u.%u,"
                "\"rec\":%d,\"sd\":%d,\"gps\":%d,\"sat\":%d,"
                "\"station\":\"%s\",\"fw\":\"%s\",\"espFw\":\"%s\","
                "\"lat5\":%ld,\"lon5\":%ld,\"alt1\":%ld,"
                "\"gpsTime\":\"%s\",\"pps\":%d,\"ppsCnt\":%lu,"
                "\"sdFree\":%lu,\"sdTotal\":%lu,"
                "\"solar\":\"%s\","
                "\"recBytes\":%lu,\"ovf\":%lu,\"clip\":%lu,"
                "\"svLat5\":%ld,\"svLon5\":%ld,\"svCnt\":%lu,"
                "\"hFiles\":%lu,\"hSecs\":%lu,\"hDet\":%lu,"
                "\"hBatMin\":%lu,\"hBatMax\":%lu,"
                "\"hTmpMin\":%ld,\"hTmpMax\":%ld,"
                "\"hBoots\":%lu,\"hSdErr\":%lu,\"hGpsLoss\":%lu,"
                "\"mdl\":%d,\"mdlSz\":%lu,\"mdlCls\":%d,"
                "\"detWin\":%lu,\"detHit\":%lu,"
                "\"detSp\":\"%s\",\"detPct\":%d,\"detTm\":\"%s\","
                "\"mission\":%d,\"conf\":%d,\"winStep\":%d,"
                "\"sunEn\":%d,\"sunB\":%u,\"sunA\":%u,"
                "\"setEn\":%d,\"setB\":%u,\"setA\":%u,"
                "\"nWin\":%d,\"wins\":[%s],"
                "\"gain\":%d,\"fmt\":%d,\"hpf\":%u,\"lpf\":%u,\"chunk\":%d,"
                "\"trigEn\":%d,\"trigDb\":%d,\"trigPre\":%d,\"trigPost\":%d,"
                "\"lowBat\":%d,\"autoStop\":%d,\"micHdg\":%u,"
                "\"pwrState\":%d,\"devMode\":%d,\"schedActive\":%d,\"rtcSync\":%d,"
                "\"synced\":%d}",
                (unsigned)s->env_battMv,
                tWhole, tFrac,
                (unsigned)(s->env_humRH100 / 100),
                (unsigned)(s->env_humRH100 / 10 % 10),
                (int)s->rec_active,
                (int)s->rec_sdMounted,
                (int)s->gps_valid,
                (int)s->gps_satellites,
                c->stationId, s->comms_stm32FwVersion, ESP_FW_VERSION,
                (long)s->gps_lat5, (long)s->gps_lon5, (long)s->gps_alt1,
                gpsTime,
                (int)s->gps_ppsSynced, (unsigned long)s->gps_ppsCount,
                (unsigned long)s->rec_sdFreeKb,
                (unsigned long)s->rec_sdTotalKb,
                solar,
                (unsigned long)s->rec_dataBytes,
                (unsigned long)s->rec_overruns,
                (unsigned long)s->audio_clipCount,
                (long)s->survey_lat5, (long)s->survey_lon5,
                (unsigned long)s->survey_count,
                (unsigned long)s->health_filesWritten,
                (unsigned long)s->health_recordingSecs,
                (unsigned long)s->health_detections,
                (unsigned long)s->health_battMinMv,
                (unsigned long)s->health_battMaxMv,
                (long)s->health_tempMinC100, (long)s->health_tempMaxC100,
                (unsigned long)s->health_bootCount,
                (unsigned long)s->health_sdErrors,
                (unsigned long)s->health_gpsFixLosses,
                (int)s->det_modelLoaded,
                (unsigned long)s->det_modelBufSize,
                (int)s->det_modelNumLabels,
                (unsigned long)s->det_windowsProcessed,
                (unsigned long)s->det_hits,
                s->det_lastSpecies,
                (int)s->det_lastConf,
                s->det_lastTime,
                (int)c->missionMode,
                (int)c->detConfThresh,
                (int)c->detWindowStep,
                (int)c->sunriseEnabled,
                (unsigned)c->sunriseBefore,
                (unsigned)c->sunriseAfter,
                (int)c->sunsetEnabled,
                (unsigned)c->sunsetBefore,
                (unsigned)c->sunsetAfter,
                (int)c->numWindows,
                winBuf,
                (int)c->gain,
                (int)c->recFormat,
                (unsigned)c->bpfLow,
                (unsigned)c->bpfHigh,
                (int)c->chunkMinutes,
                (int)c->trigEnabled,
                (int)c->trigDb,
                (int)c->trigPre,
                (int)c->trigPost,
                (int)c->lowBatPct,
                (int)c->autoStop,
                (unsigned)c->micHeading,
                (int)s->pwr_state,
                (int)s->pwr_devMode,
                (int)s->pwr_schedActive,
                (int)s->pwr_rtcSynced,
                (int)stm32_synced);
#pragma GCC diagnostic pop

            ws_broadcast(json_buf);
        }
    }
}

/* ── Audio Push Task ───────────────────────────────────────────── */

static void audio_push_task(void *arg)
{
    /* Buffer for binary WS frame: up to 214 samples = 428 bytes */
    static int16_t push_buf[214];

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(25));

        if (!audio_streaming || !ws_server || audio_ws_count() == 0) {
            /* Drain ring to avoid stale data accumulating */
            audio_ring_tail = audio_ring_head;
            continue;
        }

        /* Copy available samples from ring */
        uint32_t h = audio_ring_head;
        uint32_t t = audio_ring_tail;
        uint32_t avail = h - t;
        if (avail == 0) continue;
        if (avail > 214) {
            /* Drop excess to prevent growing latency */
            t = h - 214;
        }

        uint16_t count = 0;
        while (t != h && count < 214) {
            push_buf[count++] = audio_ring[t & AUDIO_RING_MASK];
            t++;
        }
        audio_ring_tail = t;

        if (count == 0) continue;

        /* Send binary frame to all audio WS clients */
        httpd_ws_frame_t ws_pkt = {
            .type = HTTPD_WS_TYPE_BINARY,
            .payload = (uint8_t *)push_buf,
            .len = count * sizeof(int16_t),
        };

        for (int i = 0; i < MAX_AUDIO_CLIENTS; i++) {
            if (audio_ws_fds[i] != 0) {
                esp_err_t ret = httpd_ws_send_frame_async(
                    ws_server, audio_ws_fds[i], &ws_pkt);
                if (ret != ESP_OK) {
                    audio_ws_remove(audio_ws_fds[i]);
                }
            }
        }
    }
}

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
    uint32_t last_client_tick = 0;  /* xTaskGetTickCount when a client was last seen */

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* Check if any WiFi clients are connected */
        wifi_sta_list_t sta_list;
        int wifi_clients = 0;
        if (wifi_started && esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
            wifi_clients = sta_list.num;
        }

        /* Track "last client seen" — used to bridge brief iOS disconnect/reconnect
         * cycles. We treat the AP as "in use" if a client was associated within
         * the last CLIENT_GRACE_MS, even if not right this second. */
        if (wifi_clients > 0) {
            last_client_tick = xTaskGetTickCount();
            duty_cycle_tick = xTaskGetTickCount();
        }
        uint32_t now_tick = xTaskGetTickCount();
        bool client_recent = (last_client_tick != 0) &&
                             ((now_tick - last_client_tick) * portTICK_PERIOD_MS) < CLIENT_GRACE_MS;

        /* Boot grace period: stay awake for 3 min after boot so user can connect */
        uint32_t uptime_ms = now_tick * portTICK_PERIOD_MS;
        if (uptime_ms < BOOT_GRACE_MS) continue;

        /* WiFi duty cycle: on 30s with no client (and no recent client),
         * then deep sleep 60s. Deep sleep draws ~5µA vs 44mA with WiFi off
         * + CPU idle. On wake, ESP32 reboots — WiFi/NVS reinitialize. */
        if (wifi_duty_cycle && !client_recent) {
            uint32_t elapsed = (now_tick - duty_cycle_tick) * portTICK_PERIOD_MS;
            if (elapsed >= WIFI_DUTY_ON_MS) {
                ESP_LOGI(TAG, "Duty cycle: entering deep sleep for %ds", WIFI_DUTY_SLEEP_S);
                esp_deep_sleep(WIFI_DUTY_SLEEP_S * 1000000ULL);
                /* Does not return — ESP32 reboots on wake */
            }
        }

        /* Laser wake: deep sleep indefinitely, wake on GPIO0 high.
         * Stay awake while WiFi clients are connected + grace period. */
        if (laser_wake_enabled && !client_recent) {
            uint32_t elapsed = (now_tick - duty_cycle_tick) * portTICK_PERIOD_MS;
            if (elapsed >= (LASER_IDLE_SLEEP_S * 1000)) {
                ESP_LOGI(TAG, "Laser wake: entering deep sleep (GPIO%d wake)", PIN_LASER_WAKE);
                esp_err_t err = esp_deep_sleep_enable_gpio_wakeup(
                    1ULL << PIN_LASER_WAKE, ESP_GPIO_WAKEUP_GPIO_HIGH);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "GPIO wake setup FAILED: %s", esp_err_to_name(err));
                } else {
                    vTaskDelay(pdMS_TO_TICKS(100));  /* let log flush */
                    esp_deep_sleep_start();
                    /* Does not return — ESP32 reboots on wake */
                }
            }
        }
    }
}

/* ── Laser Wake Sensor (GPIO0 phototransistor) ─────────────────── */

static void laser_sensor_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_LASER_WAKE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  /* external 10K pull-down */
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "Laser wake sensor on GPIO%d", PIN_LASER_WAKE);
}

/* Log wake cause on boot */
static void laser_wake_check(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        ESP_LOGI(TAG, "Woke from deep sleep via laser (GPIO%d)", PIN_LASER_WAKE);
    }
}


/* ── STM32 self-heal watchdog ──────────────────────────────────────
 * If the STM32 stops producing valid SPI frames (e.g. an interrupted flash
 * left it without a runnable image), and a known-good staged image exists,
 * auto-reflash it from the retained partition.
 *
 * Redesigned (v0.4.12) to fix the loop that clobbered bench flashes:
 *  - Recovery is DISARMED (attempts reset) only after the STM32 has been
 *    CONTINUOUSLY alive for STABLE_US — not on a momentary blip. The old code
 *    reset on any single alive read, so a crash-looping image that flashed SPI
 *    once per boot never hit the cap → infinite re-flash.
 *  - DEAD_US is generous (30s) so a normal flash/boot or a brief debugger halt
 *    doesn't trip it.
 *  - Hard cap: after MAX_ATTEMPTS unrecovered, give up permanently (logged
 *    once) until the STM32 proves stable or the ESP reboots. Bench tip: for
 *    heavy J-Link/debug work, flash an ESP build with this task disabled. */
#define STM32_WD_BOOT_GRACE_MS  30000       /* let STM32 boot + start SPI before judging */
#define STM32_WD_DEAD_US        30000000LL  /* no valid frame for 30s = dead */
#define STM32_WD_STABLE_US      120000000LL /* continuously alive 120s => image good, re-arm */
#define STM32_WD_MAX_ATTEMPTS   3
/* STM32_WD_SLEEP_MARGIN_S defined near stm32_sleep_until_us (used earlier in spi_task) */

static void stm32_watchdog_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(STM32_WD_BOOT_GRACE_MS));
    int     attempts    = 0;
    int64_t alive_since = 0;      /* esp_timer time the STM became continuously alive */
    bool    gave_up     = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        /* Don't interfere while a flash (web OTA or recovery) is in progress */
        if (stm32_flash_pct >= 0) { alive_since = 0; continue; }

        int64_t now = esp_timer_get_time();

        /* Scheduled sleep: the STM told us it's in Stop 2 until ~stm32_sleep_until_us.
         * Its silence is EXPECTED — do NOT treat it as dead or reflash it (this was
         * the bug that wiped config + killed dawn-chorus recording on all units).
         * Once overdue, the gate opens and normal dead-detection resumes so a unit
         * that genuinely failed to wake is still recovered. */
        if (stm32_sleep_until_us != 0 && now < stm32_sleep_until_us) {
            alive_since = 0;  /* not counting toward the post-sleep stability window */
            continue;
        }

        bool alive = (last_spi_ok_us != 0) && ((now - last_spi_ok_us) < STM32_WD_DEAD_US);

        if (alive) {
            if (alive_since == 0) alive_since = now;
            /* Re-arm only after SUSTAINED liveness — a crash-looper that blips
             * SPI each boot stays below STABLE_US and must not reset attempts. */
            if (attempts > 0 && (now - alive_since) >= STM32_WD_STABLE_US) {
                ESP_LOGI(TAG, "STM32 stable %ds — recovery re-armed",
                         (int)(STM32_WD_STABLE_US / 1000000));
                attempts = 0;
                gave_up  = false;
            }
            continue;
        }

        /* STM32 has been silent past DEAD_US */
        alive_since = 0;

        if (attempts >= STM32_WD_MAX_ATTEMPTS) {
            if (!gave_up) {
                ESP_LOGE(TAG, "STM32 unrecoverable after %d attempts — giving up "
                              "(needs manual flash)", STM32_WD_MAX_ATTEMPTS);
                gave_up = true;
            }
            continue;
        }

        uint32_t sz = 0, crc = 0;
        if (!staged_meta_load(&sz, &crc))
            continue;  /* nothing staged to recover with */

        const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, 0x80, STM32FW_PARTITION_LABEL);
        if (!part) continue;

        /* Trust the staged image only if it's still intact */
        if (stm32_flash_crc32(part, sz) != crc) {
            ESP_LOGE(TAG, "Recovery aborted: staged image CRC mismatch");
            attempts = STM32_WD_MAX_ATTEMPTS;  /* don't retry a corrupt image */
            continue;
        }

        attempts++;
        ESP_LOGW(TAG, "STM32 unresponsive %ds — recovery flash %d/%d (%lu bytes)",
                 (int)(STM32_WD_DEAD_US / 1000000), attempts, STM32_WD_MAX_ATTEMPTS,
                 (unsigned long)sz);
        ws_broadcast("{\"stm32Recover\":1}");

        do_stm32_flash(part, sz);

        /* Let it boot + check in before judging again */
        vTaskDelay(pdMS_TO_TICKS(STM32_WD_BOOT_GRACE_MS));
    }
}

/* ── Main ──────────────────────────────────────────────────────── */

void app_main(void)
{
    printf("\n=== ESP32-C3 QuailTracker Bridge v2 ===\n\n");

    /* FIRST: release STM32 BOOT0/NRST pins to high-Z */
    stm32_pins_init();

    /* Initialize NVS (required for WiFi) */
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
    load_laser_wake();

    /* Initialize binary SPI protocol state */
    memset(&local_cfg, 0, sizeof(local_cfg));
    local_cfg.micHeading = 0xFFFF;  /* unset until synced from STM32 */
    memset(&local_state, 0, sizeof(local_state));
    local_cfg_seq = 0;
    stm32_synced = false;
    first_boot = true;

    /* Initialize network interface (needed before WiFi) */
    wifi_init_netif();

    wifi_start();
    start_webserver();
    spi_slave_init();
    laser_sensor_init();
    laser_wake_check();

    xTaskCreate(spi_task, "spi_task", 4096, NULL, 3, &spi_task_handle);
    xTaskCreate(ws_push_task, "ws_push", 4096, NULL, 4, NULL);
    xTaskCreate(audio_push_task, "audio_push", 3072, NULL, 4, NULL);
    xTaskCreate(power_mgmt_task, "pwr_mgmt", 4096, NULL, 2, NULL);
    xTaskCreate(dns_task, "dns", 4096, NULL, 2, NULL);
    /* STM32 self-heal watchdog — redesigned: re-arms only after sustained
     * liveness, generous 30s dead-timeout, hard 3-attempt cap (no infinite
     * re-flash loop, tolerant of normal flash/boot). */
    xTaskCreate(stm32_watchdog_task, "stm32_wd", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "Bridge ready — WiFi AP + SPI + WebSocket");
}
