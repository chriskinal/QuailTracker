/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* BLE protobuf frame protocol over PB-03F transparent UART.
 *
 * Wire format:  [COBS-encoded(header + protobuf payload)] [0x00]
 *
 * Header (4 bytes, before COBS encoding):
 *   Byte 0:  Topic (message type)
 *   Byte 1:  Flags (bit 0 = is_response)
 *   Byte 2-3: Sequence number (uint16 LE, wrapping)
 */

#ifndef BLE_PROTO_H
#define BLE_PROTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Topic IDs — must match app-side enum */
enum {
    /* Device → App (push) */
    TOPIC_STATUS          = 0x01,
    TOPIC_DETECTION       = 0x02,
    TOPIC_AUDIO_LEVEL     = 0x03,
    TOPIC_RECORDING_STATE = 0x04,
    TOPIC_GPS_FIX         = 0x05,
    TOPIC_LOG             = 0x06,
    TOPIC_HEALTH_REPORT   = 0x07,

    /* App → Device (command) */
    TOPIC_COMMAND         = 0x10,
    TOPIC_SET_CONFIG      = 0x11,
    TOPIC_SUBSCRIBE       = 0x12,
    TOPIC_UNSUBSCRIBE     = 0x13,

    /* Device → App (response) */
    TOPIC_COMMAND_ACK     = 0x20,
    TOPIC_CONFIG_DUMP     = 0x21,

    /* Bidirectional */
    TOPIC_PING            = 0x30,
    TOPIC_PONG            = 0x31,

    /* OTA */
    TOPIC_OTA_BEGIN       = 0x40,
    TOPIC_OTA_DATA        = 0x41,
    TOPIC_OTA_END         = 0x42,
    TOPIC_OTA_COMMIT      = 0x43,
    TOPIC_OTA_ABORT       = 0x44,
    TOPIC_OTA_STATUS      = 0x45,
    TOPIC_OTA_ROLLBACK    = 0x46,
};

/* Header flags */
#define FRAME_FLAG_RESPONSE  0x01

#define FRAME_HEADER_SIZE    4
#define FRAME_MAX_PAYLOAD    176   /* protobuf payload limit */
#define FRAME_MAX_RAW        (FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD)  /* 180 */
#define FRAME_MAX_ENCODED    (FRAME_MAX_RAW + FRAME_MAX_RAW/254 + 2) /* COBS + delimiter */

/* Subscription table */
#define MAX_SUBSCRIPTIONS    8

typedef struct {
    uint8_t  topic;
    uint32_t interval_ms;   /* 0 = on-change only */
    uint32_t last_push_ms;
    bool     active;
} subscription_t;

/* Send a protobuf message as a COBS-framed packet over BLE UART.
 * topic:   one of TOPIC_* constants
 * pb_buf:  protobuf-encoded payload bytes
 * pb_len:  length of protobuf payload
 * Returns: true on success */
bool ble_proto_send(uint8_t topic, const uint8_t *pb_buf, size_t pb_len);

/* Process a received COBS frame (after stripping the 0x00 delimiter).
 * frame:   COBS-encoded bytes (without trailing 0x00)
 * len:     length of COBS data
 * Called from BLE task when a complete frame is assembled. */
void ble_proto_receive(const uint8_t *frame, size_t len);

/* Check subscription timers and push due messages.
 * Call from BLE task loop every ~10 ms. */
void ble_proto_poll_subscriptions(void);

/* Initialize protocol state. Call once at startup. */
void ble_proto_init(void);

/* Reset subscriptions (call on BLE disconnect). */
void ble_proto_reset(void);

#endif /* BLE_PROTO_H */
