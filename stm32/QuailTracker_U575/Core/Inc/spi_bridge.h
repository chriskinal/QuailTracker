/*
 * QuailTracker — SPI Bridge (STM32 side)
 *
 * Binary frame build/process for STM32 ↔ ESP32 SPI communication.
 */

#ifndef SPI_BRIDGE_H
#define SPI_BRIDGE_H

#include "device_state.h"
#include "spi_protocol.h"

/* Fill state struct from device runtime state + health */
void spi_state_fill(spi_state_t *s, const device_state_t *dev,
                    const health_stats_t *health, const device_config_t *cfg,
                    uint8_t solar_st);

/* Build a complete TX frame (header, config, state, zeroed command) */
void spi_frame_build(spi_frame_t *frame, const device_config_t *cfg,
                     const device_state_t *dev, const health_stats_t *health,
                     uint8_t solar_st, uint16_t flags);

/* Process a received frame: validate, sync config (returns 1 if config adopted),
 * dispatch command. cmd_out is set to the command type (SPI_CMD_NONE if none). */
int spi_frame_process_rx(const spi_frame_t *frame, device_config_t *cfg,
                         uint8_t *cmd_out, uint8_t *cmd_payload, uint32_t *cmd_seq);

/* Apply config to hardware (gain, format, BPF, etc).
 * Called after config load or adoption from SPI. */
void config_apply(const device_config_t *cfg);

#endif /* SPI_BRIDGE_H */
