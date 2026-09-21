# 30-Day Field Test — Defects, Fixes, and the Re-Apply Ladder

Single source of truth for what the long-term field test broke, what each
candidate fix does, and which ones have actually been tested on hardware.

**Baseline:** STM 0.10.17 (`6461b03` holds that tree) ran the 30-day field test
and rotated chunks reliably. Every version above it is a candidate, not a
known-good build.

**`main` follows the revised ladder** (since 2026-09-20): its firmware tree is
R01 (0.12.0), i.e. the 0.10.17 baseline plus the one change hardware has
passed. The old ladder's fixes #1-#4 are **not** on `main` any more — they
return as R03 onward, or get re-derived inside R02. `main` is always the tested
line; anything else lives on its branch until it passes the protocol.

**Rule (from `6461b03`):** re-apply one fix at a time, with a hardware test
between each. Batches get debugged backwards and cost days.

## Status at a glance

| # | Defect seen in the field | Fix | Ver | On main? | Tested on HW? |
|---|--------------------------|-----|-----|----------|---------------|
| 1 | Temp/humidity frozen at last value | SHT30 fail-loud + `I2C_Recover` | 0.10.18 | yes | **PASS 09-20** |
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

> **Why these keep appearing:** see [architecture_review.md](architecture_review.md)
> — resource ownership was never stated, so each feature adds a race and each
> race gets a recovery patch.

## Open bugs found while investigating (not from the field test)

- **Health page lost between the R04 and R05 runs (2026-09-21) — cause known.**
  Boot showed `Health: Loaded from flash (boots=1, files=0)` where the previous
  run had `boots=63, files=37`; config one page below survived (`seq=23`). No
  stats reset was performed. **Cause: flashing over SWD while the unit was
  running.** The upload resets the MCU, and `healthSave()` runs every 5 minutes
  from the Bridge task; a reset landing inside its erase/program window leaves
  the page erased, so the next boot resets the counters and stores `boots=1`.
  Same mechanism the audit flags for the ESP32 NRST watchdog. R01 fixed the
  concurrency half of this; a reset mid-write needs redundancy or fewer writes,
  not a mutex.

  **Exposure tracks write frequency**, which explains the asymmetry in both
  directions: health writes ~288x/day and died; config, which since R01 writes
  only on real change, survived. Before R01 config was written on every adopt
  and every 100 GPS fixes — and config was the page that kept dying.

  **Follow-up candidate:** a mount-time pass could truncate and re-header the
  oversized files left by a dead-card stop, once the card works again (the
  abandoned line had orphan-chunk handling, `079e3ab`). Low priority: `flac -d
  -F` already recovers the audio.

  **Fix if wanted (not yet written):** apply R01's skip-unchanged test to
  `healthSave()` and lengthen the cadence; most 5-minute saves rewrite a page
  that has barely changed. A/B pages would be the thorough fix but are more
  machinery than telemetry warrants. **Meanwhile: do not flash a running unit**
  — power it down or accept losing the counters.

- **Audio DMA is not restarted after a Stop 2 wake, silently.** `enterStop2()`
  restarts the MDF stereo DMA only when both `HAL_MDF_AcqStart_DMA` calls
  return `HAL_OK`, and logs nothing when they don't. `audioStarted` stays 0, no
  DMA callback fires, the audio task blocks on `audioDmaSem` forever, and the
  recording gets only the ring residue (~1 s, ~118 KB). From outside it looks
  like the unit went to sleep: no LED, no RTT, no SWD — while the bridge task
  keeps logging. **Seen on 0.10.23 and on 0.10.22 (`main`)**, always after a
  Stop 2 wake. Root cause not yet established; branch
  `fix-audio-restart-after-stop2` only makes the failure loud.
- **Config reverts to the UID-derived station name (`QT_xxxx`).** Seen on
  0.10.19-diag, 2026-09-20: `Config: Invalid/empty - writing defaults` at boot
  while `Health: Loaded from flash (boots=48)` — the page below survived.
  `configSave()` erases + programs the single config page from **three tasks
  with no lock** (CLI `app_freertos.c:1114,1141`; GPS survey-in `:2027,2079`,
  which saves every 100 fixes; Bridge `:2338,2422`, on every adopt), and
  `healthSave()` runs only from the Bridge task — which is why health lives and
  config dies. A reset landing between the erase and the programming does the
  same thing, with no second copy to fall back on (A/B went out with the
  dual-bank OTA in v0.10.0 and is not coming back). It then becomes permanent:
  defaults take `cfg_seq = 1`, `configSave()` bumps it to 2, and "higher seq
  wins" (`spi_bridge.c:165`) pushes the UID name out to the ESP32, overwriting
  the last good copy. `config_apply()` also never refreshes `deviceStationId`,
  so filenames keep the boot-time name even after a good config is adopted.
  **Fix (not yet written):** serialise the flash writes; skip the write when
  nothing changed; load defaults with `cfg_seq = 0` and delay persisting them so
  the ESP32's copy wins instead of being overwritten; refresh `deviceStationId`
  on adopt. **This invalidated the 2026-09-20 step-02 run** — see the results
  log.

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

| 01 | 0.10.18 | 2026-09-20 | **PASS.** Woke 18:13:00, 4 chunks x 300 s (51.5/51.6/51.8/51.6 MB), rotations, clean stop 18:33:00, slept. SHT30 failures now visible and clustered at sleep/wake (unpowered rail — step 05); `I2C_Recover` fired once after 2 strikes and recovered. |

| R01 | 0.12.0 | 2026-09-20 | **PASS.** Slept into Stop 2 before the window and woke into it. 4 chunks x 300 s (49.7/49.5/49.6/49.1 MB at gain 6 = 165 KB/s), full duration on every chunk, **Ring Overruns 0**, clean stop 00:40:00, slept, woke on ESP32. `Config: Loaded from flash (station=QT004, seq=12)` — page survived; seq moved only 10->12 across several boots and setting changes, so skip-unchanged is not churning. A second window was captured end to end: `Entering Non-Record sleep` 00:44:00 (60 s) -> `Woke from Stop 2 (RTC)` 00:45:01 -> `Entering Scheduled Record` -> 3 chunks x 300 s (49.18/49.12/48.73 MB) -> clean stop 01:00:00 -> sleep 36300 s. Full-length audio through the wake path, which is where 0.10.22/0.10.23 lost it. |

| 02 | 0.10.19-diag | 2026-09-20 | **INVALID — retest.** The config page was lost at boot, so the run used defaults: `chunkMinutes = 30` against a 20-minute window, i.e. **zero chunk rotations** — the suspected failure path was never exercised. What it did show: one 1200 s file (196.7 MB, 164 KB/s), clean stop, slept after, **overruns +0**. Latency: fatfs write max 101.33 ms (14067/14067 calls > 10 ms), sync max 53.87 ms (1758/1758), card write max 97.99 ms over 26 sectors (17419 slow / 40826 calls). No CRC-retry lines at all, so no CRC errors fired. |

| R02 | 0.13.0 | 2026-09-20 | **PASS.** Three sleep/wake cycles: ESP32 wake, RTC wake into the window, post-window sleep + ESP32 wake. **`PWR: resume INCOMPLETE` never printed** — every peripheral restored. 4 chunks x 300 s (49.54/49.52/49.19/49.08 MB), **Ring Overruns 0**, clean stop 01:39:00. After the wakes: battery 3.899 V (ADC re-init + recalibration), temp/humidity live at 25.47 C / 51.7% (I2C1 re-init), ESP32 bridge Ready with 5055 transactions (SPI2 re-init). |

| R03 | 0.14.0 | 2026-09-21 | **PASS, with a measured cost.** Window 09:45-10:05 after a Stop 2 wake. 4 chunks x 300 s (49.97/49.52/49.87/49.10 MB), clean stop, slept. **Ring overruns 0 -> 3 (~32 ms lost).** Exactly 3 mid-window chunk opens, 1 overrun each = one DMA half-buffer (10.67 ms) per `f_expand`. No `f_expand skipped` line, so pre-alloc succeeded every time; free space fell 189 MB for ~198 MB of files (not 4 x 86.5 MB), so `f_truncate` on close works. Compare: the CRC runs lost 14 s and 58 s with 1253 and 5406 overruns. Cluster allocation was the stall; it is now paid once per chunk instead of continuously. |

| R04 | 0.15.0 | 2026-09-21 | **PASS, with a measured cost.** Window 10:15-10:35 after a Stop 2 wake. 4 chunks x 300 s (49.89/49.21/49.39/49.46 MB), clean stop, slept. **Ring overruns 18 (~192 ms)** — +15 over R03's 3, so the CRC path costs ~160 ms per 20-minute window. `SD init: data CRC ENABLED (CMD59 resp=0x00)` at every mount including after each wake. **Zero retries and zero CRC mismatches**, as in every previous run. Compare step 02 (same change, no pre-alloc): 1253 and 5406 overruns, 14 s and 58 s lost. Confirms the reorder: cluster allocation was the dominant stall. |

| R05 | 0.16.0 | 2026-09-21 | **PASS on the normal path.** Window 13:26-13:46 after a Stop 2 wake. 4 chunks x 300 s (49.55/50.04/50.18/49.53 MB), rotations, clean stop, slept. Ring overruns 24 (R04 18, R03 3) — **treat these as single samples with unknown variance**; R05 changes nothing in the write path except the untaken failure branch. No write errors, so the finalise path did not run: it needs the injection fixtures. **Also observed: health stats reset between runs** (`boots=1, files=0`, was `boots=63, files=37`) while config survived (`seq=23`) — see open bugs. |

| R05 failure path | 0.16.3 | 2026-09-21 | **PASS, after three wrong fixes.** Verified with SD write-error injection (`r05-inject-test`), because five clean runs had never produced a write error. Each attempt looked correct in the log and was wrong on the card: **0.16.0** — `f_truncate` refused, because FatFS latches the error on the FIL (`fp->err`, ff.c:455/3627/4485) and every later call on that handle returns it; file left at the full 86.5 MB. **0.16.1** — reopened through a fresh handle, but STREAMINFO still promised the samples of the block the failed write took, so `flac -t` stopped at END_OF_STREAM. **0.16.2** — declared unknown length, still refused: the file was 12,698 bytes longer than header + accounted audio, because a failed `f_write` advances `fptr` by the sectors it committed, so the file ended mid-frame. **0.16.3** — truncate at `f_tell() - bw` (the start of the failed write = a frame boundary): file 2,129,967 B = audio + 2,358 B header/seektable, `flac -t` "tested 618496 samples ... ok". Dead-card fixture bails at `reopen` as intended. **Lesson: the log line was wrong three times; only decoding the file settled it.** |

| R05 dead-card path | 0.16.3 | 2026-09-21 | **PASS.** Every write failing from #400: `REC: finalise stopped at reopen — 2159168 bytes of audio are on the card`. Bails immediately instead of grinding, as intended. The file stays at the full 86.5 MB with the placeholder header (nothing can be written when the card is gone) and `flac -t` refuses it — but **`flac -d -F` recovers all 13.1 s of audio**, matching what the firmware reported. So the unrecoverable case costs convenience, not data, and needs no custom repair tool. |

Baseline numbers for comparison: a healthy 5-minute chunk is **~52 MB**. A
~118 KB file means the DMA never restarted and only the ring residue was
written.

**Conclusion so far:** the Stop 2 wake path is sound at 0.10.17, so the audio
death after wake is introduced by one of steps 01-04 — all of which are on
`main` today.

## DECISION 2026-09-20 — rebuild from 0.10.17 with ownership fixed first

**The original ladder (steps 02-04) is stopped.** It is not being continued to
step 13.

**Why.** Five of the thirteen fixes are compensations for two architectural
defects rather than fixes in their own right — see
[architecture_review.md](architecture_review.md). #6 (ADC/SPI2/USART3 re-init
after wake) is pure compensation and a symmetric `resume()` removes the need for
it entirely; #1, #3, #4 and #7 are partly the same. Re-applying those one at a
time onto a baseline that still has the defects means testing patches whose
value depends on the bug still being there — and every such result is about code
that the ownership work then replaces. Testing once, on the code that ships, is
strictly less work than testing twice.

**Why it is safe to stop the hunt.** The open question was which step kills audio
after a Stop 2 wake. **Step 00 passed the full protocol including a Stop 2 wake**
(2026-09-20), so 0.10.17 is known good on exactly that path and the bug is inside
01-04 — all of which are dropped or re-applied one at a time below. It resurfaces
on the way back up, under the same protocol, with the step that caused it named.
If the culprit is step 02 (the prime suspect for the chunk-rotation hang) rather
than the resume path, the refactor would never have fixed it and the ladder
catches it when CRC is re-applied.

**What is preserved.** 0.10.17 (`6461b03`) stays the fixed point. The test
protocol and pass criteria above are unchanged, so the two passes already
recorded stay comparable. The stopped ladder's findings live on branch
`diag-step02-write-latency`.

**Unchanged rule:** one change at a time, hardware test between each — including
the two refactor steps. The refactor gets laddered like everything else.

### Revised ladder

| Step | From | Adds | Status |
|------|------|------|--------|
| R00 | `6461b03` (0.10.17) | baseline — the 30-day build | PASS 2026-09-20 |
| R01 | R00 | **flash single-owner**: mutex in `flashWritePage()` (or one owning task); skip the write when nothing changed; defaults load with `cfg_seq = 0` and are not persisted immediately, so the ESP32 copy wins; `config_apply()` refreshes `deviceStationId` | **PASS 2026-09-20** — 0.12.0 (`9dc8a9e`). Slept into Stop 2 before the window, 4 chunks x 300 s (49.7/49.5/49.6/49.1 MB), **overruns 0**, clean stop, config intact (`seq=12`), no flash-write failures. Defaults/`cfg_seq=0` path not yet triggered (needs a real config loss). |
| R02 | R01 | **one `suspend()` / `resume()` pair** naming every peripheral in order — subsumes #6, and is the prime candidate for the audio-DMA-after-wake bug and part of the SHT30 failures | **PASS 2026-09-20** — 0.13.0 (`a3d3492`). 3 sleep/wake cycles (ESP32, RTC, post-window), no `resume INCOMPLETE`, 4 chunks x 300 s, overruns 0 |
| R03 | R02 | **`f_expand` pre-alloc + 15 s sync cadence** (was #5) — *pulled ahead of the CRC step, see below* | **PASS (with a cost) 2026-09-21** — 0.14.0 (`fe1db2e`). 4 chunks x 300 s, **3 overruns (~32 ms)** where R02 had 0 — one per mid-window chunk open, i.e. `f_expand` writing the FAT chain. Truncate confirmed. |
| R04 | R03 | SD data CRC + CMD59 + CRC7 + retry (was #2 / step 02) | **PASS (with a cost) 2026-09-21** — 0.15.0 (`5fe599a`). 4 chunks x 300 s, **18 overruns (~192 ms)** vs R03's 3. The same change lost 14 s and 58 s as step 02, before R03 removed the allocation stalls. |
| R05 | R04 | **finalise on write failure** — *replaces the planned record-through (#7)* | **PASS 2026-09-21** — 0.16.3. Normal path: 4 chunks x 300 s, clean stop. Failure path verified with injected write errors: recoverable error -> truncated, header-patched file that `flac -t` decodes (618496 samples, ok); dead card -> bails at `reopen`, audio left on the card. |
| R06 | R05 | diagnostics: in-flash error log, SPI surface, health `sdErrors`, crash capture (was #8-#12) | **written** — branch `r06-diagnostics`, 0.17.0 (`26e3513`), `bisect_bins/R06_v0.17.0_diagnostics.bin`; **pending hardware test**. Self-reset NOT re-applied (audit default); SPI2 failure is logged but not recovered. |

**Reordered 2026-09-21:** `f_expand` (was R04) now comes before the SD CRC work
(now R04). The old step-02 runs lost audio to ring overruns concentrated in the
**first chunk after a window opened** — where the file is being grown cluster by
cluster. The CRC arithmetic costs ~125 us per 512 B block, which cannot stall a
write past the 10.67 ms that overruns the ring, so cluster allocation is the
better explanation. Testing pre-allocation first also means the CRC step lands
on a write path that no longer stalls, and can be measured honestly.

**Dropped:** #6 (ADC/SPI2/USART3 re-init) — subsumed by R02.

**Handled inside R02 (2026-09-20):** #6 (ADC/SPI2/USART3 re-init) is subsumed
by `pwrResume()`; #4 (PRIMASK wakeup guard) was re-derived in the entry sequence
rather than re-applied. R02 also re-inits I2C1 on wake, which may make #1's
`I2C_Recover` unnecessary — decide after the R02 run and step 05.

**Re-evaluate after R02, do not re-apply blind:** #1 (SHT30 fail-loud +
`I2C_Recover`), #3 (ICACHE ES0499), #4 (PRIMASK guard), #13 (audio stack
8->16 KB, already suspected to be an artifact). With symmetric suspend/resume
these may shrink to very little or become unnecessary.

**Still pending separately:** step 05 (`fix-sht30-read-path`, 0.11.0) — the
SHT30 rail gate and I2C bus clear. Re-evaluate against R02 for the same reason.

### Compensation audit — a gate after R02, before anything is re-applied

Almost the entire recovery stack lives in the rolled-back line, not in the
baseline: at `6461b03` only `SPI_Recover` (SD bus) and the stock HardFault
handler exist. So this is a gate on **re-applying** machinery, not on removing
working code — the default is **do not re-apply**, and each item has to earn its
way back in.

**For each mechanism, answer in writing before it returns:**

1. What failure does it handle, concretely?
2. Can that failure still occur once R01 (single-owner flash) and R02
   (symmetric suspend/resume) are in? If the failure was a symptom of either,
   the answer is no and the mechanism is dropped.
3. Is there field or bench evidence it ever fired *usefully* — a counter, an
   error-log row, an RTT line? "Might help someday" is not evidence.
4. What failure modes does it add? Recovery code runs at the worst moment by
   definition.

**The list:**

| Mechanism | Compensates for | Audit note |
|---|---|---|
| `I2C_Recover` (#1) | SHT30 reads failing | Root cause is the rail gate + PB7/SDA toggle (step 05), both of which R02 and step 05 address. Keep only if reads still fail with the rail up and PB7 left alone. |
| ICACHE ES0499 workaround (#3) | Stop 2 exit corruption | Silicon errata, not ownership — likely genuine. Confirm against REV_ID on the units actually in the field before carrying it. |
| PRIMASK wakeup guard (#4) | Stop 2 entry race | Belongs to the sleep/wake state machine that R02 rewrites. Re-derive it inside R02 rather than re-applying the patch. |
| ADC/SPI2/USART3 re-init (#6) | peripherals dead after wake | **Dropped** — R02 subsumes it by construction. |
| Record-through remount (#7) | a write error abandoning the window | **Dropped 2026-09-21.** No field evidence of write failures; five bench runs logged zero write errors, zero retries, zero CRC mismatches. It also remounts, opens a file and runs f_expand on a card that has just refused a write — and if the card is genuinely failing, writing more is unlikely to produce usable data. Replaced by R05 (finalise on failure), which fixes the real defect: the old path did a raw `f_close`, leaving an un-truncated ~86.5 MB file with a placeholder FLAC header. |
| SD retry loops (`SD_IO_RETRIES`, part of R04) | transient block errors | Arrives with CRC. **Four runs now (2026-09-20 and the R04 run) have logged zero retries and zero CRC mismatches** — the path has never been observed to fire on this card. Keep the CRC detection (it is the fix for the 35% corruption); the retry loop itself is still unexercised code on the critical path. |
| `SPI_Recover` (in baseline) | wedged SD SPI bus | Already there and cheap. Confirm it has ever fired; if not, it is untested code on the critical path. |
| ESP32 NRST watchdog (`STM32_WD_*`) | a hung STM32 | **Has its own failure mode** — it can reset mid-flash-write, one of the two candidate causes of the config loss. R01 makes that survivable; re-check the 30 s threshold against real stall times. |
| HardFault self-reset + TAMP capture (#12) | 30 s of downtime per fault | **Resolved 2026-09-21 in R06:** capture kept, self-reset **dropped**. With R01 and R02 in, the open question is whether faults still occur — the TAMP capture answers it, and a halted core is easier to attach to. The ESP watchdog still recovers a hung unit. Revisit if `ERR_HARDFAULT` rows appear in the field. |
| Audio stack 8 -> 16 KB (#13) | stack overflow at rotation | Already suspected to be an artifact of the batch. Measure high-water marks on R02 instead of guessing. |
| Error log, SPI surface, `sdErrors` (#8-#11) | no visibility | Diagnostics, not compensation. Keep — they are how the questions above get answered. |

**Rule:** anything that cannot answer questions 2 and 3 does not come back. If
it turns out to be needed later, the error log will say so, and it can be added
then against real evidence rather than a remembered fear.

### Test-run hygiene (until R01 lands)

The 2026-09-20 step-02 run was void because the config page was lost and the
unit ran on defaults (`chunkMinutes = 30` against a 20-minute window, so no
rotations). Until R01 is in:

- Treat **`Config: Invalid/empty` in the RTT log as an automatic void** — reset
  the config and rerun.
- Confirm `chunkMinutes = 5` actually stuck before the window opens.
- **RTT drops during Stop 2** and must be reconnected after the wake, so the
  pre-window sleep lines are normally missing from the capture. Absence of
  `PWR: Entering Non-Record sleep` is not evidence the unit stayed awake —
  check uptime at `REC start` instead (the tick does not run in Stop 2).

## The ladder (original — STOPPED at step 02, see the decision above)

Binaries live in `bisect_bins/ladder/`, numbered in apply order. Step 00 is the
30-day baseline; stop at the first step that fails. **Kept for the record:**
steps 02-04 are not being run; their fixes return as R03 onward.

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
