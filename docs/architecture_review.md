# STM32 Firmware — Architecture Review (2026-09-20)

Why a device whose job is "wake, record some chunks, sleep" keeps producing
races and recovery patches. Written while chasing the config-reversion bug and
ladder step 02; companion to [field_test_fixes.md](field_test_fixes.md), which
tracks the individual defects.

**This is an assessment, not a plan of record.** Nothing here is scheduled and
no refactor is authorised.

## The case against a rewrite

State it first, because it bounds everything below:

- **0.10.17 ran the 30-day field test and rotated chunks reliably.** The core
  loop works.
- Ladder steps 00 and 01 pass on the bench (2026-09-20).
- The defect list is not "it doesn't work" — it is a list of *specific* races
  and missed re-inits, each traceable to one of the three items below.

The foundation is sound. What follows is about rules that were never stated,
not about the design being beyond repair.

## Defect 1 — no single owner per resource

The same resource is driven from several tasks, and whether that is guarded is
decided case by case:

| Resource | Tasks that touch it | Guard |
|---|---|---|
| FatFS / SD files | Audio (6 call sites), CLI (8), GPS (3) | `fileMtx` — 19 acquires vs 17 `f_*` calls |
| Config/health flash | CLI (`app_freertos.c:1114,1141`), GPS (`:2027,2079`), Bridge (`:2338,2422`) | **none** |
| SD SPI bus | Audio write loop; CLI format/mount/eject | via `fileMtx` only |
| `cfg` struct | written by all three of the above | **none** |

`flashWritePage()` toggles global hardware state — `HAL_ICACHE_Disable()`,
`HAL_FLASH_Unlock()`, erase, program, `HAL_FLASH_Lock()`. Two tasks inside it
at once leaves an erased page with nothing programmed into it.

**That is exactly the config-reversion bug**, and the asymmetry is the proof:
`healthSave()` is called only from the Bridge task and health survived the
2026-09-20 boot (`boots=48`) while the config page one page below it did not.

Because the rule is inconsistent, every new feature has to rediscover it — and
the cost of getting it wrong is invisible until a field unit loses its name.

## Defect 2 — Stop 2 resume is not symmetric

Stop 2 destroys peripheral state. Restoration is scattered across whichever
call site remembered, so "is everything back?" cannot be answered by reading one
function. The consequences are already on the defect list as separate items,
but they are one defect:

- ADC / SPI2 / USART3 never re-init after a wake (#6).
- Audio DMA silently not restarted after a wake — `audioStarted` stays 0, the
  task blocks on `audioDmaSem` forever, the file gets ~118 KB of ring residue.
- SHT30 read runs against a PERIPH rail that is still off between the wake and
  `powerEnterRecord()`.

There is no single `suspend()` / `resume()` pair naming every peripheral in
order, so each of these had to be found in the field rather than by inspection.

## Defect 3 — recovery layers compensating for 1 and 2

`I2C_Recover`, `SPI_Recover`, `ADC_Recover`, record-through remount, the
in-flash error log, the ESP32 NRST watchdog, the HardFault self-reset. Each is
defensible alone. Together they are a lot of machinery for a timer-driven
recorder, they are most of what has been hard to test, and they add failure
modes of their own — **the ESP32 watchdog can assert NRST mid-flash-write**,
which is one of the two candidate causes of the config loss (the other being
Defect 1).

Recovery code is the symptom. Ownership is the disease.

## Scale check

Six tasks — audio, CLI, format, GPS, bridge, infer — for a device that does one
thing at a time. `app_freertos.c` is 3155 lines. Three tasks would cover it:

- **recorder** — owns audio DMA, FatFS and the SD bus.
- **system** — owns config/health persistence, power state, schedule.
- **bridge** — owns SPI2 and nothing else; translates to messages.

Everything else becomes a message. No task touches a peripheral it does not
own; no task writes another's state directly.

This is a target to steer toward, not a rewrite to schedule.

## Suggested order, if this is taken up

Each step is small, independently testable, and useful on its own. **Do not mix
these into ladder steps** — batching is what made the July regressions
undebuggable (`6461b03`).

1. **Make flash single-owner.** A mutex in `flashWritePage()`, or route every
   persist through one task. Fixes the config reversion outright.
2. **One `suspend()` / `resume()` pair** listing every peripheral in order.
   Likely collapses defect #6, the audio-DMA-after-wake bug, and part of the
   SHT30 failures into a single change.
3. **Then** consider folding the task count down, once ownership is explicit
   enough that the reduction is mechanical rather than a redesign.

Steps 1 and 2 are worth doing whatever happens to step 3.

## Related

- Config-reversion bug and the voided step-02 run:
  [field_test_fixes.md](field_test_fixes.md).
- SD bus runs at 5 MHz (`user_diskio.c:196`) against a 288 KB/s stream — about
  half the bus, which is why every `f_write` exceeds the 10.67 ms DMA
  half-buffer deadline. Relevant to step 02's latency numbers, but a tuning
  question rather than an architectural one.
