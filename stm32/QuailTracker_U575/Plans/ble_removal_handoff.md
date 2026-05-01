# Phase B + C — Finish ripping out PB-03F BLE remnants

Phase A (this session) cleaned up the docs / app / BOMs. The STM32
firmware still has a few stale PB-03F-era code paths that build but no
longer do anything useful. This document is the handoff for the
firmware-side cleanup; do it in a session where you can flash a real
board and verify recording + GPS + ESP32 over SPI still work afterward.

Branch state at handoff: `main` at `b682fff`.

---

## Prompt to paste at the start of the next session

> Continue the QuailTracker BLE / PB-03F removal. Read
> `stm32/QuailTracker_U575/Plans/ble_removal_handoff.md` for context.
> Phase A landed in `b682fff` (companion app + docs cleanup). Today's
> goal is Phase B: remove the dead USART2 + GPIO PD10 + nanopb
> generated code from the STM32 firmware, then verify with `pio run`
> and a flash + recording bring-up. Phase C is a quick audit of
> `esp32_bridge/`: confirm the BLE beacon code is the wanted ESP32
> NimBLE feature and leave it alone.

---

## Phase B — STM32 firmware

### What's still there (the actual surface area is small)

The firmware mostly migrated already — the BLE state machine in
`app_freertos.c` was ripped out in earlier work. What's left is dead
peripheral init and a generated protobuf header that's no longer
consumed by anything:

1. **USART2 init in `Core/Src/stm32u5xx_hal_msp.c`** — `HAL_UART_MspInit`
   and `HAL_UART_MspDeInit` both have an `else if (huart->Instance ==
   USART2)` branch that enables USART2 clock + AF7 on PA2/PA3. Comment
   reads `/* BLE — USART2 on PA2 (TX) / PA3 (RX), AF7 */`. Lines
   ~255-280 (init) and ~305-315 (deinit). Delete both branches.

2. **`MX_USART2_UART_Init()` and `huart2`** — search `main.c` and
   `main.h`. If the prototype, the global handle, or the init call exist
   at all, delete them. (CubeMX generates these even if the peripheral
   is unused — when you regenerate, untick USART2 in the .ioc.)

3. **GPIO PD10 init in `Core/Src/main.c`** — `MX_GPIO_Init` writes
   `GPIO_PIN_10` on GPIOD high (line ~1605–1606). That's the legacy
   `BLE_EN` / `BLE_VCC` rail enable from the V4 PB-03F design. ESP32
   is on always-on 3V3 in V5, so this pin drives nothing. Delete the
   `HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, ...)` and the matching
   `GPIO_InitStruct` block.

4. **Nanopb generated code + middleware**:
   - `Core/Inc/quailtracker.pb.h`
   - `Core/Src/quailtracker.pb.c`
   - `Middlewares/Third_Party/nanopb/` (entire directory — pb.h,
     pb_common.{c,h}, pb_decode.{c,h}, pb_encode.{c,h})
   - The .proto source isn't in the repo. If you find one locally
     (e.g. `tools/quailtracker.proto`), delete it too.

5. **`platformio.ini`** lines 39 and 72:
   ```
   +<stm32/QuailTracker_U575/Middlewares/Third_Party/nanopb/>
   -Istm32/QuailTracker_U575/Middlewares/Third_Party/nanopb
   ```
   Remove both.

6. **`device_state.h`** — `bleConnected` (uint8_t) at line ~159 stays.
   The ESP32 still tells the STM32 "a BLE client connected" over SPI,
   so the field is still meaningful. Just update the comment to say
   "set by ESP32 over SPI" instead of anything implying PB-03F.

7. **`Core/Inc/main.h`** — bump `FW_VERSION`. Per project convention,
   patch-level bump (e.g. 0.9.10 → 0.9.11).

### Likely non-issues — verify but don't go looking for trouble

- `app_freertos.c:995` already prints `"BLE Beacon: Managed by ESP32"`
  in the boot banner. Leave it — it's accurate.
- The "BLE/WiFi name" comment near line 2261 in `app_freertos.c` is
  about deriving a unique device name; still relevant for ESP32. Leave.
- `device_state.h` enum value `PWR_USER_CONNECTED = 2` ("User connected
  via BLE/WiFi") — accurate, leave.

### Build + bring-up checklist

After the deletions:

```bash
pio run -t clean
pio run                    # must pass with zero warnings
pio run --target upload    # via J-Link
```

On the target, verify:
- Boot banner reaches the FW_VERSION line (no Error_Handler hang from
  missing peripheral init).
- A manual recording starts, writes a file to SD, and stops cleanly.
- GPS time/PPS still ticks (USART1 + EXTI8).
- Stop 2 sleep entry/wake still works (RTC wake at the next scheduled
  recording window).
- ESP32 still talks to STM32 over SPI2 (PB12-PB15) — you should still
  see device-state polling happening.

If anything breaks, the most likely culprit is a leftover `extern
UART_HandleTypeDef huart2;` somewhere or a stale `MX_USART2_UART_Init()`
call left in `main()`.

### Suggested commit shape

One commit, message along the lines of:

```
Phase B: remove dead USART2 / PD10 / nanopb from firmware

V5 hardware uses ESP32-C3 over SPI2 for all wireless config.
USART2 + GPIO PD10 (BLE_VCC) + nanopb generated code are no
longer referenced by any active code path. FW_VERSION bumped to
0.9.11.
```

---

## Phase C — esp32_bridge audit (small)

The ESP32 has its own NimBLE stack and runs a real BLE beacon. Active
references in `esp32_bridge/src/main.c`:

- `ble_beacon_init(device_name, on_ble_connect, on_ble_disconnect)`
- `ble_beacon_set_name(device_name)`
- `ble_beacon_update_data(env_battMv, ...)`

This is the **kept** BLE feature — proximity advertisement + battery
level so a phone can spot a station from a few meters away. Do **not**
delete `esp32_bridge/src/ble_beacon.{c,h}`.

What to confirm:
1. `ble_beacon.c` does not reference PB-03F or any STM32-side BLE
   protocol structures (it shouldn't — it talks NimBLE directly).
2. The advertised name format is what we want long-term (currently
   driven by `device_name` which derives from the STM32 stationId).
3. No leftover stub / dead code branches from the migration.

If everything looks clean, no commit is needed for Phase C. If a tiny
cleanup falls out, commit it separately so it's distinguishable from
the firmware-side B work.

---

## Why this is split off from Phase A

Phase A was pure docs / config / dead-tree deletion — no chance of
breaking the running firmware. Phase B touches MSP init code that the
HAL calls from `HAL_Init()` and `HAL_UART_Init()`; if a stale
`MX_USART2_UART_Init()` call survives the cleanup, the firmware will
hard-fault at startup before anyone sees a serial print. That's why
this is a flash-the-board phase, not a "merge and trust the build"
phase.
