# Single-Bank Firmware Bring-Up & Provisioning

Procedure for provisioning a QuailTracker unit to the **single-bank** firmware
(STM `v0.10.0`+, ESP `v0.5.1`+) — the architecture that retired dual-bank A/B OTA.
Applies to new units and to re-provisioning existing dual-bank units.

## What changed (why this procedure exists)
- The STM is now **single-bank**: one firmware image, config/health at fixed top
  pages (`0x080FC000`/`0x080FE000`) that never move. No SWAP, no A/B, no rollback.
- The STM is updated **only** via the ESP ROM-bootloader flash (mass erase + write;
  the U5 SPI bootloader won't accept a page-list erase). A flash therefore **resets
  config — reconfigure after a firmware update.** The web UI has **one** "STM32
  Firmware Update" card.
- The self-heal watchdog escalates: **NRST reset ×2 → ROM reflash → give up**
  (sleep-gated, so a scheduled Stop-2 is never disturbed).

## Firmware artifacts (repo root, from `pio run`)
- `qt_esp_v0.5.1.bin` — merged ESP image, USB flash @ `0x0`
- `qt_esp_app_v0.5.1.bin` — app-only, for Wi-Fi OTA of an already-running ESP
- `qt_stm_v0.10.0.bin` — STM image, uploaded via the web UI

---

## Step 0 — Option bytes (ONE-TIME per unit, bench, J-Link/CubeProgrammer)
**The single-bank firmware requires `SWAP_BANK=0`.** Config is addressed in the
high window (`0x080FE000`) and erased as a fixed physical bank; that mapping is
only correct when the banks aren't swapped. A unit that was never A/B-swapped is
already `SWAP_BANK=0` and needs nothing here — but verify, because a unit that did
a real A/B swap can be left on `SWAP_BANK=1`, which would corrupt config.

1. Connect J-Link; open STM32CubeProgrammer (or read `OPTR` at `0x40022040`).
2. Confirm **`DUALBANK = 1`** (leave as-is — no geometry change) and
   **`SWAP_BANK = 0`**. (`OPTR` bit: `SWAP_BANK` = bit 20.)
3. If `SWAP_BANK = 1` → program it to **0** and apply (reloads option bytes + resets).
4. *(Optional, recommended for a unit that was stuck/looping)* full-chip erase to
   clear any stale config or half-written image before the clean flash.

> No J-Link on hand? Units that never committed an A/B swap are already
> `SWAP_BANK=0`; only the bench units that exercised A/B need this. If in doubt,
> check — it's a 30-second read.

## Step 1 — Flash the ESP over USB
1. Plug a **data** USB-C cable into the ESP32-C3.
2. Flash `qt_esp_v0.5.1.bin` to offset **`0x0`**:
   - Zero-install: <https://espressif.github.io/esptool-js/> (Chrome/Edge) → Connect
     → address `0x0` → `qt_esp_v0.5.1.bin` → Program.
   - Or: `cd esp32_bridge && pio run -t upload`
3. **Power-cycle** — full unplug (USB **and** battery), reconnect. (esptool's reset
   alone does not run the new ESP firmware.)

## Step 2 — Flash the STM from the web UI
1. Join the unit's Wi-Fi AP (`QT_XXXX`, open) and open **http://192.168.9.1**.
   - The Wi-Fi should stay connected (the AP-drop bug is fixed in this firmware).
2. STM32 version reads `--` (blank/old). In the **STM32 Firmware Update** card,
   choose **`qt_stm_v0.10.0.bin`** → **Upload & Flash STM32**.
3. The ESP runs the flash in the background and the card shows live progress —
   **"Flashing STM32 N% — keep powered, do NOT disconnect"** (~10–15 s) → on
   success **"✓ STM32 flashed — running 0.10.0"** and the heartbeat starts. The Wi‑Fi
   session stays up throughout (the flash no longer blanks the UI). If it ever shows
   a failure, leave the unit powered and re-flash — don't power-cycle mid-write.

## Step 3 — Configure
1. Set the **Station ID** (becomes the AP SSID on next reboot).
2. **GPS survey** (Ops → Start Survey, outdoors / clear sky) — needed for the
   sunrise/sunset schedule to compute windows. Wait for "survey complete".
3. Set the **schedule** (sunrise/sunset offsets and/or windows). Confirm the UI
   reports a valid schedule and arms sleep.

## Step 4 — Verify (bench)
1. **Short test window:** set a window that records in ~2 min and sleeps otherwise.
2. Watch the ESP serial log (`pio device monitor -b 115200`, port `usbmodem2*`):
   - STM enters Stop 2; **NO** `recovery flash` / `NRST reset` lines during sleep
     (the sleep-notice gate is working).
   - At the window, STM wakes and a recording appears on the SD card.
3. **`boots` increments and persists** across a power-cycle (it no longer sticks at
   1) — read it from the boot banner / health card. This is the at-a-glance signal
   that nothing is wiping flash.
4. **Config survives a power-cycle** (settings intact across reboots). Note: it does
   **NOT** survive a firmware re-flash — the ROM mass-erase wipes it, so a STM update
   resets station ID / schedule / gain (reconfigure after).
5. *(Optional)* Confirm the watchdog escalation: make the STM go silent when it
   shouldn't (e.g. hold it in reset / a crashing test build) → the ESP log shows
   `NRST reset 1/2`, `2/2`, then `recovery flash`, then "giving up".

## Step 5 — Roll out
- Repeat Steps 0–4 for all units.
- Merge `single-bank` → `main` once a unit passes Step 4.
- Subsequent updates are field-pushable with no J-Link: ESP via `/ota`
  (`qt_esp_app_*.bin`), STM via the web UI card. Step 0 is one-time only.
