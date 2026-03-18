# QuailTracker BLE Protocol Migration: Text Commands to Nanopb Pub/Sub

## Motivation

The current BLE protocol uses newline-terminated text commands (`$STATUS`, `$SET,GAIN,12`, etc.) over the PB-03F's transparent UART service. This has several problems:

1. **No keepalive** — idle connections drop after 3-4 minutes because no data flows
2. **Poll-only** — the app must explicitly request every update; nothing is pushed automatically
3. **String parsing** — both sides build and parse formatted text, which is fragile and verbose
4. **No schema** — the protocol lives implicitly in C `strcmp` chains and C# string parsers; adding a field requires coordinated changes with no compile-time safety
5. **Bandwidth** — text encoding is 2-5x larger than equivalent binary for numeric data

## Target Architecture

Replace the text protocol with **Protocol Buffers (protobuf)** using:
- **Nanopb** on the STM32 (~3 KB ROM, zero dynamic allocation)
- **Google.Protobuf** NuGet on the Avalonia apps
- A single `.proto` file that generates both C and C# code

Layer a lightweight **pub/sub envelope** on top so the device can push updates automatically and the app subscribes to topics of interest.

The transport remains the PB-03F transparent UART — this plan changes only the message format and data flow pattern, not the hardware.

---

## Framing Layer

UART is a byte stream with no message boundaries. Protobuf messages are not self-delimiting. We need a framing layer.

### COBS (Consistent Overhead Byte Stuffing)

Use COBS encoding with `0x00` as the frame delimiter:

```
[COBS-encoded payload] [0x00]
```

**Why COBS over length-prefix:**
- **Self-synchronizing** — if a byte is lost or corrupted, the decoder resynchronizes at the next `0x00` delimiter. Length-prefix framing loses sync permanently on a single corrupt byte.
- **Low overhead** — adds at most 1 byte per 254 bytes of payload (~0.4%)
- **Simple** — encoder/decoder is ~30 lines of C
- **Battle-tested** — used in USB CDC, PPP, and many embedded protocols

### Frame Structure

```
+----------------------------------------------+------+
| COBS-encoded(Header + Protobuf payload)      | 0x00 |
+----------------------------------------------+------+
```

**Header (4 bytes, prepended before COBS encoding):**
```
Byte 0:    Message type (Topic enum, uint8)
Byte 1:    Flags (uint8: bit 0 = request/response, bit 1 = ack requested)
Byte 2-3:  Sequence number (uint16 little-endian, wrapping)
```

The header is not protobuf-encoded — it's a fixed 4-byte prefix for fast dispatch without deserializing the payload. The protobuf payload follows immediately after.

### Maximum Frame Size

PB-03F transparent UART MTU is negotiated (typically 185 bytes). After COBS encoding, a 180-byte payload becomes at most 181 bytes. Set maximum protobuf payload to **176 bytes** (180 - 4 byte header).

Exception: OTA data frames may need fragmentation (handled separately).

---

## Topic / Message Type Enum

```
enum Topic : uint8 {
    // Device → App (push)
    STATUS          = 0x01,   // Periodic device status
    DETECTION       = 0x02,   // Detection event
    AUDIO_LEVEL     = 0x03,   // Audio peak + activity ratio
    RECORDING_STATE = 0x04,   // Recording start/stop/progress
    GPS_FIX         = 0x05,   // GPS position + satellite update
    LOG             = 0x06,   // Debug log line (replaces BLE log ring)

    // App → Device (command)
    COMMAND         = 0x10,   // Generic command (start rec, mount SD, etc.)
    SET_CONFIG      = 0x11,   // Configuration change
    SUBSCRIBE       = 0x12,   // Subscribe to topic with interval
    UNSUBSCRIBE     = 0x13,   // Unsubscribe from topic

    // Device → App (command response)
    COMMAND_ACK     = 0x20,   // Success/error response to COMMAND
    CONFIG_DUMP     = 0x21,   // Full config (response to request or on connect)

    // Bidirectional
    PING            = 0x30,
    PONG            = 0x31,

    // OTA (keep as separate flow)
    OTA_BEGIN       = 0x40,
    OTA_DATA        = 0x41,
    OTA_END         = 0x42,
    OTA_COMMIT      = 0x43,
    OTA_ABORT       = 0x44,
    OTA_STATUS      = 0x45,
    OTA_ROLLBACK    = 0x46,
}
```

---

## Protobuf Message Definitions

### `quailtracker.proto`

```protobuf
syntax = "proto3";
package quailtracker;

// ===== Device → App: Push Messages =====

message Status {
    // Battery
    uint32 battery_mv       = 1;   // millivolts
    uint32 battery_pct      = 2;   // 0-100

    // BLE
    bool   ble_ready        = 10;
    string ble_name         = 11;  // max 15 chars

    // SD
    bool   sd_mounted       = 20;
    uint32 sd_total_kb      = 21;
    uint32 sd_free_kb       = 22;

    // Recording
    bool   recording        = 30;
    string rec_filename     = 31;
    uint32 rec_bytes        = 32;
    uint32 rec_duration_s   = 33;
    uint32 rec_overruns     = 34;
    uint32 rec_format       = 35;  // 0=FLAC, 1=WAV

    // Detection
    bool   det_active       = 40;
    uint32 det_windows      = 41;
    uint32 det_hits         = 42;
    string det_last_species = 43;
    bool   model_loaded     = 44;
    uint32 model_size       = 45;
    uint32 model_labels     = 46;

    // Environment
    int32  temperature_c100 = 50;  // hundredths of °C
    uint32 humidity_rh100   = 51;  // hundredths of %RH

    // Firmware
    string firmware_version = 60;
    string station_id       = 61;
}

message GpsFix {
    bool   valid            = 1;
    uint32 satellites       = 2;
    int32  latitude_e7      = 3;   // degrees × 10^7
    int32  longitude_e7     = 4;
    int32  altitude_mm      = 5;
    uint32 fix_type         = 6;   // 0=none, 1=2D, 2=3D
    bool   pps_synced       = 7;
    uint32 pps_count        = 8;
    string utc_time         = 9;   // YYYYMMDDHHMMSS
}

message AudioLevel {
    uint32 peak             = 1;   // 0-32767
    uint32 activity_pct     = 2;   // 0-100
    uint32 clip_count       = 3;
    uint32 buf_used         = 4;
    uint32 buf_capacity     = 5;
}

message Detection {
    string species          = 1;
    uint32 confidence       = 2;   // 0-100
    string utc_time         = 3;
    uint64 window_sample    = 4;
}

message RecordingState {
    bool   active           = 1;
    string filename         = 2;
    uint32 bytes_written    = 3;
    uint32 duration_s       = 4;
    uint32 overruns         = 5;
    uint32 sd_free_kb       = 6;
}

message LogLine {
    string text             = 1;   // single log line
}

// ===== App → Device: Commands =====

enum CommandType {
    CMD_NONE            = 0;
    CMD_REC_START       = 1;
    CMD_REC_STOP        = 2;
    CMD_REC_TOGGLE      = 3;
    CMD_SD_MOUNT        = 10;
    CMD_SD_EJECT        = 11;
    CMD_SD_FORMAT       = 12;
    CMD_GET_STATUS      = 20;
    CMD_GET_CONFIG      = 21;
    CMD_MODEL_RELOAD    = 30;
    CMD_MODEL_STATUS    = 31;
    CMD_DET_STATUS      = 32;
    CMD_SURVEY_START    = 40;
    CMD_SURVEY_STOP     = 41;
    CMD_SURVEY_CLEAR    = 42;
    CMD_LOGS_TOGGLE     = 50;
}

message Command {
    CommandType type     = 1;
}

message CommandAck {
    CommandType type     = 1;
    bool        success  = 2;
    string      error    = 3;   // empty on success, error code on failure
}

// ===== App → Device: Subscribe/Unsubscribe =====

message Subscribe {
    uint32 topic         = 1;   // Topic enum value
    uint32 interval_ms   = 2;   // 0 = on-change only, >0 = periodic push
}

message Unsubscribe {
    uint32 topic         = 1;
}

// ===== Configuration =====

message Config {
    // Audio
    uint32 gain             = 1;   // 0-24, 3dB steps
    uint32 bpf_low_hz       = 2;   // HPF cutoff
    uint32 bpf_high_hz      = 3;   // LPF cutoff
    uint32 sample_rate      = 4;   // always 48000 for now
    uint32 rec_format       = 5;   // 0=FLAC, 1=WAV

    // Station
    string station_id       = 10;  // max 15 chars

    // Amplitude trigger
    bool   trig_enabled     = 20;
    int32  trig_db           = 21;  // -60 to 0
    uint32 trig_pre_s       = 22;  // 0-30
    uint32 trig_post_s      = 23;  // 0-60

    // Battery
    uint32 low_bat_pct      = 30;  // 0-100
    bool   auto_stop        = 31;

    // Activity filter
    uint32 act_mode         = 40;  // 0-3
    uint32 act_min_pct      = 41;  // 1-50
    uint32 act_max_pct      = 42;  // 50-99
    uint32 act_hold_s       = 43;  // 1-30

    // Scheduling
    bool   sunrise_enabled  = 50;
    uint32 sunrise_before   = 51;  // minutes
    uint32 sunrise_after    = 52;
    bool   sunset_enabled   = 53;
    uint32 sunset_before    = 54;
    uint32 sunset_after     = 55;
    repeated TimeWindow windows = 56;  // max 8

    // Detection
    uint32 mission_mode     = 60;  // 0=record, 1=detect, 2=both
    uint32 det_threshold    = 61;  // 0-100
    uint32 det_step_s       = 62;  // 1-3

    // Survey-in
    int32  survey_lat_e7    = 70;
    int32  survey_lon_e7    = 71;
    int32  survey_alt_mm    = 72;
    uint32 survey_count     = 73;
}

message TimeWindow {
    uint32 start_hhmm       = 1;  // e.g., 600 = 06:00
    uint32 end_hhmm         = 2;  // e.g., 900 = 09:00
}

// SetConfig sends a partial Config — only populated fields are applied.
// Uses proto3 field presence (optional) to distinguish "set to 0" from "not set".
message SetConfig {
    optional uint32 gain             = 1;
    optional uint32 bpf_low_hz       = 2;
    optional uint32 bpf_high_hz      = 3;
    optional uint32 rec_format       = 5;
    optional string station_id       = 10;
    optional bool   trig_enabled     = 20;
    optional int32  trig_db          = 21;
    optional uint32 trig_pre_s       = 22;
    optional uint32 trig_post_s      = 23;
    optional uint32 low_bat_pct      = 30;
    optional bool   auto_stop        = 31;
    optional uint32 act_mode         = 40;
    optional uint32 act_min_pct      = 41;
    optional uint32 act_max_pct      = 42;
    optional uint32 act_hold_s       = 43;
    optional bool   sunrise_enabled  = 50;
    optional uint32 sunrise_before   = 51;
    optional uint32 sunrise_after    = 52;
    optional bool   sunset_enabled   = 53;
    optional uint32 sunset_before    = 54;
    optional uint32 sunset_after     = 55;
    repeated TimeWindow windows      = 56;
    optional uint32 mission_mode     = 60;
    optional uint32 det_threshold    = 61;
    optional uint32 det_step_s       = 62;
}

// ===== OTA =====

message OtaBegin {
    uint32 size_bytes       = 1;
    uint32 crc32            = 2;
}

message OtaData {
    bytes  chunk            = 1;   // raw firmware bytes, up to 128 per frame
}

message OtaEnd {}

message OtaCommit {}

message OtaAbort {}

message OtaRollback {}

message OtaStatus {
    uint32 state            = 1;   // 0=idle, 1=receiving, 2=complete
    uint32 bytes_received   = 2;
    uint32 pages_written    = 3;
    uint32 pages_total      = 4;
}
```

---

## Pub/Sub Behavior

### On BLE Connect

1. Device sends **Status** immediately (full snapshot)
2. Device sends **GpsFix** immediately
3. Device sends **Config** dump (full configuration)
4. App sends **Subscribe** for topics it cares about

### Default Subscriptions (app sets these on connect)

| Topic | Interval | Rationale |
|-------|----------|-----------|
| STATUS | 30000 ms | Keepalive + battery/SD/detection updates |
| GPS_FIX | 10000 ms | Position/satellite updates while GPS active |
| AUDIO_LEVEL | 0 (on-change) | Only when Operations tab is visible |
| RECORDING_STATE | 0 (on-change) | Push on start/stop/progress |
| DETECTION | 0 (on-change) | Push on every detection event |

### Firmware Push Logic

For each subscribed topic, the firmware maintains:
```c
typedef struct {
    uint8_t  topic;
    uint32_t interval_ms;   // 0 = on-change
    uint32_t last_push_ms;  // HAL_GetTick() of last push
    bool     subscribed;
} subscription_t;

#define MAX_SUBSCRIPTIONS 8
subscription_t subs[MAX_SUBSCRIPTIONS];
```

In the BLE task loop (runs every 10 ms):
```
for each subscription:
    if interval > 0 and (now - last_push) >= interval:
        encode and send topic message
        last_push = now
```

On-change topics (interval = 0) push immediately when the event occurs:
- Detection event → push Detection message
- Recording starts/stops → push RecordingState
- SD card inserted/removed → push Status

### Unsubscribe

App sends **Unsubscribe** when leaving a tab or disconnecting. Device stops pushing that topic.

### Connection Keepalive

The STATUS subscription at 30s interval serves as the keepalive. If the device sends a Status message every 30 seconds, the BLE link stays active. No separate ping/pong needed during normal operation.

PING/PONG is available as a lightweight fallback if the app wants a sub-second connection check.

---

## Implementation Phases

### Phase 1: Foundation + Build Integration

**Goal:** Add nanopb/protobuf infrastructure, COBS framing, build-time code generation for both sides.

**Firmware:**
- [ ] Add nanopb as a PlatformIO library or vendor it into `Middlewares/Third_Party/nanopb/`
- [ ] Create `proto/quailtracker.proto`
- [ ] Add `proto/quailtracker.options` (nanopb field size limits: max string lengths, max repeated counts)
- [ ] Add protoc generation step to `extra_build.py`
- [ ] Implement COBS encoder/decoder (`cobs.c` / `cobs.h`, ~60 lines)
- [ ] Implement frame send/receive functions over USART2 (BLE UART)
- [ ] Unit test: encode a Status message → COBS frame → decode → verify fields match

**App:**
- [ ] Add `Google.Protobuf` NuGet to `QuailTracker.Shared.csproj`
- [ ] Add `Grpc.Tools` NuGet for `.proto` → C# code generation (build-time)
- [ ] Add `proto/quailtracker.proto` to the project with `<Protobuf>` build item
- [ ] Implement COBS encoder/decoder in C# (`CobsCodec.cs`)
- [ ] Implement frame send/receive over BLE transparent UART characteristic
- [ ] Unit test: round-trip encode/decode

**Estimated effort:** 2-3 sessions

---

### Phase 2: Replace BLE Protocol (Full Cut)

**Goal:** Remove the text protocol entirely. Replace with binary protobuf pub/sub on both sides.

**Firmware:**
- [ ] Remove all `bleHandleCommand()` text parsing (`strcmp` chains)
- [ ] Remove `bleSendLine()` formatted text output
- [ ] Remove `$STATUS`, `$SET`, `$CONFIG`, `$REC`, `$SD` handler functions
- [ ] Implement binary command dispatcher (switch on Topic byte in header)
- [ ] Implement Status, GpsFix, AudioLevel, RecordingState, Detection, Config message encoding
- [ ] Implement Command, SetConfig, Subscribe, Unsubscribe message decoding
- [ ] Implement subscription table and push loop in BLE task
- [ ] Keep USART3 debug console as text (human-readable, not BLE)

**App:**
- [ ] Rewrite `BluetoothService` to use protobuf framing
- [ ] Parse incoming COBS frames, dispatch by topic to view models
- [ ] Send Subscribe/Unsubscribe/Command/SetConfig as COBS frames
- [ ] Remove all string parsing (`ParseStatusResponse`, etc.)
- [ ] Map protobuf messages to existing `DeviceStatus` / `DeviceConfig` view models

**Estimated effort:** 4-5 sessions

---

### Phase 3: Pub/Sub Lifecycle + Connection Stability

**Goal:** App uses subscription-based updates. No more manual polling. Connection stays alive.

**App:**
- [ ] On connect: subscribe to STATUS (30s), GPS_FIX (10s), RECORDING_STATE (on-change), DETECTION (on-change)
- [ ] Health tab visible → already covered by STATUS subscription
- [ ] Operations tab visible → subscribe AUDIO_LEVEL (200ms); on leave → unsubscribe
- [ ] Detection tab visible → DETECTION already subscribed on-change
- [ ] Remove manual Refresh buttons (data arrives automatically)
- [ ] Add connection watchdog: if no message received for 60s, show "Connection Lost"
- [ ] Handle reconnection: re-send all subscriptions

**Firmware:**
- [ ] Push Status on 30s subscription timer (keepalive)
- [ ] Push GpsFix when satellite count, fix type, or position changes (and on timer)
- [ ] Push Detection immediately on each inference hit
- [ ] Push RecordingState on start/stop and every 5s during recording
- [ ] Push AudioLevel at subscribed interval (fast: 100-500ms for live meter)

**Estimated effort:** 2-3 sessions

---

### Phase 4: OTA Over Protobuf

**Goal:** Migrate OTA firmware update to binary protocol.

**Firmware:**
- [ ] Implement OtaBegin, OtaData, OtaEnd, OtaCommit, OtaAbort, OtaStatus, OtaRollback handlers
- [ ] OtaData carries raw bytes (not hex-encoded) — 2x throughput improvement
- [ ] OTA progress pushed as OtaStatus messages (no polling needed)
- [ ] Retain CRC32 verification and bank-swap logic

**App:**
- [ ] Implement OTA upload using protobuf OTA messages
- [ ] Progress bar driven by OtaStatus push messages
- [ ] Remove hex encoding (binary chunks directly in OtaData.chunk)

**Estimated effort:** 1-2 sessions

---

## Build System Integration

### Firmware (PlatformIO)

Add to `extra_build.py`:
```python
import subprocess

def generate_proto(source, env):
    subprocess.check_call([
        "protoc",
        "--nanopb_out=stm32/QuailTracker_U575/Core/Src/",
        "--proto_path=proto/",
        "proto/quailtracker.proto"
    ])

env.AddPreAction("buildprog", generate_proto)
```

Generated files:
- `quailtracker.pb.c` — nanopb encode/decode functions
- `quailtracker.pb.h` — message structs and field descriptors

### App (MSBuild)

Add to `QuailTracker.Shared.csproj`:
```xml
<ItemGroup>
    <Protobuf Include="../../proto/quailtracker.proto" GrpcServices="None" />
</ItemGroup>
```

Generated files:
- `Quailtracker.cs` — C# message classes with serialization

### Single Source of Truth

```
QuailTracker/
├── proto/
│   ├── quailtracker.proto          # Message definitions
│   └── quailtracker.options        # Nanopb field size constraints
```

Both firmware and app reference the same `.proto` file. Adding a field to a message automatically generates updated code on both sides at build time.

---

## Nanopb Options File

`proto/quailtracker.options`:
```
# String field max lengths (nanopb needs static allocation)
quailtracker.Status.firmware_version    max_size:16
quailtracker.Status.station_id          max_size:16
quailtracker.Status.ble_name            max_size:16
quailtracker.Status.rec_filename        max_size:40
quailtracker.Status.det_last_species    max_size:32

quailtracker.GpsFix.utc_time            max_size:16

quailtracker.Detection.species          max_size:32
quailtracker.Detection.utc_time         max_size:16

quailtracker.LogLine.text               max_size:128

quailtracker.CommandAck.error           max_size:32

quailtracker.Config.station_id          max_size:16
quailtracker.Config.windows             max_count:8

quailtracker.SetConfig.station_id       max_size:16
quailtracker.SetConfig.windows          max_count:8

quailtracker.OtaData.chunk              max_size:128
```

---

## Memory Budget (Firmware)

| Component | ROM | RAM |
|-----------|-----|-----|
| Nanopb core | ~3 KB | 0 |
| Generated encode/decode | ~4 KB | 0 |
| COBS codec | <1 KB | 0 |
| Encode buffer | 0 | 256 B (stack) |
| Decode buffer | 0 | 256 B (stack) |
| Subscription table | 0 | 80 B (8 × 10 B) |
| **Total** | **~8 KB** | **~600 B** |

Current text protocol uses ~12 KB ROM for string formatting and parsing. Net ROM change is approximately neutral. RAM usage decreases because we eliminate the 512-byte BLE log ring buffer.

---

## Wire Format Example

**Status message (binary):**
```
Header:  01 00 00 01          (topic=STATUS, flags=0, seq=1)
Payload: 08 C2 1E 10 42 ...   (protobuf: battery_mv=3906, battery_pct=66, ...)
COBS:    [encoded bytes] 00   (frame delimiter)
```

Total: ~40-60 bytes for a full status update.

**Same data as current text protocol:**
```
$STATUS,0\n
id=QT001\n
fw=0.8.48\n
bat_v=3.906\n
bat_pct=66\n
... (20+ lines)
$END\n
```

Total: ~300-500 bytes across 4 pages. Binary is 5-10x more compact.

---

## Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| COBS sync loss on noisy BLE link | COBS is self-synchronizing — next `0x00` restarts frame. Add frame CRC8 if needed. |
| Protobuf version mismatch (app/firmware version skew) | Protobuf is forward/backward compatible by design. Unknown fields are ignored. |
| Nanopb stack overflow on encode | All buffers are fixed-size (256 B). Largest message (Config with 8 windows) fits in ~150 B. |
| OTA reliability over binary | OTA already has CRC32 verification. Binary chunks are more reliable than hex encoding. |
| Development disruption | Clean cut during prototyping — no production users on old protocol. Both sides change together on feature branch. |
| PB-03F transparent UART limitations | Protocol is transport-agnostic. If we swap to a programmable BLE module later, same protocol works over proper GATT characteristics. |

---

## Future: Proper GATT (Post PB-03F)

If a future board revision uses a programmable BLE module (nRF52, ESP32-C3, etc.), each protobuf message type maps naturally to a GATT characteristic:

| Topic | GATT Characteristic | Properties |
|-------|-------------------|------------|
| STATUS | Custom UUID 0x0001 | Read + Notify |
| GPS_FIX | Custom UUID 0x0002 | Read + Notify |
| AUDIO_LEVEL | Custom UUID 0x0003 | Notify |
| DETECTION | Custom UUID 0x0004 | Notify |
| RECORDING_STATE | Custom UUID 0x0005 | Read + Notify |
| COMMAND | Custom UUID 0x0010 | Write |
| SET_CONFIG | Custom UUID 0x0011 | Write |
| CONFIG_DUMP | Custom UUID 0x0012 | Read + Indicate |

The protobuf payloads stay identical — only the transport changes. The app swaps the transport layer without touching message handling.

---

## Decision Log

| Decision | Rationale |
|----------|-----------|
| Nanopb over Embedded Proto | Nanopb is MIT-licensed, mature, wider community. Embedded Proto is commercial. |
| COBS over length-prefix | Self-synchronizing on byte loss. Critical for wireless links. |
| Fixed header over protobuf envelope | 4-byte header enables topic dispatch without deserializing payload. Fast path for high-frequency messages. |
| Subscription table on device | Device controls push rate, not app. Prevents app bugs from flooding the link. |
| Clean cut over dual protocol | No production users yet. Dual protocol adds complexity for no benefit during prototyping. |
| Keep USART3 console as text | Human-readable debug output is valuable during development. No reason to binary-encode it. |
| `optional` fields in SetConfig | Proto3 field presence lets app send partial updates. "Set gain to 12" without re-sending every other field. |
