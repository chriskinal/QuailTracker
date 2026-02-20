/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Plugin.BLE;
using Plugin.BLE.Abstractions;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.EventArgs;
using QuailTracker.Shared.Models;

namespace QuailTracker.Shared.Services;

/// <summary>
/// Real Bluetooth Low Energy service using Plugin.BLE.
/// Communicates with QuailTracker firmware via transparent UART (HM-19/PB-03F)
/// using newline-terminated $CMD text protocol.
/// </summary>
public class BluetoothService : IBluetoothService
{
    // HM-19 / PB-03F transparent UART UUIDs
    private static readonly Guid ServiceUuid =
        Guid.Parse("0000FFE0-0000-1000-8000-00805F9B34FB");
    private static readonly Guid CharacteristicUuid =
        Guid.Parse("0000FFE1-0000-1000-8000-00805F9B34FB");

    private readonly IAdapter _adapter;
    private IDevice? _device;
    private ICharacteristic? _characteristic;
    private int _mtu = 20; // BLE default, updated after negotiation

    // RX line assembly
    private readonly StringBuilder _rxBuffer = new();

    // Command/response serialization
    private readonly SemaphoreSlim _cmdLock = new(1, 1);
    private TaskCompletionSource<string>? _responseTcs;

    // Multi-line response buffering ($STATUS / $CONFIG blocks)
    private List<string>? _multiLines;
    private string? _multiLineType;

    private CancellationTokenSource? _scanCts;

    public ConnectionState CurrentState { get; private set; } = ConnectionState.Disconnected;
    public string? ConnectedDeviceName { get; private set; }

    public event EventHandler<ConnectionState>? ConnectionStateChanged;
    public event EventHandler<DeviceStatus>? StatusReceived;
    public event EventHandler<DeviceConfig>? ConfigReceived;

    public BluetoothService()
    {
        _adapter = CrossBluetoothLE.Current.Adapter;
        _adapter.ScanTimeout = 15000;
        _adapter.DeviceDisconnected += OnDeviceDisconnected;
    }

    public async Task ScanAndConnectAsync()
    {
        if (CurrentState != ConnectionState.Disconnected)
            return;

        SetState(ConnectionState.Scanning);
        _scanCts = new CancellationTokenSource();

        IDevice? found = null;

        _adapter.DeviceDiscovered += OnDiscovered;
        try
        {
            await _adapter.StartScanningForDevicesAsync(
                serviceUuids: new[] { ServiceUuid },
                cancellationToken: _scanCts.Token);
        }
        catch (OperationCanceledException)
        {
            // Scan was stopped by user or we found a device
        }
        finally
        {
            _adapter.DeviceDiscovered -= OnDiscovered;
        }

        if (found == null)
        {
            SetState(ConnectionState.Disconnected);
            return;
        }

        // Connect
        SetState(ConnectionState.Connecting);
        try
        {
            var connectParams = new ConnectParameters(autoConnect: false, forceBleTransport: true);
            await _adapter.ConnectToDeviceAsync(found, connectParams);
            _device = found;

            // Request larger MTU for efficient writes
            _mtu = await _device.RequestMtuAsync(185);

            // Discover service and characteristic
            var service = await _device.GetServiceAsync(ServiceUuid);
            if (service == null)
                throw new InvalidOperationException("UART service not found");

            _characteristic = await service.GetCharacteristicAsync(CharacteristicUuid);
            if (_characteristic == null)
                throw new InvalidOperationException("UART characteristic not found");

            // Subscribe to notifications
            _characteristic.ValueUpdated += OnValueUpdated;
            await _characteristic.StartUpdatesAsync();

            ConnectedDeviceName = _device.Name ?? "QuailTracker";
            SetState(ConnectionState.Connected);

            // Verify connection with PING
            try
            {
                var pong = await SendRawAsync("$PING");
                // $PONG expected — if we got here, link is working
            }
            catch
            {
                // PING timeout is non-fatal; device may be busy
            }

            // Auto-request status
            _ = RequestStatusAsync();
        }
        catch
        {
            await CleanupConnection();
            SetState(ConnectionState.Disconnected);
        }

        return;

        void OnDiscovered(object? s, DeviceEventArgs e)
        {
            found = e.Device;
            // Stop scanning — we connect to the first device found
            _scanCts?.Cancel();
        }
    }

    public void StopScan()
    {
        _scanCts?.Cancel();
        if (CurrentState == ConnectionState.Scanning)
            SetState(ConnectionState.Disconnected);
    }

    public async Task DisconnectAsync()
    {
        if (_device == null) return;
        await CleanupConnection();
        SetState(ConnectionState.Disconnected);
    }

    public async Task RequestStatusAsync()
    {
        if (CurrentState != ConnectionState.Connected) return;

        try
        {
            await SendRawAsync("$STATUS");
            // Response handled in multi-line parser → fires StatusReceived
        }
        catch
        {
            // Timeout or disconnect — caller sees no StatusReceived event
        }
    }

    public async Task RequestConfigAsync()
    {
        if (CurrentState != ConnectionState.Connected) return;

        try
        {
            await SendRawAsync("$CONFIG");
            // Response handled in multi-line parser → fires ConfigReceived
        }
        catch
        {
            // Timeout or disconnect
        }
    }

    public async Task<bool> SendConfigAsync(DeviceConfig config)
    {
        if (CurrentState != ConnectionState.Connected) return false;

        var commands = new[]
        {
            $"$SET,STATION,{config.StationId}",
            $"$SET,GAIN,{(int)config.Gain}",
            $"$SET,HPF,{(int)config.HighPassFilter}",
            $"$SET,SCHED,{(int)config.ScheduleMode}",
            $"$SET,SUNOFF,{config.SunriseOffsetMinutes},{config.SunsetOffsetMinutes}",
            $"$SET,MANUAL,{config.ManualStartTime:HHmm},{config.ManualEndTime:HHmm}",
            $"$SET,TRIG,{(config.AmplitudeTriggerEnabled ? 1 : 0)}",
            $"$SET,TRIGDB,{config.AmplitudeThresholdDb}",
            $"$SET,TRIGPRE,{config.PreTriggerSeconds}",
            $"$SET,TRIGPOST,{config.PostTriggerSeconds}",
            $"$SET,LOWBAT,{config.LowBatteryThresholdPercent}",
            $"$SET,AUTOSTOP,{(config.AutoStopOnLowBattery ? 1 : 0)}",
        };

        foreach (var cmd in commands)
        {
            try
            {
                var response = await SendRawAsync(cmd);
                if (response.StartsWith("$ERR", StringComparison.Ordinal))
                    return false;
            }
            catch
            {
                return false;
            }
        }

        return true;
    }

    public async Task SendCommandAsync(string command)
    {
        if (CurrentState != ConnectionState.Connected) return;

        var wire = command.ToUpperInvariant() switch
        {
            "START" => "$REC,START",
            "STOP" => "$REC,STOP",
            "TOGGLE" => "$REC,TOGGLE",
            _ => command.StartsWith('$') ? command : $"${command}",
        };

        try
        {
            await SendRawAsync(wire);
        }
        catch
        {
            // Timeout — non-fatal
        }

        // Auto-refresh status after recording commands
        if (wire.StartsWith("$REC,", StringComparison.Ordinal))
        {
            await Task.Delay(100); // Brief settle time
            _ = RequestStatusAsync();
        }
    }

    /// <summary>
    /// Send a raw command string and await the single-line or multi-line response.
    /// Commands are serialized via _cmdLock so only one is in-flight at a time.
    /// </summary>
    private async Task<string> SendRawAsync(string cmd)
    {
        await _cmdLock.WaitAsync();
        try
        {
            if (_characteristic == null)
                throw new InvalidOperationException("Not connected");

            _responseTcs = new TaskCompletionSource<string>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            // Write UTF-8 bytes, chunked to fit MTU
            var bytes = Encoding.UTF8.GetBytes(cmd + "\n");
            var chunkSize = Math.Max(_mtu - 3, 20); // ATT overhead = 3 bytes

            for (var offset = 0; offset < bytes.Length; offset += chunkSize)
            {
                var len = Math.Min(chunkSize, bytes.Length - offset);
                var chunk = new byte[len];
                Array.Copy(bytes, offset, chunk, 0, len);
                await _characteristic.WriteAsync(chunk);
            }

            // Await response with 5s timeout
            using var timeoutCts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            timeoutCts.Token.Register(() =>
                _responseTcs?.TrySetCanceled());

            return await _responseTcs.Task;
        }
        finally
        {
            _responseTcs = null;
            _cmdLock.Release();
        }
    }

    /// <summary>
    /// Called by Plugin.BLE when the characteristic sends a notification.
    /// Accumulates bytes into lines and dispatches complete lines.
    /// </summary>
    private void OnValueUpdated(object? sender, CharacteristicUpdatedEventArgs e)
    {
        var text = Encoding.UTF8.GetString(e.Characteristic.Value);
        _rxBuffer.Append(text);

        // Process all complete lines
        var buf = _rxBuffer.ToString();
        int nlIndex;
        while ((nlIndex = buf.IndexOf('\n')) >= 0)
        {
            var line = buf.Substring(0, nlIndex).TrimEnd('\r');
            buf = buf.Substring(nlIndex + 1);
            if (line.Length > 0)
                ProcessLine(line);
        }
        _rxBuffer.Clear();
        _rxBuffer.Append(buf);
    }

    private void ProcessLine(string line)
    {
        // Skip HM-19 module noise
        if (line.StartsWith("OK+", StringComparison.Ordinal))
            return;

        // Multi-line mode: accumulating $STATUS or $CONFIG block
        if (_multiLines != null)
        {
            if (line == "$END")
            {
                var type = _multiLineType;
                var lines = _multiLines;
                _multiLines = null;
                _multiLineType = null;

                if (type == "$STATUS")
                {
                    var status = ParseStatus(lines);
                    StatusReceived?.Invoke(this, status);
                    _responseTcs?.TrySetResult("$STATUS");
                }
                else if (type == "$CONFIG")
                {
                    var config = ParseConfig(lines);
                    ConfigReceived?.Invoke(this, config);
                    _responseTcs?.TrySetResult("$CONFIG");
                }
            }
            else
            {
                _multiLines.Add(line);
            }
            return;
        }

        // Start of multi-line block
        if (line == "$STATUS" || line == "$CONFIG")
        {
            _multiLineType = line;
            _multiLines = new List<string>();
            return;
        }

        // Single-line response ($OK, $PONG, $ERR,... etc.)
        _responseTcs?.TrySetResult(line);
    }

    private void OnDeviceDisconnected(object? sender, DeviceEventArgs e)
    {
        if (_device != null && e.Device.Id == _device.Id)
        {
            // Unexpected disconnect
            _responseTcs?.TrySetCanceled();
            _device = null;
            _characteristic = null;
            ConnectedDeviceName = null;
            SetState(ConnectionState.Disconnected);
        }
    }

    private async Task CleanupConnection()
    {
        _responseTcs?.TrySetCanceled();

        if (_characteristic != null)
        {
            try
            {
                _characteristic.ValueUpdated -= OnValueUpdated;
                await _characteristic.StopUpdatesAsync();
            }
            catch { /* best-effort */ }
            _characteristic = null;
        }

        if (_device != null)
        {
            try { await _adapter.DisconnectDeviceAsync(_device); }
            catch { /* best-effort */ }
            _device = null;
        }

        ConnectedDeviceName = null;
        _multiLines = null;
        _multiLineType = null;
        _rxBuffer.Clear();
    }

    private void SetState(ConnectionState state)
    {
        CurrentState = state;
        ConnectionStateChanged?.Invoke(this, state);
    }

    // ── Parsing helpers ─────────────────────────────────────────────────

    private static Dictionary<string, string> LinesToDict(List<string> lines)
    {
        var dict = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var line in lines)
        {
            var eq = line.IndexOf('=');
            if (eq > 0)
                dict[line.Substring(0, eq)] = line.Substring(eq + 1);
        }
        return dict;
    }

    private static string Get(Dictionary<string, string> d, string key, string def = "") =>
        d.TryGetValue(key, out var v) ? v : def;

    private static int GetInt(Dictionary<string, string> d, string key, int def = 0) =>
        d.TryGetValue(key, out var v) && int.TryParse(v, out var i) ? i : def;

    private static long GetLong(Dictionary<string, string> d, string key, long def = 0) =>
        d.TryGetValue(key, out var v) && long.TryParse(v, out var l) ? l : def;

    private static float GetFloat(Dictionary<string, string> d, string key, float def = 0f) =>
        d.TryGetValue(key, out var v) && float.TryParse(v, CultureInfo.InvariantCulture, out var f) ? f : def;

    private static bool GetBool(Dictionary<string, string> d, string key) =>
        d.TryGetValue(key, out var v) && v == "1";

    private static DeviceStatus ParseStatus(List<string> lines)
    {
        var d = LinesToDict(lines);

        var sdTotal = GetLong(d, "sd_tot") * 1024; // firmware sends KB
        var sdFree = GetLong(d, "sd_free") * 1024;

        DateTime? gpsTime = null;
        var timeStr = Get(d, "gps_time");
        if (timeStr.Length >= 14 &&
            DateTime.TryParseExact(timeStr, "yyyyMMddHHmmss",
                CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal, out var t))
        {
            gpsTime = t;
        }

        return new DeviceStatus
        {
            StationId = Get(d, "id", "Unknown"),
            FirmwareVersion = Get(d, "fw", "0.0.0"),

            BatteryVoltage = GetFloat(d, "bat_v"),
            BatteryPercentage = GetInt(d, "bat_pct"),
            BatteryLevel = (BatteryLevel)GetInt(d, "bat_lvl"),

            GpsValid = GetBool(d, "gps_valid"),
            GpsSatellites = GetInt(d, "gps_sats"),
            Latitude = GetLong(d, "gps_lat") / 1e7,
            Longitude = GetLong(d, "gps_lon") / 1e7,
            Altitude = GetFloat(d, "gps_alt"),
            PpsValid = GetBool(d, "pps"),
            GpsTime = gpsTime,

            Temperature = GetFloat(d, "temp"),
            Humidity = GetFloat(d, "hum"),

            SdCardMounted = GetBool(d, "sd"),
            SdTotalBytes = sdTotal,
            SdFreeBytes = sdFree,
            SdUsedBytes = sdTotal - sdFree,

            IsRecording = GetBool(d, "rec"),
            CurrentFilename = Get(d, "rec_file") is { Length: > 0 } fn ? fn : null,
            CurrentFileSize = GetLong(d, "rec_bytes"),
            BufferOverflows = (uint)GetInt(d, "rec_ovf"),

            PeakLevel = GetInt(d, "aud_peak"),
            BufferUsed = GetInt(d, "aud_buf"),
            BufferCapacity = GetInt(d, "aud_cap"),

            LastUpdated = DateTime.Now,
        };
    }

    private static DeviceConfig ParseConfig(List<string> lines)
    {
        var d = LinesToDict(lines);

        return new DeviceConfig
        {
            StationId = Get(d, "id", "QT001"),
            Gain = (GainLevel)GetInt(d, "gain"),
            HighPassFilter = (HighPassFilter)GetInt(d, "hpf"),
            SampleRate = GetInt(d, "rate", 48000),
            ScheduleMode = (ScheduleMode)GetInt(d, "sched"),
            SunriseOffsetMinutes = GetInt(d, "sun_rise"),
            SunsetOffsetMinutes = GetInt(d, "sun_set"),
            ManualStartTime = ParseHhmm(Get(d, "man_start", "0600")),
            ManualEndTime = ParseHhmm(Get(d, "man_end", "0900")),
            AmplitudeTriggerEnabled = GetBool(d, "trig"),
            AmplitudeThresholdDb = GetInt(d, "trig_db", -40),
            PreTriggerSeconds = GetInt(d, "trig_pre", 2),
            PostTriggerSeconds = GetInt(d, "trig_post", 5),
            LowBatteryThresholdPercent = GetInt(d, "lowbat", 10),
            AutoStopOnLowBattery = GetBool(d, "autostop"),
        };
    }

    private static TimeOnly ParseHhmm(string hhmm)
    {
        if (hhmm.Length >= 4 &&
            int.TryParse(hhmm.Substring(0, 2), out var h) &&
            int.TryParse(hhmm.Substring(2, 2), out var m))
        {
            return new TimeOnly(h, m);
        }
        return new TimeOnly(6, 0);
    }
}
