/*
 * ota_ab.h — Dual-bank A/B firmware update for STM32U575.
 *
 * The application is confined to one 512 KB bank (see linker). OTA writes the
 * alternate (inactive) bank, verifies it, copies config/health forward, then
 * flips the SWAP_BANK option byte and resets so the new image becomes active.
 * A reset-surviving "pending confirm" flag (RTC backup registers) auto-rolls
 * back to the previous bank if the new image fails to check in.
 *
 * Bank-2 base / sizing are derived at runtime from FLASH_BANK_SIZE, so this
 * adapts to both the 1 MB production part (2x512 KB) and a 2 MB dev board.
 *
 * GNU GPL v3 or later.
 */
#ifndef OTA_AB_H
#define OTA_AB_H

#include <stdint.h>
#include <stdbool.h>

/* ── Bank geometry / option-byte state (runtime) ─────────────────── */
uint32_t ota_bank_size(void);            /* bytes per bank (FLASH_BANK_SIZE) */
uint32_t ota_inactive_bank_base(void);   /* virtual base of the inactive bank */
bool     ota_dualbank_enabled(void);     /* FLASH->OPTR DUALBANK bit */
bool     ota_swap_enabled(void);         /* FLASH->OPTR SWAP_BANK bit */

/* One-time provisioning: on a 1 MB part with DUALBANK not yet set, enable it.
 * Programs option bytes and launches (system reset) — does NOT return on success.
 * No-op (returns false) if dual-bank is already active or not applicable. */
bool     ota_provision_dualbank(void);

/* ── A/B receive state machine (driven by SPI from the ESP32) ─────── */
/* OTA_BEGIN: erase the inactive bank's image region and arm reception. */
bool     ota_ab_begin(uint32_t image_size, uint32_t image_crc32);
/* Program one in-order chunk at `offset` (lock-step; see ota_ab_next_offset). */
bool     ota_ab_data(uint32_t offset, const uint8_t *data, uint16_t len);
/* Verify the full image CRC, carry config/health into the new bank, set the
 * pending-confirm flag, flip SWAP_BANK and reset. Does not return on success. */
bool     ota_ab_commit(void);
void     ota_ab_abort(uint8_t err);

uint8_t  ota_ab_state(void);         /* spi_ota_state_t */
uint8_t  ota_ab_err(void);
uint32_t ota_ab_next_offset(void);   /* next byte offset the receiver wants */
uint32_t ota_ab_bytes_ok(void);
bool     ota_ab_active(void);        /* true while receiving (speed up SPI poll) */

/* ── Confirm / rollback (RTC-backed, survives reset) ─────────────── */
/* Call ASAP in boot (after RTC is ready). If a freshly-swapped image is on
 * trial and has used up its boot attempts without confirming, this rolls back
 * to the previous bank (swap + reset). Otherwise counts this trial boot. */
void     ota_ab_boot_check(void);
/* Mark the running image good (clears the pending flag). Call on the first
 * successful SPI check-in with the ESP32. */
void     ota_ab_confirm(void);
bool     ota_ab_pending_confirm(void);

#endif /* OTA_AB_H */
