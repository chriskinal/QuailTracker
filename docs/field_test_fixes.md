# 30-Day Field Test — Defects, Fixes, and the Re-Apply Ladder

Single source of truth for what the long-term field test broke, what each
candidate fix does, and which ones have actually been tested on hardware.

**Baseline:** STM 0.10.17 (`6461b03` holds that tree) ran the 30-day field test
and rotated chunks reliably. Every version above it is a candidate, not a
known-good build.

**Rule (from `6461b03`):** re-apply one fix at a time, with a hardware test
between each. Batches get debugged backwards and cost days.

## Status at a glance

| # | Defect seen in the field | Fix | Ver | On main? | Tested on HW? |
|---|--------------------------|-----|-----|----------|---------------|
| 1 | Temp/humidity frozen at last value | SHT30 fail-loud + `I2C_Recover` | 0.10.18 | yes | **no** |
| 2 | ~35% of files corrupt (silent bit errors) | SD data CRC + CMD59 + CRC7 + retry | 0.10.19 | yes | **no** |
| 3 | Stop 2 exit errata (ES0499, ICACHE) | ICACHE workaround + REV_ID readout | 0.10.20 | yes | **no** |
| 4 | Stop 2 entry wakeup race (ES0499) | PRIMASK guard | 0.10.21 | yes | **no** |
| 5 | SD metadata wear: `f_sync` ~2.6M times in 30 days | `f_expand` pre-alloc + 15 s sync cadence | 0.10.22 | no | no |
| 6 | ADC/SPI2/USART3 never re-init after Stop 2 | `ADC_Recover` / `SPI2_Recover` / USART3 re-init | 0.10.23 | no | no |
| 7 | A write error abandons the rest of the window | Record-through: remount + fresh file, bounded | 0.10.24 | no | no |
| 8 | No way to tell why a unit failed in the field | In-flash error log + ring buffer | 0.10.25 | no | no |
| 9 | Error log not reachable from the web UI | `SPI_CMD_GET_ERRLOG` + Health tab table | 0.10.26 | no | no |
| 10 | SD data loss invisible in health stats | `health.sdErrors` on hard loss | 0.10.27 | no | no |
| 11 | A dropped frame blanks the error-log overlay | Send the reply over ~6 frames | 0.10.29 | no | no |
| 12 | No crash cause after a reboot | TAMP-backed fault capture + self-heal reset | 0.10.30 | no | no |
| 13 | Stack overflow at chunk rotation | Audio task stack 8 KB → 16 KB | 0.10.31 | no | no — **may be an artifact of the batch, not a real defect** |

Fixes 1–4 are on `main` (0.10.22) because they map to observed field defects.
5–13 were rolled back on 2026-07-18 and are still deferred. The ESP32 bridge
(0.5.19) and `shared/` were never rolled back, so they already speak the
error-log and crash-capture protocol that the STM32 side no longer implements.

## Open bugs found while investigating (not from the field test)

- **Audio DMA is not restarted after a Stop 2 wake, silently.** `enterStop2()`
  restarts the MDF stereo DMA only when both `HAL_MDF_AcqStart_DMA` calls
  return `HAL_OK`, and logs nothing when they don't. `audioStarted` stays 0, no
  DMA callback fires, the audio task blocks on `audioDmaSem` forever, and the
  recording gets only the ring residue (~1 s, ~118 KB). From outside it looks
  like the unit went to sleep: no LED, no RTT, no SWD — while the bridge task
  keeps logging. **Seen on 0.10.23 and on 0.10.22 (`main`)**, always after a
  Stop 2 wake. Root cause not yet established; branch
  `fix-audio-restart-after-stop2` only makes the failure loud.
- **`SPI_CMD_HEALTH_RESET` has no handler on the STM32.** The rollback dropped
  the case while the ESP kept sending the command, so the web UI's stats reset
  has been a no-op since July. Restored on `diagnostics-reapply`.
- **SHT30 read failures were always happening, just silently.** 0.10.17's
  `sht30Read()` returns `void` and keeps the last values on error, so the
  baseline looks clean while failing exactly as much; 0.10.18 only made it
  audible. Root causes, fixed on branch `fix-sht30-read-path` (0.11.0, pending
  test): the 5 s periodic read runs while the PERIPH rail is off between a
  Stop 2 wake and `powerEnterRecord()`; `I2C_Recover()` reset only the master
  and never clocked a slave off SDA; and the PB7 toggle below. That branch also
  switches to a 5-sample burst read with median-based outlier rejection.
- **PB7 is toggled as a "blue LED" mel heartbeat** (`app_freertos.c`, in the
  mel path). PB7 is **I2C1_SDA** on this board — there is no LED there. Every
  mel hop toggles the I2C data line, which is a strong candidate for the SHT30
  read failures that defect #1 papers over. Present on `main`.

## Test protocol (run for every ladder step)

The failures this week only appear after a **Stop 2 wake**, so a test that
starts recording from a fresh boot proves nothing.

1. Flash the step's binary. Format or empty the SD card.
2. Set a schedule window a few minutes out, `chunkMinutes = 5`, and let the
   unit **sleep into Stop 2 before the window opens**.
3. Capture RTT for the whole run:
   `JLinkRTTLogger -device STM32U575VG -if SWD -speed 4000 -RTTChannel 0 ~/qt_rtt.log`
4. Pass requires all of:
   - `REC start` for each chunk, and one file per 5 minutes
   - each file ≈ 50 MB (≈168 KB/s measured at gain 7; **not** a 118 KB stub)
   - `Chunk rotation` lines in `logs/diag.log`
   - a clean stop at the window end, then `PWR: sleep iter=...`
   - temperature/humidity present and changing in the web UI
5. Record the result in the table above before flashing the next step.

## Results log

| Step | Ver | Date | Result |
|------|-----|------|--------|
| 00 | 0.10.17 | 2026-09-20 | **PASS.** Slept into Stop 2, woke 17:25:01, 4 chunks x 300 s (51.9/51.9/51.7/51.7 MB), rotations logged, clean stop, slept again, survived an ESP32 wake. Measured 173 KB/s; PPS rate 48047.91 Hz. |

Baseline numbers for comparison: a healthy 5-minute chunk is **~52 MB**. A
~118 KB file means the DMA never restarted and only the ring residue was
written.

**Conclusion so far:** the Stop 2 wake path is sound at 0.10.17, so the audio
death after wake is introduced by one of steps 01-04 — all of which are on
`main` today.

## The ladder

Binaries live in `bisect_bins/ladder/`, numbered in apply order. Step 00 is the
30-day baseline; stop at the first step that fails.

| Step | Commit | Version | Adds |
|------|--------|---------|------|
| 00 | `6461b03` | 0.10.17 | baseline — the 30-day build |
| 01 | `0113942` | 0.10.18 | SHT30 fail-loud + I2C recover |
| 02 | `63d9103` | 0.10.19 | SD data CRC + CMD59 + CRC7 + retry |
| 03 | `3066304` | 0.10.20 | ES0499 ICACHE Stop 2-exit workaround |
| 04 | `8a6e885` | 0.10.21 | ES0499 wakeup-race PRIMASK guard |

Step 04 plus the SPI2 PING-test removal is today's `main` (0.10.22).

**Test step 00 first.** If the baseline also loses audio after a Stop 2 wake,
then the bug predates every fix in this table and the ladder is not the place
to look — the wake path is.

## Pending steps (new work, not from the rolled-back line)

| Step | Branch | Ver | Adds | Tested? |
|------|--------|-----|------|---------|
| 05 | `fix-sht30-read-path` | 0.11.0 | SHT30 rail gate + real I2C bus clear + 5-sample burst read with outlier rejection + PB7/SDA toggle removed | **no** — `bisect_bins/step05_v0.11.0_sht30.bin` |

Version numbering: new work starts at **0.11.0**. 0.10.23-0.10.33 belong to the
rolled-back line and some of those binaries exist on thumbdrives and units, so
reusing those numbers would make two different builds report the same version.

## Tools (not fixes — never ship)

- `bisect_bins/qt_stm_v0.10.23-diag_swd.bin` — `main` plus: loud MDF restart
  failure, `audioStarted`/`dmaCallbackCount` printed at each REC start, and
  `DEBUG_KEEP_SWD`.
- `pio run -e stm32u575_swd` — keeps SWD alive in Stop 2 so J-Link can halt a
  sleeping (or tickless-idle) unit. Bench only; costs standby current.
