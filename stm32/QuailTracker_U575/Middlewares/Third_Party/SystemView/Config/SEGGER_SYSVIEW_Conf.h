/*
 * SEGGER SystemView configuration for QuailTracker STM32U575 (Cortex-M33)
 */
#ifndef SEGGER_SYSVIEW_CONF_H
#define SEGGER_SYSVIEW_CONF_H

// RTT buffer for SystemView — 4KB should be sufficient with full J-Link (1 Mbps)
#define SEGGER_SYSVIEW_RTT_BUFFER_SIZE  8192

#endif  // SEGGER_SYSVIEW_CONF_H
