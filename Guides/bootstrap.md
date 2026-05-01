# Bootstrap Guide

You finished the [board build](board_build.md) and confirmed the 3V3
rail is alive. The board has no firmware yet. This guide walks you
from a blank board to a fully-flashed station with a working web UI —
no J-Link required. The only physical connection you'll need is a
USB-C cable for the very first ESP32 flash; everything after that is
wireless via the ESP32's web UI.

## What you need

- The bare main board, smoke-tested per the build guide.
- A **USB-C cable** that does data (not just power) — for the initial
  ESP32 flash.
- A **computer** to host the flashing tool (any OS — the easiest path
  is browser-based).
- A **phone or laptop with WiFi** for the web UI step.
- The two QuailTracker firmware binaries:
  - `qt_esp_v<ver>.bin` — merged ESP32-C3 image (bootloader + partition
    table + app). Flash at offset 0x0.
  - `qt_stm_v<ver>.bin` — STM32U575 image. Sideloaded via the web UI.

## Get the firmware files

If you cloned the repo and have PlatformIO installed, build them:

```bash
# STM32 production firmware
pio run

# ESP32 companion firmware
cd esp32_bridge && pio run
```

Both builds drop their post-build artifact at the repo root:
`qt_stm_v<ver>.bin` and `qt_esp_v<ver>.bin`. The version strings are
pulled from the firmware source — `Core/Inc/main.h` for the STM32 and
`src/main.c` (`ESP_FW_VERSION`) for the ESP32.

If you don't want to build from source, grab the same two files from
the project's releases page (whenever that exists) or from someone who
has built them.

## Step 1 — Flash the ESP32 over USB-C

Plug the USB-C cable into the **ESP32-C3 Super Mini** module on the
main board. Pick whichever flashing tool you're comfortable with.
They all produce the same result.

### Option A — Espressif Web Flasher (recommended; no install)

1. Open <https://espressif.github.io/esptool-js/> in Chrome or Edge
   (Firefox doesn't support WebSerial).
2. Set chip to **ESP32-C3** and click **Connect**. Pick the serial
   port the ESP32-C3 enumerates as.
3. In the file row, set address to **0x0** and choose
   `qt_esp_v<ver>.bin`.
4. Click **Program**. The flash should take ~10 s for a ~1 MB image.

### Option B — esptool from the command line

```bash
esptool.py --chip esp32c3 --port /dev/cu.usbmodemXXXX \
    write_flash 0x0 qt_esp_v<ver>.bin
```

(On macOS the port is `/dev/cu.usbmodemXXXX`; on Linux it's
`/dev/ttyACM0` or similar; on Windows it's `COMx`.)

### Option C — PlatformIO

```bash
cd esp32_bridge
pio run --target upload
```

PlatformIO auto-detects the port and flashes bootloader + partitions +
app. Useful if you're already building from source.

After flashing, the board resets and the ESP32 starts running. Its
onboard LED activity will change.

## Step 2 — Connect to the station's WiFi

The ESP32 firmware brings up an open WiFi access point named
`QT_XXXX`, where `XXXX` is the last 4 hex digits of the ESP32's MAC
address. (You can rename this later via the web UI — it'll persist to
NVS and become the station ID.)

1. On your phone or laptop, scan for WiFi networks. You should see a
   `QT_XXXX` SSID within ~5 seconds of the ESP32 booting.
2. Connect to it. No password.
3. Open a browser to **<http://192.168.9.1>**. Most phones will pop
   up a captive portal automatically — accepting it loads the same
   page.

You should see the QuailTracker web UI. The "STM32" firmware version
field will show `--` because no firmware is on the STM32 yet.

## Step 3 — Flash the STM32 via the web UI

The ESP32 can flash the STM32 over its SPI link, using the STM32's
ROM bootloader. No J-Link, no USB-UART adapter, no special hardware.

1. In the web UI, scroll to the **STM32 Firmware Update** card.
2. Choose `qt_stm_v<ver>.bin` from your phone's downloads.
3. Tap **Upload & Flash**. The ESP32 holds BOOT0 high, pulses NRST,
   talks to the STM32's ROM bootloader over SPI2, and writes the new
   firmware to flash.
4. Wait for the progress bar — typically ~10–15 s.
5. The STM32 reboots into the new firmware. The "STM32" version field
   in the web UI flips from `--` to the version string.

If the upload errors out or the STM32 doesn't come up:

- Confirm the SWD/serial header (H2) is *not* connected to anything
  that could be holding NRST or BOOT0.
- Re-seat the cable and try again — the ROM bootloader is patient.
- As a worst case: hold BOOT0 (SW2), tap RESET (SW1), release BOOT0
  to manually drop the STM32 into the ROM bootloader, then retry the
  upload.

## Step 4 — Verify the station is alive

Once the STM32 is flashed, the web UI populates with live state from
the station: GPS lock, battery voltage, SD card status, audio gain,
recording schedule. The Health tab updates every ~500 ms via a
WebSocket push from the ESP32.

Things that should look healthy:

- **STM32 firmware** version matches what you uploaded.
- **ESP32 firmware** version matches.
- **Battery voltage** reads close to USB-supplied 3.3 V (no battery
  installed yet — the value is unreliable at this stage but the field
  populates).
- **SD card**: shows "Not mounted" if no card is inserted. Insert a
  FAT-formatted MicroSD and the field updates.
- **GPS satellites**: takes a few minutes to acquire on first boot,
  especially indoors. Outdoors with a clear sky, expect 4+ satellites
  within 60 s.

## (Optional) Mic verification with the test firmware

The build guide forward-references this step. If you assembled mic
breakouts and want to confirm they work before deploying, the
`tools/mic_test/` project produces a small standalone firmware that
turns the LED brightness into a peak-level meter and prints L/R bars
on the SEGGER RTT terminal.

You can flash it the same way you flashed the production STM32
firmware:

1. Build the test firmware:
   ```bash
   cd tools/mic_test && pio run
   ```
   The output is `tools/mic_test/.pio/build/mic_test/firmware.bin`.
2. In the web UI, use the **STM32 Firmware Update** form to upload
   that `firmware.bin`. (Rename it if you like — the ESP32 doesn't
   care what the file is called.)
3. Open SEGGER RTT (or run `pio device monitor` from `tools/mic_test/`
   if you have a J-Link) to see the L/R bars.
4. Tap each mic in turn. The matching L or R bar should jump.
5. When you're done, re-flash the production firmware
   (`qt_stm_v<ver>.bin`) via the same upload form to revert.

If a mic is silent or both bars track together, suspect a short or a
cold joint under the IM72D128 — see the rework note in the build
guide.

## Updating firmware later

After this initial bootstrap, you never need to plug USB-C in again
unless you brick the ESP32. Both halves update over WiFi:

- **STM32**: STM32 Firmware Update card (same flow as Step 3 above).
- **ESP32**: ESP32 Firmware Update card. Use a built `firmware.bin`
  from `esp32_bridge/.pio/build/esp32c3/firmware.bin` (note: this is
  the *app* binary, not the merged image — the ESP32's own OTA
  partition writer doesn't need the bootloader). The ESP32 reboots
  into the new firmware automatically.

If you do brick the ESP32 — bad firmware, corrupted flash, etc. —
just repeat Step 1 with USB-C and the merged `qt_esp_v<ver>.bin`. The
STM32's flash is untouched, so the production firmware stays put.

## Next steps

The station is alive and configurable. Continue to the
[**web UI guide**](web_ui.md) to learn how to set the recording
schedule, microphone gain, station ID, mission mode, and detection
thresholds.
