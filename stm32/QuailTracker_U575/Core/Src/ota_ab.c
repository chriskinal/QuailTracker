/*
 * ota_ab.c — Dual-bank A/B firmware update for STM32U575. See ota_ab.h.
 *
 * GNU GPL v3 or later.
 *
 * BENCH-VALIDATION NOTES (this code is untested on hardware):
 *  - DUALBANK provisioning (ota_provision_dualbank) changes flash organisation
 *    on a 1 MB part; prefer doing it ONCE via ST-LINK/CubeProgrammer on the
 *    bench. The runtime path is provided but is the riskiest operation here.
 *  - SWAP_BANK semantics: this assumes the inactive bank is always the logical
 *    high window (FLASH_BANK_2 / FLASH_BASE+FLASH_BANK_SIZE) regardless of the
 *    current swap state, and that HAL erase/program follow the swapped view.
 *    Confirm on hardware before trusting unattended rollback.
 */

#include "main.h"
#include "ota_ab.h"
#include "spi_protocol.h"
#include <string.h>

/* Config/health live in the top two pages of the active bank window — keep in
 * sync with app_freertos.c. The image must not reach these pages. */
#define OTA_CFG_OFFSET    (FLASH_BANK_SIZE - 1 * FLASH_PAGE_SIZE)  /* config page */
#define OTA_HEALTH_OFFSET (FLASH_BANK_SIZE - 2 * FLASH_PAGE_SIZE)  /* health page */
#define OTA_MAX_IMAGE     (FLASH_BANK_SIZE - 2 * FLASH_PAGE_SIZE)  /* leave cfg+health */

/* RTC backup registers for reset-surviving confirm/rollback state */
#define OTA_BKP_PENDING   RTC_BKP_DR2
#define OTA_BKP_BOOTCNT   RTC_BKP_DR3
#define OTA_PENDING_MAGIC 0x0B0A0B0Au
#define OTA_MAX_TRIAL_BOOTS 3

extern RTC_HandleTypeDef hrtc;

static struct {
    uint8_t  state;       /* spi_ota_state_t */
    uint8_t  err;
    uint32_t size;
    uint32_t crc;         /* expected */
    uint32_t next_offset;
} ota;

/* ── geometry ─────────────────────────────────────────────────── */
uint32_t ota_bank_size(void)          { return FLASH_BANK_SIZE; }
uint32_t ota_inactive_bank_base(void) { return FLASH_BASE + FLASH_BANK_SIZE; }
bool ota_dualbank_enabled(void) { return (FLASH->OPTR & FLASH_OPTR_DUALBANK) != 0u; }
bool ota_swap_enabled(void)     { return (FLASH->OPTR & FLASH_OPTR_SWAP_BANK) != 0u; }

uint8_t  ota_ab_state(void)       { return ota.state; }
uint8_t  ota_ab_err(void)         { return ota.err; }
uint32_t ota_ab_next_offset(void) { return ota.next_offset; }
uint32_t ota_ab_bytes_ok(void)    { return ota.next_offset; }
bool     ota_ab_active(void)      { return ota.state == OTA_ST_RECEIVING; }

/* ── option-byte programming ──────────────────────────────────── */

/* Program USER option bytes (mask = which OB_USER_* bits, cfg = their values),
 * then launch (reloads OBs + system reset — does NOT return on success). */
static bool ob_program_and_launch(uint32_t userType, uint32_t userConfig)
{
    if (HAL_FLASH_Unlock() != HAL_OK) return false;
    if (HAL_FLASH_OB_Unlock() != HAL_OK) { HAL_FLASH_Lock(); return false; }

    FLASH_OBProgramInitTypeDef ob = {0};
    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType   = userType;
    ob.USERConfig = userConfig;
    if (HAL_FLASHEx_OBProgram(&ob) != HAL_OK) {
        HAL_FLASH_OB_Lock(); HAL_FLASH_Lock();
        return false;
    }

    /* Launch WHILE STILL UNLOCKED. HAL_FLASH_OB_Launch() sets FLASH_NSCR
     * OBL_LAUNCH, which the hardware honours only while the option bytes are
     * unlocked (OPTLOCK=0). Locking first (as we used to) makes the launch a
     * silent no-op: the swap byte is programmed but no reload/reset occurs, so
     * the swap never takes effect until the next unrelated reset. Setting
     * OBL_LAUNCH reloads the option bytes and generates a system reset — this
     * call does NOT return on success. HAL_FLASHEx_OBProgram leaves OPTLOCK
     * cleared, so it is valid to launch immediately here. */
    HAL_FLASH_OB_Launch();

    /* Only reached if the launch failed to reset — clean up and report. */
    HAL_FLASH_OB_Lock();
    HAL_FLASH_Lock();
    return false;
}

bool ota_provision_dualbank(void)
{
    /* Only meaningful on a 1 MB device where DUALBANK selects 2x512 KB. */
    if (ota_dualbank_enabled()) return false;
    if (FLASH_SIZE > (1024u * 1024u)) return false;  /* 2 MB part is inherently dual */
    return ob_program_and_launch(OB_USER_DUALBANK, OB_DUALBANK_DUAL);
}

/* ── flash erase/program helpers (inactive bank) ──────────────── */

static bool flash_erase_pages(uint32_t firstPage, uint32_t nPages)
{
    FLASH_EraseInitTypeDef e = {0};
    e.TypeErase = FLASH_TYPEERASE_PAGES;
    /* Erase targets the INACTIVE bank (high window 0x08080000+). HAL erase wants
     * the PHYSICAL bank; under SWAP_BANK=1 the high window is physical bank 1,
     * otherwise bank 2. (Program uses the mapped address, so it follows swap.) */
    e.Banks     = ota_swap_enabled() ? FLASH_BANK_1 : FLASH_BANK_2;
    e.Page      = firstPage;
    e.NbPages   = nPages;
    uint32_t pageErr = 0;

    HAL_ICACHE_Disable();
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &pageErr);
    HAL_FLASH_Lock();
    HAL_ICACHE_Enable();
    return st == HAL_OK;
}

/* Program `len` bytes at virtual `addr` (must be 16-byte aligned) as quad-words,
 * padding any final partial quad-word with 0xFF. */
static bool flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    bool ok = true;
    HAL_ICACHE_Disable();
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    for (uint32_t i = 0; i < len && ok; i += 16) {
        uint32_t qw[4];
        uint8_t *p = (uint8_t *)qw;
        uint32_t n = (len - i >= 16) ? 16 : (len - i);
        memset(p, 0xFF, 16);
        memcpy(p, data + i, n);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr + i, (uint32_t)qw) != HAL_OK)
            ok = false;
    }
    HAL_FLASH_Lock();
    HAL_ICACHE_Enable();
    return ok;
}

/* ── receive state machine ────────────────────────────────────── */

bool ota_ab_begin(uint32_t image_size, uint32_t image_crc32)
{
    ota.state = OTA_ST_ERROR;
    ota.err = 0;
    printf("OTA: begin size=%lu crc=0x%08lX dualbank=%d swap=%d inactBase=0x%08lX bankSize=%lu\r\n",
           (unsigned long)image_size, (unsigned long)image_crc32,
           (int)ota_dualbank_enabled(), (int)ota_swap_enabled(),
           (unsigned long)ota_inactive_bank_base(), (unsigned long)FLASH_BANK_SIZE);
    if (image_size == 0 || image_size > OTA_MAX_IMAGE) { ota.err = 1;
        printf("OTA: REJECT bad size (max=%lu)\r\n", (unsigned long)OTA_MAX_IMAGE); return false; }
    if (!ota_dualbank_enabled()) { ota.err = 2;
        printf("OTA: REJECT dualbank off\r\n"); return false; }

    /* Erase the image region of the inactive bank (not the top cfg/health pages) */
    uint32_t nPages = (image_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    printf("OTA: erasing BANK2 pages 0..%lu\r\n", (unsigned long)(nPages - 1));
    if (!flash_erase_pages(0, nPages)) { ota.err = 3;
        printf("OTA: erase FAILED (err=0x%lX)\r\n", (unsigned long)HAL_FLASH_GetError()); return false; }

    ota.size = image_size;
    ota.crc  = image_crc32;
    ota.next_offset = 0;
    ota.state = OTA_ST_RECEIVING;
    printf("OTA: erase OK, receiving\r\n");
    return true;
}

bool ota_ab_data(uint32_t offset, const uint8_t *data, uint16_t len)
{
    if (ota.state != OTA_ST_RECEIVING) return false;
    if (offset != ota.next_offset) return false;          /* lock-step: ignore out-of-order */
    if (offset + len > ota.size) { ota_ab_abort(4); return false; }

    if (!flash_program(ota_inactive_bank_base() + offset, data, len)) {
        printf("OTA: program FAILED at 0x%08lX (err=0x%lX)\r\n",
               (unsigned long)(ota_inactive_bank_base() + offset),
               (unsigned long)HAL_FLASH_GetError());
        ota_ab_abort(5);
        return false;
    }
    uint32_t prev = ota.next_offset;
    ota.next_offset += len;
    if ((prev >> 15) != (ota.next_offset >> 15))   /* ~every 32 KB */
        printf("OTA: rx %lu/%lu\r\n", (unsigned long)ota.next_offset, (unsigned long)ota.size);

    if (ota.next_offset >= ota.size) {
        /* Whole image programmed — verify what actually landed in flash */
        uint32_t crc = spi_crc32(0, (const uint8_t *)ota_inactive_bank_base(), ota.size);
        if (crc != ota.crc) {
            printf("OTA: CRC FAIL got=0x%08lX want=0x%08lX\r\n",
                   (unsigned long)crc, (unsigned long)ota.crc);
            ota_ab_abort(6); return false;
        }
        ota.state = OTA_ST_READY;
        printf("OTA: image complete + CRC OK — ready to commit\r\n");
    }
    return true;
}

void ota_ab_abort(uint8_t err)
{
    ota.state = (err ? OTA_ST_ERROR : OTA_ST_IDLE);
    ota.err = err;
    ota.next_offset = 0;
}

bool ota_ab_commit(void)
{
    if (ota.state != OTA_ST_READY) { printf("OTA: commit ignored (state=%d)\r\n", ota.state); return false; }

    /* No config carry-forward: config/health live in the inactive bank's top
     * pages, which the A/B image write never touches, and config is re-synced
     * from the ESP32 (the config master) after the swap. Carrying here would
     * require writing the active bank (RWW-illegal) anyway. */
    printf("OTA: commit\r\n");

    /* Arm confirm-or-rollback BEFORE the swap so it survives the reset */
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, OTA_BKP_PENDING, OTA_PENDING_MAGIC);
    HAL_RTCEx_BKUPWrite(&hrtc, OTA_BKP_BOOTCNT, 0);

    /* Flip SWAP_BANK to the opposite of the current state + reset */
    uint32_t newSwap = ota_swap_enabled() ? OB_SWAP_BANK_DISABLE : OB_SWAP_BANK_ENABLE;
    printf("OTA: swapping bank (swap %d->%d) + reset\r\n",
           (int)ota_swap_enabled(), (int)(newSwap == OB_SWAP_BANK_ENABLE));
    if (!ob_program_and_launch(OB_USER_SWAP_BANK, newSwap)) {
        printf("OTA: swap option-byte program FAILED\r\n");
        /* swap failed — disarm so we don't rollback a still-good current image */
        HAL_RTCEx_BKUPWrite(&hrtc, OTA_BKP_PENDING, 0);
        ota_ab_abort(9);
        return false;
    }
    return true;  /* unreachable on success */
}

/* ── confirm / rollback ───────────────────────────────────────── */

bool ota_ab_pending_confirm(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, OTA_BKP_PENDING) == OTA_PENDING_MAGIC;
}

void ota_ab_boot_check(void)
{
    HAL_PWR_EnableBkUpAccess();
    if (HAL_RTCEx_BKUPRead(&hrtc, OTA_BKP_PENDING) != OTA_PENDING_MAGIC)
        return;  /* not on trial */

    /* AUTO-ROLLBACK TEMPORARILY DISABLED.
     *
     * The original logic rolled back to the other bank after N un-confirmed
     * trial boots. Two problems made it actively harmful:
     *  1) It decided at the very START of boot, before the image could run and
     *     self-confirm — so a perfectly good image with a stale BOOTCNT (e.g.
     *     left high by an earlier crash, or inherited by a J-Link flash that
     *     doesn't reset the RTC backup counter) got swapped away instantly.
     *  2) Confirmation depended on the ESP, which is asleep in the field
     *     (laser-wake) — so good images falsely rolled back.
     * On the bench this trapped every debugger flash, reverting it to an old
     * bank. Auto-rollback is a FIELD-OTA recovery feature, not something that
     * should second-guess a debugger flash, and it needs a proper
     * image-identity-based redesign (tie the trial to the specific image, not a
     * bare counter) before it's trustworthy. Until then: clear the trial state
     * and let the image run. Dual-bank A/B SWAP itself is unaffected. */
    uint32_t cnt = HAL_RTCEx_BKUPRead(&hrtc, OTA_BKP_BOOTCNT);
    printf("OTA: trial image (swap=%d, prior attempts=%lu) — clearing trial, "
           "auto-rollback disabled\r\n", (int)ota_swap_enabled(), (unsigned long)cnt);
    HAL_RTCEx_BKUPWrite(&hrtc, OTA_BKP_PENDING, 0);
    HAL_RTCEx_BKUPWrite(&hrtc, OTA_BKP_BOOTCNT, 0);
}

void ota_ab_confirm(void)
{
    if (ota_ab_pending_confirm()) {
        HAL_PWR_EnableBkUpAccess();
        HAL_RTCEx_BKUPWrite(&hrtc, OTA_BKP_PENDING, 0);
        HAL_RTCEx_BKUPWrite(&hrtc, OTA_BKP_BOOTCNT, 0);
        printf("OTA: image CONFIRMED (checked in over SPI)\r\n");
    }
}
