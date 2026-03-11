# PB-03F AT Command List

Captured from production AT firmware via `AT+HELP` on 2026-03-10.

Module: Ai-Thinker PB-03F (PHY6252), BLE 5.2
Address: 2442e32ef1e5

## Commands from AT+HELP (28 total)

| Command | Description |
|---------|-------------|
| `AT` | Check whether UART is in AT mode |
| `ATE0` | Turn off echo |
| `ATE1` | Turn on echo |
| `AT+SYSIOMAP` | IO map configuration |
| `AT+SYSGPIOREAD` | GPIO read |
| `AT+SYSGPIOWRITE` | GPIO write |
| `AT+PWMCFG` | PWM set |
| `AT+PWMCFGS` | PWM set (variant) |
| `AT+PWMSTOP` | PWM stop |
| `AT+PWMDUTYSET` | PWM duty set |
| `AT+PWMDUTYSETS` | PWM duty set (variant) |
| `AT+HELP` | Function description |
| `AT+TRANSENTER` | Enter transparent mode |
| `AT+BLESTATE` | BLE state |
| `AT+BLESEND` | BLE send |
| `AT+BLEDISCON` | BLE disconnect |
| `AT+BLEMTU` | Set MTU |
| `AT+BLEADVEN` | Set advertising enable |
| `AT+BLECONINTV` | Set connection interval |
| `AT+BLESCAN` | BLE scan |
| `AT+BLECONNECT` | Connect to device |
| `AT+BLEAUTOCON` | Auto connect |
| `AT+BLEDISAUTOCON` | Disable auto connect |
| `AT+BLEAUTH` | Authentication |
| `AT+BLEIBCNDATA` | Set iBeacon data |
| `AT+BLEIBCNUUID` | Set iBeacon UUID |
| `AT+RESTORE` | Restore module to factory default |
| `AT+RST` | Reset module |

## Commands NOT in AT+HELP but known to work

These commands are not listed by `AT+HELP` but have been used successfully:

| Command | Notes |
|---------|-------|
| `AT+BLENAME` | Get/set BLE device name. Works with `?` query and `=` set. |
| `AT+BLEMAC` | Get/set BLE MAC address. Works with `?` query. |
| `AT+SLEEP` | Power mode control. `AT+SLEEP=1` enters light sleep (~1mA savings). |
| `AT+BLEADVDATA` | Set raw advertising data. |
| `AT+BLERFPWR` | Change RF power. |
| `AT+UARTCFG` | Change UART baud rate. |
| `AT+BLEADVINTV` | Change advertising interval. |
| `AT+BLESERUUID` | Set service UUID. |
| `AT+BLETXUUID` | Set transfer UUID. |
| `AT+BLERXUUID` | Set RX UUID. |

## Key Commands for Wake-from-Sleep

- **`AT+BLESTATE`** — Query BLE connection state. Could be polled or may have event output.
- **`AT+SYSGPIOREAD`** — Read GPIO pin state.
- **`AT+SYSGPIOWRITE`** — Write GPIO pin state.
- **`AT+SYSIOMAP`** — Configure IO pin mapping. May allow mapping connection status to a GPIO.

## Notes

- `AT+RESTORE` is listed but **DANGEROUS** — previously bricked modules. Avoid.
- Previous sessions noted 42 commands; this clean dump shows 28. The earlier count was likely from a corrupted/merged output due to queue overflow.
- Commands not in `AT+HELP` may still work but are undocumented in this firmware version.
