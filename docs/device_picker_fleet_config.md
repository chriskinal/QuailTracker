# Device Picker & Fleet Configuration

Design document for multi-device support in the QuailTracker companion app. Covers BLE device discovery, explicit device selection, and batch configuration of 10+ units on a bench.

## Problem

The app currently auto-connects to the first QuailTracker BLE device found during scanning (`BluetoothService.cs:166-170`):

```csharp
void OnDiscovered(object? s, DeviceEventArgs e)
{
    found = e.Device;
    _scanCts?.Cancel();  // connect to first device found
}
```

With 10+ units powered on simultaneously, there is no way to:
- See which devices are in range
- Pick a specific device by name or MAC address
- Configure multiple devices in sequence without restarting the app

Additionally, the firmware saves the station ID to flash when `$SET,STATION,<name>` is received (`app_freertos.c:1868-1871`) but never pushes it to the BLE module's advertised name. All units advertise the factory-default BLE name regardless of their configured station ID, making them indistinguishable during scanning.

## Design

### 1. Device Discovery List

Replace the current "scan and grab first" behavior with a scan that accumulates all discovered devices for a configurable duration.

**New interface methods** on `IBluetoothService`:

```csharp
/// <summary>
/// Discovered device info from BLE scan.
/// </summary>
public record DiscoveredDevice(
    Guid Id,            // Plugin.BLE device ID (stable per platform)
    string Name,        // Advertised name (e.g. "QT-NORTH-01")
    int Rssi,           // Signal strength in dBm
    DateTimeOffset LastSeen
);

/// <summary>
/// Fired when a device is discovered or its RSSI updates during scanning.
/// </summary>
event EventHandler<DiscoveredDevice>? DeviceDiscovered;

/// <summary>
/// Start scanning for QuailTracker devices. Fires DeviceDiscovered for each
/// device found. Call StopScan() when done, then ConnectToDeviceAsync() to
/// pick one.
/// </summary>
Task StartScanAsync();

/// <summary>
/// Connect to a specific previously-discovered device.
/// </summary>
Task ConnectToDeviceAsync(DiscoveredDevice device);
```

The existing `ScanAndConnectAsync()` remains for backward compatibility (headless/single-device scenarios) but internally calls `StartScanAsync()` + auto-selects the first result + `ConnectToDeviceAsync()`.

**Implementation notes:**
- Plugin.BLE's `DeviceDiscovered` event fires for each device. Accumulate into a `Dictionary<Guid, DiscoveredDevice>`, updating RSSI on re-discovery.
- Filter by `ServiceUuid` (0xFFE0) same as today — only QuailTracker transparent UART devices appear.
- Scan runs until `StopScan()` is called or 30s timeout (whichever first).
- RSSI values help identify nearby vs. distant units on a bench.

### 2. Device Picker View

New `DevicePickerView.axaml` displayed when the user taps "Scan" in the bottom status bar. Replaces the current behavior of immediately scanning + connecting.

```
┌──────────────────────────────────┐
│  QuailTracker                    │
│  Select a device                 │
├──────────────────────────────────┤
│                                  │
│  ┌────────────────────────────┐  │
│  │ ● QT-NORTH-01     -42 dBm │  │
│  └────────────────────────────┘  │
│  ┌────────────────────────────┐  │
│  │ ● QT-SOUTH-03     -58 dBm │  │
│  └────────────────────────────┘  │
│  ┌────────────────────────────┐  │
│  │ ● HMSoft          -61 dBm │  │
│  └────────────────────────────┘  │
│  ┌────────────────────────────┐  │
│  │ ● QT-EAST-07      -74 dBm │  │
│  └────────────────────────────┘  │
│                                  │
│  Scanning... (4 found)           │
│                                  │
├──────────────────────────────────┤
│  [Cancel]              [Rescan]  │
└──────────────────────────────────┘
```

**Behavior:**
- Tapping a row calls `StopScan()` then `ConnectToDeviceAsync()`.
- List sorted by RSSI (strongest first) — nearest device on top.
- Signal strength indicator: filled circle color (green > -50, yellow > -70, red > -70 dBm).
- Devices with factory-default names ("HMSoft", "PB-03F") shown with dimmed text to distinguish from named units.
- "Rescan" clears the list and starts a fresh scan.

**ViewModel:** `DevicePickerViewModel` with:
- `ObservableCollection<DiscoveredDevice> Devices`
- `IRelayCommand<DiscoveredDevice> SelectDeviceCommand`
- `IRelayCommand RescanCommand`
- `bool IsScanning`

**Navigation:** The picker is shown as a modal overlay or replaces the main content area while disconnected. After connection, the main tabbed view (Health/Ops/Schedule/Config) appears as it does today.

### 3. Firmware: BLE Name Sync

When the station ID is set via `$SET,STATION,<name>`, the firmware should also update the BLE module's advertised name so devices are distinguishable during scanning.

**Current code** (`app_freertos.c:1868-1871`):
```c
if (strcmp(key, "STATION") == 0) {
    if (strlen(val) > 15) { bleSendLine("$ERR,BADARG"); return; }
    strncpy(cfg.stationId, val, sizeof(cfg.stationId));
    strncpy(deviceStationId, cfg.stationId, sizeof(deviceStationId));
    // Bug: never updates BLE module name
}
```

**Change:** After saving to flash, send the AT command to rename the BLE module:

```c
if (strcmp(key, "STATION") == 0) {
    if (strlen(val) > 15) { bleSendLine("$ERR,BADARG"); return; }
    strncpy(cfg.stationId, val, sizeof(cfg.stationId));
    strncpy(deviceStationId, cfg.stationId, sizeof(deviceStationId));

    // Update BLE module advertised name
    char atCmd[32];
    char atResp[32];
    snprintf(atCmd, sizeof(atCmd), "AT+NAME%s", val);  // HM-19 format
    if (bleSendCmd(atCmd, atResp, sizeof(atResp), 1000) > 0) {
        strncpy(bleName, val, sizeof(bleName) - 1);
        bleName[sizeof(bleName) - 1] = '\0';
        printf("BLE: Name updated to %s\r\n", val);
    }
}
```

**Module-specific AT commands:**
- **HM-19:** `AT+NAME<name>` (no equals sign). Response: `OK+Set:<name>`. Takes effect immediately on next advertisement.
- **PB-03F:** TBD — verify AT command syntax when boards arrive. Likely `AT+NAME=<name>` or similar. Add a module-type flag if the command format differs.

**Boot-time sync:** `StartBleTask()` currently queries the name with `AT+NAME?` but never sets it. Add a check after the query: if `bleName` differs from `cfg.stationId`, send `AT+NAME<stationId>` to sync. This handles the case where a unit was configured before the name-sync firmware was flashed.

```c
// After AT+NAME? query (line 1195-1204):
if (bleReady && strlen(cfg.stationId) > 0 &&
    strcmp(bleName, cfg.stationId) != 0) {
    char atCmd[32];
    snprintf(atCmd, sizeof(atCmd), "AT+NAME%s", cfg.stationId);
    if (bleSendCmd(atCmd, resp, sizeof(resp), 1000) > 0) {
        strncpy(bleName, cfg.stationId, sizeof(bleName) - 1);
        printf("BLE: Name synced to %s\r\n", bleName);
    }
}
```

### 4. Fleet Configuration Workflow

For configuring 10+ units in sequence on a bench, the app should support a streamlined connect-configure-disconnect-next cycle.

**Approach: "Apply & Next" button on Config tab**

After saving config to a device, a new "Apply & Next" button:
1. Sends the current config (same as Save)
2. Disconnects from the current device
3. Returns to the device picker with the scan already running

This lets the operator keep the same settings loaded in the Config tab and apply them to the next device with minimal taps: pick device -> Apply & Next -> pick next device -> Apply & Next -> ...

**Per-device overrides:**
- Station ID must be unique per device. The Config tab's Station ID field is edited before each "Apply & Next".
- All other settings (gain, schedule, thresholds) typically stay the same across the fleet.

**Fleet config is NOT a batch operation.** BLE is point-to-point; you can only talk to one device at a time. The workflow just minimizes friction in the connect/configure/disconnect loop.

### 5. Connected Device Header

Replace the current static "No device" / device name text in the bottom status bar with a richer connected-device display in the header area.

**Current** (`MainView.axaml:91-94`):
```xml
<TextBlock Text="{Binding DeviceName}"
           VerticalAlignment="Center"
           FontSize="12"
           Foreground="#AAAAAA" />
```

**Proposed:** When connected, show station ID + BLE address (from `$STATUS` response fields `ble_name` and `ble_addr`) so the operator can confirm which physical unit they're talking to.

```
QuailTracker
Connected to QT-NORTH-01 (A4:C1:38:xx:xx:xx)
```

## Files to Modify

### App (C# / Avalonia)

| File | Change |
|------|--------|
| `app/Shared/Services/IBluetoothService.cs` | Add `DiscoveredDevice` record, `DeviceDiscovered` event, `StartScanAsync()`, `ConnectToDeviceAsync()` |
| `app/Shared/Services/BluetoothService.cs` | Implement new scan/connect methods; keep `ScanAndConnectAsync()` as convenience wrapper |
| `app/Shared/Services/MockBluetoothService.cs` | Stub new interface members for design-time preview |
| `app/Shared/ViewModels/DevicePickerViewModel.cs` | **New file.** Observable device list, select/rescan commands |
| `app/Shared/ViewModels/MainWindowViewModel.cs` | Add `DevicePickerViewModel`, wire navigation between picker and tab views |
| `app/Shared/ViewModels/ConfigViewModel.cs` | Add `ApplyAndNextCommand` that saves + disconnects + signals return to picker |
| `app/Shared/Views/DevicePickerView.axaml` | **New file.** Device list UI with RSSI indicators |
| `app/Shared/Views/MainView.axaml` | Swap between DevicePickerView (disconnected) and tab content (connected); update header |

### Firmware (C / STM32)

| File | Change |
|------|--------|
| `stm32/QuailTracker_U575/Core/Src/app_freertos.c` | `bleHandleSet()`: send `AT+NAME` on `$SET,STATION`. `StartBleTask()`: sync name at boot if mismatched |

## Risks & Open Questions

1. **PB-03F AT command format.** The HM-19 uses `AT+NAME<name>` (no delimiter). The PB-03F (PHY6252) may use a different syntax. Verify with actual hardware and add a module-detection path if needed.

2. **BLE name length.** HM-19 supports up to 12 characters for the name. PB-03F limit is TBD. Station IDs are capped at 15 chars (`app_freertos.c:1869`); may need to truncate for the BLE name if the module limit is shorter.

3. **Name change requires disconnect?** Some BLE modules require a reset or disconnect/reconnect cycle for the new name to take effect in advertisements. Test whether the name change is visible to other scanners while a connection is active.

4. **iOS background scanning.** Plugin.BLE on iOS may have restrictions on background BLE scanning. The device picker should work fine in the foreground, but document any limitations.

5. **Device identification before name sync.** Until firmware with name-sync is flashed, all devices will still advertise factory names. The picker should display the BLE MAC address alongside the name so devices can be distinguished by address in the interim.
