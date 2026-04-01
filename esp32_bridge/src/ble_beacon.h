/*
 * ble_beacon.h — BLE advertising beacon for QuailTracker ESP32-C3 bridge
 *
 * Advertises device name + manufacturer data (battery, recording state).
 * On BLE connect: triggers WiFi start + STM32 wake.
 * On disconnect + timeout: stops WiFi.
 */

#ifndef BLE_BEACON_H
#define BLE_BEACON_H

#include <stdint.h>
#include <stdbool.h>

/* Callback types for BLE events */
typedef void (*ble_connect_cb_t)(void);
typedef void (*ble_disconnect_cb_t)(void);

/* Initialize NimBLE and start advertising */
void ble_beacon_init(const char *device_name,
                     ble_connect_cb_t on_connect,
                     ble_disconnect_cb_t on_disconnect);

/* Update manufacturer data in advertisement (called periodically) */
void ble_beacon_update_data(uint16_t battery_mv, uint8_t recording,
                            uint8_t power_state);

/* Check if a BLE client is currently connected */
bool ble_beacon_is_connected(void);

#endif /* BLE_BEACON_H */
