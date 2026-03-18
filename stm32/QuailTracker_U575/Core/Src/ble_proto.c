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

#include "ble_proto.h"
#include "stm32u5xx_hal.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef husart2;  /* BLE UART */

static uint16_t tx_seq = 0;
static subscription_t subs[MAX_SUBSCRIPTIONS];

void ble_proto_init(void)
{
    memset(subs, 0, sizeof(subs));
    tx_seq = 0;
}

void ble_proto_reset(void)
{
    memset(subs, 0, sizeof(subs));
}

bool ble_proto_send(uint8_t topic, const uint8_t *pb_buf, size_t pb_len)
{
    if (pb_len > FRAME_MAX_PAYLOAD) return false;

    /* Build frame: [0x01 SOF] [LEN_HI] [LEN_LO] [header] [protobuf payload]
     * Length = header + payload (does NOT include SOF or length bytes themselves) */
    size_t raw_len = FRAME_HEADER_SIZE + pb_len;
    uint8_t frame[3 + FRAME_MAX_RAW];  /* SOF + 2-byte length + header + payload */
    frame[0] = 0x01;  /* start-of-frame marker */
    frame[1] = (uint8_t)((raw_len >> 8) & 0xFF);
    frame[2] = (uint8_t)(raw_len & 0xFF);
    frame[3] = topic;
    frame[4] = 0;  /* flags */
    frame[5] = (uint8_t)(tx_seq & 0xFF);
    frame[6] = (uint8_t)((tx_seq >> 8) & 0xFF);
    if (pb_len > 0)
        memcpy(&frame[7], pb_buf, pb_len);
    tx_seq++;

    /* Transmit over BLE UART */
    HAL_UART_Transmit(&husart2, frame, (uint16_t)(3 + raw_len), 100);
    return true;
}

void ble_proto_receive(const uint8_t *frame, size_t len)
{
    /* Frame is raw bytes (no COBS): [header 4 bytes] [protobuf payload] */
    if (len < FRAME_HEADER_SIZE) return;

    uint8_t topic = frame[0];
    /* uint8_t flags = frame[1]; */
    /* uint16_t seq = frame[2] | (frame[3] << 8); */
    const uint8_t *payload = &frame[FRAME_HEADER_SIZE];
    size_t payload_len = len - FRAME_HEADER_SIZE;

    printf("BLE_PROTO: rx topic=0x%02X len=%u\r\n", topic, (unsigned)payload_len);

    /* Dispatch by topic — implemented in app_freertos.c */
    extern void ble_proto_dispatch(uint8_t topic, const uint8_t *payload,
                                   size_t payload_len);
    ble_proto_dispatch(topic, payload, payload_len);
}

void ble_proto_poll_subscriptions(void)
{
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (!subs[i].active) continue;
        if (subs[i].interval_ms == 0) continue;  /* on-change only */
        if ((now - subs[i].last_push_ms) >= subs[i].interval_ms) {
            subs[i].last_push_ms = now;
            /* Encode and push the subscribed topic */
            extern void ble_proto_push_topic(uint8_t topic);
            ble_proto_push_topic(subs[i].topic);
        }
    }
}

/* Called by dispatch when a Subscribe message is received */
void ble_proto_add_subscription(uint8_t topic, uint32_t interval_ms)
{
    /* Update existing or find empty slot */
    int free_slot = -1;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (subs[i].active && subs[i].topic == topic) {
            subs[i].interval_ms = interval_ms;
            subs[i].last_push_ms = HAL_GetTick();
            return;
        }
        if (!subs[i].active && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        subs[free_slot].topic = topic;
        subs[free_slot].interval_ms = interval_ms;
        subs[free_slot].last_push_ms = HAL_GetTick();
        subs[free_slot].active = true;
    }
}

/* Called by dispatch when an Unsubscribe message is received */
void ble_proto_remove_subscription(uint8_t topic)
{
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (subs[i].active && subs[i].topic == topic) {
            subs[i].active = false;
            return;
        }
    }
}

/* Check if a topic has active subscribers (for on-change push) */
bool ble_proto_has_subscriber(uint8_t topic)
{
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (subs[i].active && subs[i].topic == topic)
            return true;
    }
    return false;
}
