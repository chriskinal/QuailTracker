# PB-03F AT Command List

## Full Command Set (41 commands)

Captured via `AT+HELP` from test PB-03 module on USB-TTL adapter, 2026-03-11.
The production PB-03F on the QuailTracker board only lists 28 of these in
`AT+HELP`, but the missing 13 still work when sent directly.

| # | Command | Description | On prod PB-03F |
|---|---------|-------------|----------------|
| 1 | `AT` | Check whether UART is in AT mode | Yes |
| 2 | `ATE0` | Turn off echo | Yes |
| 3 | `ATE1` | Turn on echo | Yes |
| 4 | `AT+SYSIOMAP` | IO map configuration | Yes |
| 5 | `AT+SYSGPIOREAD` | GPIO read | Yes |
| 6 | `AT+SYSGPIOWRITE` | GPIO write | Yes |
| 7 | `AT+PWMCFG` | PWM set | Yes |
| 8 | `AT+PWMCFGS` | PWM set (variant) | Yes |
| 9 | `AT+PWMSTOP` | PWM stop | Yes |
| 10 | `AT+PWMDUTYSET` | PWM duty set | Yes |
| 11 | `AT+PWMDUTYSETS` | PWM duty set (variant) | Yes |
| 12 | `AT+HELP` | Function description | Yes |
| 13 | `AT+TRANSENTER` | Enter transparent mode | Yes |
| 14 | `AT+BLESTATE` | BLE state query | Yes |
| 15 | `AT+BLESEND` | BLE send data | Yes |
| 16 | `AT+BLEDISCON` | BLE disconnect | Yes |
| 17 | `AT+BLEMTU` | Set MTU | Yes |
| 18 | `AT+BLEADVEN` | Set advertising enable | Yes |
| 19 | `AT+BLECONINTV` | Set connection interval | Yes |
| 20 | `AT+BLESCAN` | BLE scan | Yes |
| 21 | `AT+BLECONNECT` | Connect to device | Yes |
| 22 | `AT+BLEAUTOCON` | Auto connect | Yes |
| 23 | `AT+BLEDISAUTOCON` | Disable auto connect | Yes |
| 24 | `AT+BLEAUTH` | Authentication | Yes |
| 25 | `AT+BLEIBCNDATA` | Set iBeacon data | Yes |
| 26 | `AT+BLEIBCNUUID` | Set iBeacon UUID | Yes |
| 27 | `AT+RESTORE` | Restore to factory default | Yes |
| 28 | `AT+RST` | Reset module | Yes |
| 29 | `AT+GMR` | Fetch module version | No* |
| 30 | `AT+BLEMODE` | Switch Master/Slave role | No* |
| 31 | `AT+BLENAME` | Change bluetooth name | No* |
| 32 | `AT+BLEMAC` | Change bluetooth address | No* |
| 33 | `AT+BLERFPWR` | Change RF power | No* |
| 34 | `AT+UARTCFG` | Change UART baud rate | No* |
| 35 | `AT+BLEADVDATA` | Change advertising data | No* |
| 36 | `AT+SLEEP` | Change power mode | No* |
| 37 | `AT+BLEADVINTV` | Change advertising interval | No* |
| 38 | `AT+BLESERUUID` | Set service UUID | No* |
| 39 | `AT+BLETXUUID` | Set transfer UUID | No* |
| 40 | `AT+BLERXUUID` | Set RX UUID | No* |
| 41 | `AT+LEDTEST` | LED test | No* |

*Not listed in production PB-03F `AT+HELP` but commands still work when sent.

## Sleep Modes (AT+SLEEP)

Full syntax: `AT+SLEEP=<mode>[,<wakeup source>,<param1>,<param2>]`

| Mode | Current | Advertising | UART Wake | Auto-boot |
|------|---------|-------------|-----------|-----------|
| 0 | 275 µA | Yes | No* | No |
| 1 | ~2 mA | Yes | Yes | Yes (persists in EEPROM!) |
| 2 | 50 µA | No | Yes | No |
| 3 | Normal | Yes | Yes | N/A (default) |

*UART wake in mode 0 may work in real circuit — USB adapter TX backfeeds power into
module RX pin, preventing proper sleep/wake testing. Disconnect adapter TX to test.

Wakeup source (modes 0/1/2 only): 0=timer, 2=GPIO
- Timer: param1 = interval in ms
- GPIO: param1 = pin number, param2 = trigger (0=low, 1=high, 2=falling, 3=rising, 4=both)

**Mode 0 is the recommended sleep mode for QuailTracker:**
- 275µA with BLE advertising active
- Phone can discover and connect
- On connect, module sends `+EVENT:BLE_CONNECTED` over UART TX
- That start bit wakes STM32 from Stop 2 via USART2 RX
- Total system sleep: ~280µA (BLE 275µA + MCU Stop 2 ~5µA)

## URC Events (Unsolicited Result Codes)

The module sends these automatically over UART:
- `+EVENT:BLE_CONNECTED` — BLE client connected (followed by `>` for transparent mode)
- `+EVENT:BLE_DISCONNECT` — BLE client disconnected

These fire in all sleep modes. Confirmed working in sleep mode 0 (2026-03-11).

## AT+BLESTATE — Connection Status Query

- `AT+BLESTATE?` → `+BLESTATE:0` (disconnected) or `+BLESTATE:1` (connected)
- Does NOT work in transparent mode (after connect). Send `+++` to exit transparent mode first.

## AT+BLEIOCON — Connection Status GPIO (NOT available)

Documented in Ai-Thinker combo module manual (section 5.1.13) but **not implemented
in PB-03F firmware**. Command returns ERROR. Use UART event approach instead.

## Safety Notes

- **`AT+RESTORE`** — Listed but DANGEROUS. Previously appeared to brick modules (they were actually stuck in AT+SLEEP).
- **`AT+SLEEP=1`** — Persists in EEPROM across power cycles. Module boots into sleep and won't respond to first AT command. Must send throwaway `AT` to wake. Use mode 0 instead.
- **`AT+SLEEP=3`** — Fixes modules stuck in sleep mode 1. Restores normal operation.
- **NEVER send commands not in this list** — Unknown commands return "OK" but can corrupt firmware.
