# Web UI Guide

The QuailTracker web UI is the primary way to configure and monitor a
deployed station. It runs on the ESP32-C3 and is reachable from any
phone or laptop with WiFi — no app, no cloud, no internet needed.

This guide assumes you've already finished the [bootstrap](bootstrap.md)
and the station is running.

## Connecting

1. Power the station (USB-C, or battery + solar).
2. Within ~10 s of boot, scan WiFi networks. The station advertises an
   open AP named `QT_XXXX` by default — `XXXX` is the last 4 hex digits
   of the ESP32's MAC, or the station ID you set later.
3. Connect (no password). Most phones pop up a captive portal that
   loads the UI directly. If yours doesn't, browse to
   **<http://192.168.9.1>**.

The UI is mobile-first — the bottom tab bar gives you five tabs:
**Health**, **Ops**, **Detect**, **Schedule**, **Config**. State updates
every ~500 ms while the page is open via a WebSocket, so you don't
need to refresh.

The connection indicator at the top-left of the header shows a green
dot ("Live") when state is flowing, red ("…") if the WebSocket has
dropped. Pull-to-refresh or revisit the page to reconnect.

## Health tab — at-a-glance status

The Health tab is read-only. Land here first when you walk up to a
station to confirm it's healthy.

**Top card — device summary**

- **Station ID** — the configurable name (default `QT_XXXX`).
- **Firmware** — STM32 and ESP32 firmware versions side-by-side.
- **Battery** — bar graph + percentage + raw voltage.
- **SD** — bar graph (used / total) + free space in MB or GB.
- **Temp / Humidity** — live SHT30 readings.

**GPS card**

Badge shows lock state, satellite count, and PPS sync status. Position
and UTC time appear on the second row when locked. Outdoors with a
clear sky, expect 4+ satellites within 60 s; cold-start indoors can
take much longer or never lock.

**"Since Last Visit" card**

Hidden until populated. Shows aggregated stats since the last time
someone connected to the web UI: recording count, detection count,
battery min/max, and any system errors.

**Solar card**

Charge controller state — Standby / Charging / Complete / Fault.

## Ops tab — live operations

Things you actually do during a field visit.

**Recording card**

`Start Recording` / `Stop Recording` button manually toggles audio
capture, regardless of the schedule. Useful for one-off captures or
debugging mic placement. While recording, the card shows file size,
DMA buffer occupancy, and any overrun / clip counts (both should
stay at 0 for a healthy mic).

**Live Listen card**

Streams the live mic audio to your phone over WebSocket. Pick **Left**
or **Right** in the channel selector and tap **Listen**. The level bar
shows real-time amplitude. Useful for confirming the mics work and
sanity-checking gain. Turn it off when you're done — streaming uses
battery and burns CPU on both ends.

**SD Card card**

- **Mount** / **Eject** for hot-swap support.
- **Format** wipes the card (FAT32). Confirms before doing it.

The card status badge shows the current state. If you've just inserted
a card, mount it before starting a recording.

**Location Survey card**

This is how you tell the station where it is for TDOA cross-referencing
across multiple stations. The "Current" row shows live GPS; the "Survey"
row shows what's stored in the station's persistent config.

- **Start Survey** captures the current GPS fix as the station's
  reference position. Keep it running for at least 30 s for a stable
  average.
- **Clear** discards the stored survey position.

You typically do this once per deployment site, after the GPS has had
time to lock cleanly with a good antenna view.

## Detect tab — ML inference

The on-device species detector. Configure how aggressively it runs and
what it considers a "hit".

**Model Status card**

Shows the loaded model's name, size, and last activity timestamp. The
**Reload** button forces a re-read of the model file from SD — use
this after dropping a new `.tflite` model into `/model/` on the SD
card.

**Detection Control card**

- **Mission Mode**:
  - *Record Only* — capture audio, skip inference. Saves CPU/battery
    when you'll process recordings off-line in the analyzer instead.
  - *Detect Only* — run inference continuously, but don't write audio
    files. Useful for live presence/absence monitoring with minimal
    storage cost.
  - *Record + Detect* — do both. Default for most deployments.
- **Confidence Threshold** (10–95%) — minimum sigmoid output to count
  a window as a hit. 50% is a reasonable starting point; raise it if
  you're getting too many false positives, lower it if you suspect
  the model is missing real calls.
- **Window Step** (1, 2, or 3 s) — how often inference runs across
  the audio stream. Smaller step = more compute, more chances to
  catch a brief vocalization. 2 s is a reasonable default for
  Northern Bobwhite.

Tap **Save** after changing any of these — they persist to flash.

**Detection Stats card**

Live counters: windows processed, hits, last detected species, last
hit timestamp, last confidence. Updates as the model runs.

## Schedule tab — when to record

The station can run on solar indefinitely if you don't record 24/7.
The schedule tab lets you carve out the windows you actually care
about.

**Sunrise / Sunset cards**

Each has an on/off toggle and a "before / after (min)" pair. When
enabled, the station opens a recording window starting *N* minutes
before the relevant solar event and lasting *M* minutes past it.
Sunrise/sunset times are computed on-device from GPS position + UTC
time, so they automatically track the season.

Defaults are reasonable for many vocalizing-bird deployments
(30 min before sunrise, 60 min after; 30 min before sunset, 30 min
after).

**Recording Windows card**

Custom fixed-time windows. Use the preset buttons for common
patterns:

- **Dawn/Dusk** — both sunrise and sunset windows enabled, no
  custom windows.
- **Dawn Only** — sunrise window only.
- **Nocturnal** — fixed nighttime window.
- **All Day** — continuous recording (battery-aggressive — only
  feasible with USB or large solar).

Or build a custom schedule with **+ Add Window**: each row gets a
start time and an end time (24-hour). Up to 8 windows.

Tap **Save Schedule** at the bottom when done. **Load** re-reads from
flash if you want to discard unsaved edits.

## Config tab — everything else

Persistent station configuration. Most of these get set once at
deployment and rarely changed.

**Device & Audio card**

- **Station** — the human-readable name. Becomes the WiFi AP SSID and
  appears in detection logs and recording filenames. 15 chars max.
- **Mic Heading** — compass bearing the mic array's "front arrow"
  faces (0–359°, 0 = north). Used by the analyzer for TDOA bearing
  estimation. Set this when you mount the station.
- **Gain** — MDF1 software gain, 0–24 dB in 3 dB steps. **+18 dB is
  the bench-validated sweet spot** for Northern Bobwhite detection;
  lower for noisy environments, higher only if the model is missing
  faint calls.
- **Format** — FLAC (compressed lossless, ~50% size of WAV) or WAV
  (uncompressed, simpler tooling). Default FLAC.
- **Rate** — fixed at 48 kHz. (Reserved for future per-deployment
  rate switching; not adjustable today.)
- **HPF** (high-pass filter) — drops everything below this frequency,
  kills wind rumble + low-frequency noise floor. Default 150 Hz.
- **LPF** (low-pass filter) — drops everything above this frequency.
  Default 8000 Hz, which keeps the full Northern Bobwhite call range
  with margin and trims hiss.
- **Chunk** — split long recordings into N-minute files (set to 0 to
  disable splitting). Smaller chunks = more files but easier to
  process and recover from corruption.

**Amplitude Trigger card**

Optional: only write to SD when audio exceeds a threshold. Useful for
quiet sites where most of the schedule is silence.

- Toggle on/off.
- **Thr** — threshold in dBFS (-60 to 0). -40 dB is a typical starting
  point.
- **Pre** — seconds of pre-roll captured before the trigger (the
  buffer is always running; this just reaches back).
- **Post** — seconds of post-roll after audio drops below threshold.

When trigger is off, the schedule windows record continuously.

**Low Battery card**

- Slider sets the cutoff percentage (0–50%, default 10%).
- **Auto-stop** checkbox: when battery drops below the cutoff,
  stop the current recording and skip the rest of the schedule
  window until charge recovers. Prevents corrupted FLAC files from
  brownout shutdowns.

**ESP32 Power Mode card**

Two mutually-exclusive WiFi power-saving modes (both off by default —
WiFi stays on continuously):

- **WiFi Duty Cycle (30 s on / 60 s sleep)** — ESP32 enters deep
  sleep on a fixed cycle when no client is connected. Drops standby
  draw from ~80 mA to ~5 µA at the cost of "you might have to wait
  90 s for the WiFi AP to be reachable when you walk up to the
  station".
- **Laser Wake (deep sleep until laser trigger)** — same idea, but
  the ESP32 stays asleep indefinitely and only wakes when a laser
  pointer hits the laser-wake phototransistor (P1 header on the
  board). Most aggressive battery savings; useful for stations
  deployed in remote locations where you'll only re-visit
  occasionally.

For most deployments leave both off. The ESP32's ~80 mA standby is
modest compared to the recording draw and is offset by even a small
solar panel.

**ESP32 Firmware Update card**

OTA update for the ESP32 itself. Drop in `firmware.bin` from
`esp32_bridge/.pio/build/esp32c3/` (the *app* binary, not the merged
`qt_esp_v*.bin` from the repo root). The ESP32 reboots into the new
firmware automatically.

**STM32 Firmware Update card**

Same flow for the STM32. Drop in `qt_stm_v<ver>.bin`. The ESP32
flashes the STM32 over SPI using the ROM bootloader; takes ~10–15 s.

## Common workflows

### First deployment at a site

1. Power up the station, wait for GPS lock (Health tab → GPS card).
2. Set **Station** name and **Mic Heading** in Config.
3. **Start Survey** in Ops, leave running 30+ seconds, stop. Confirms
   the station's reference position is recorded.
4. Configure Schedule for the species you're after.
5. Set Gain, HPF/LPF, Chunk in Config to taste.
6. Save everything. Walk away.

### Tuning detection sensitivity

1. Run **Mission Mode = Detect Only** for a session at the deployment
   site so you don't fill the SD card while testing.
2. Watch **Detection Stats** in Detect tab during known-call periods
   (dawn / dusk for Northern Bobwhite). Note the confidence values
   you see.
3. Move the **Confidence Threshold** slider. Lower if real calls are
   below threshold; raise if non-bird sounds are getting hits.
4. Save, then re-test. Repeat as needed.
5. Switch back to **Record + Detect** when satisfied.

### Quick field-service check

Walk-up sequence to verify a station that's been deployed for weeks:

1. Connect to the AP.
2. Health tab — check battery %, SD free space, GPS lock.
3. "Since Last Visit" card — review error counts and detection counts.
4. Ops → Live Listen briefly to confirm mics still work.
5. Config → check Station ID and Mic Heading still match what you
   wrote on your field log.

If anything looks wrong, OTA-update firmware right there from your
phone — no need to bring the station home.

## Next steps

The station is configured and recording. To process the audio it
captures and visualize detections across multiple stations, continue
to the [**analyzer guide**](analyzer.md).
