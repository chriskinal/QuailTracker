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

*Mode 0 cannot be woken by GPIO/UART — only power cycle wakes it. `AT+SLEEP=0,2,7,0`
(documented GPIO wake on RX pin) does NOT work on this firmware. Mode 0 is only useful
if you never need to wake the module programmatically.

Wakeup source (modes 0/1/2 only): 0=timer, 2=GPIO
- Timer: param1 = interval in ms
- GPIO: param1 = pin number (counterclockwise from upper-left, starting at 1). **Pin 7 = RX.**
- GPIO: param2 = trigger (0=low, 1=high, 2=falling, 3=rising, 4=both)

**Mode 2 is the recommended sleep mode for QuailTracker:**
- 50µA, no BLE advertising, GPIO wake on RX pin works
- `AT+SLEEP=2,2,7,0` — deep sleep, wake on RX low level (UART start bit)
- After MCU wakes from Stop 2, send throwaway `AT\r\n` to wake module (start bit pulls RX low)
- First UART byte is consumed as wake trigger — follow with `ATE0\r\n` to suppress echo
- Total system sleep: ~55µA (BLE 50µA + MCU Stop 2 ~5µA)
- Tradeoff: phone cannot discover device while sleeping (no advertising)

**Mode 0 — light sleep with advertising (NOT recommended):**
- 275µA with BLE advertising active, phone can discover and connect
- Module sends `+EVENT:BLE_CONNECTED` over UART TX on connect
- Problem: no way to wake module after MCU RTC wake (GPIO wake broken, no RST GPIO)
- Would require RST pin connected to STM32 GPIO on production PCB for power-cycle wake

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
