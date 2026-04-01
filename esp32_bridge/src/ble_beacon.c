/*
 * ble_beacon.c — BLE advertising beacon for QuailTracker ESP32-C3 bridge
 *
 * Uses NimBLE stack (ESP-IDF native) to advertise device presence.
 * Manufacturer data includes battery voltage, recording state, and power state.
 *
 * BLE connect → callback triggers WiFi start + STM32 wake
 * BLE disconnect → callback triggers WiFi stop after timeout
 */

#include "ble_beacon.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "BLE_BEACON"

/* Company ID: 0xFFFF = reserved for testing/development */
#define COMPANY_ID 0xFFFF

static ble_connect_cb_t    s_on_connect = NULL;
static ble_disconnect_cb_t s_on_disconnect = NULL;
static volatile bool       s_connected = false;
static uint8_t             s_own_addr_type;

/* Manufacturer-specific data payload:
 * [0-1] Company ID (little-endian)
 * [2-3] Battery voltage mV (little-endian)
 * [4]   Recording state (0/1)
 * [5]   Power state enum
 */
static uint8_t s_mfg_data[6] = {
    (COMPANY_ID & 0xFF), (COMPANY_ID >> 8),
    0, 0,   /* battery mV */
    0,      /* recording */
    0,      /* power state */
};

static void ble_advertise(void);

/* GAP event handler */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE connect: status=%d handle=%d",
                 event->connect.status, event->connect.conn_handle);
        if (event->connect.status == 0) {
            s_connected = true;
            if (s_on_connect) s_on_connect();
        } else {
            /* Connection failed — restart advertising */
            ble_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnect: reason=%d",
                 event->disconnect.reason);
        s_connected = false;
        if (s_on_disconnect) s_on_disconnect();
        /* Restart advertising */
        ble_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "ADV complete");
        ble_advertise();
        break;

    default:
        break;
    }
    return 0;
}

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};

    /* Advertising fields */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)ble_svc_gap_device_name();
    fields.name_len = strlen(ble_svc_gap_device_name());
    fields.name_is_complete = 1;
    fields.mfg_data = s_mfg_data;
    fields.mfg_data_len = sizeof(s_mfg_data);

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    /* Connectable undirected advertising, 1000ms interval */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 1600;  /* 1000ms in 0.625ms units */
    adv_params.itvl_max = 1600;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    }
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_auto failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE synced, starting advertising");
    ble_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE reset: reason=%d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_beacon_init(const char *device_name,
                     ble_connect_cb_t on_connect,
                     ble_disconnect_cb_t on_disconnect)
{
    s_on_connect = on_connect;
    s_on_disconnect = on_disconnect;

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return;
    }

    /* Configure host callbacks */
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* Initialize GAP and GATT services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Set device name */
    rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "name_set failed: %d", rc);
    }

    /* Start NimBLE host task */
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE beacon initialized: name=%s", device_name);
}

void ble_beacon_update_data(uint16_t battery_mv, uint8_t recording,
                            uint8_t power_state)
{
    s_mfg_data[2] = battery_mv & 0xFF;
    s_mfg_data[3] = (battery_mv >> 8) & 0xFF;
    s_mfg_data[4] = recording;
    s_mfg_data[5] = power_state;

    /* If not connected, restart advertising with updated data */
    if (!s_connected) {
        ble_gap_adv_stop();
        ble_advertise();
    }
}

bool ble_beacon_is_connected(void)
{
    return s_connected;
}
