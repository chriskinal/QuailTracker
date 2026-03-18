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
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;
using Google.Protobuf;
using Plugin.BLE;
using Plugin.BLE.Abstractions;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.EventArgs;
using QuailTracker.Shared.Models;

namespace QuailTracker.Shared.Services;

/// <summary>
/// Real Bluetooth Low Energy service using Plugin.BLE.
/// Communicates with QuailTracker firmware via transparent UART (HM-19/PB-03F)
/// using COBS-framed protobuf binary protocol.
/// </summary>
public class BluetoothService : IBluetoothService
{
    // Known service UUIDs to SKIP during transparent UART discovery.
    // 0x180A = Device Information (read-only), FF01 = PB-03F control (not passthrough).
    private static readonly string SkipServicePrefix180A = "0000180a-";
    private static readonly string SkipServicePrefix5833 = "5833ff01-";  // PB-03F FF01 control svc

    // HM-19 uses FFE0/FFE1 on standard Bluetooth Base UUID (single combined char).
    private static readonly Guid HmServiceUuid =
        Guid.Parse("0000FFE0-0000-1000-8000-00805F9B34FB");

    private readonly IAdapter _adapter;
    private IDevice? _device;
    private ICharacteristic? _writeCharacteristic;  // TX: app -> device
    private ICharacteristic? _notifyCharacteristic; // RX: device -> app
    private int _mtu = 20; // BLE default, updated after negotiation

    // COBS frame assembler for incoming BLE data
    private readonly BleFrameAssembler _frameAssembler = new();
    private readonly BleProtoFrame _frameBuilder = new();

    // Command/response serialization
    private readonly SemaphoreSlim _cmdLock = new(1, 1);
    private TaskCompletionSource<Quailtracker.CommandAck>? _ackTcs;
    private TaskCompletionSource<Quailtracker.Config>? _configTcs;
    private TaskCompletionSource<Quailtracker.Status>? _statusTcs;

    private CancellationTokenSource? _scanCts;

    public ConnectionState CurrentState { get; private set; } = ConnectionState.Disconnected;
    public string? ConnectedDeviceName { get; private set; }

    // Discovered devices during scan (for device picker)
    private readonly Dictionary<Guid, (DiscoveredDevice Info, IDevice Device)> _discoveredDevices = new();

    public event EventHandler<ConnectionState>? ConnectionStateChanged;
    public event EventHandler<DeviceStatus>? StatusReceived;
    public event EventHandler<DeviceConfig>? ConfigReceived;
    public event EventHandler<DetectionEvent>? DetectionReceived;
    public event EventHandler<DiscoveredDevice>? DeviceDiscovered;

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

            _mtu = await _device.RequestMtuAsync(185);
            await DiscoverUartCharacteristicsAsync(CancellationToken.None);

            ConnectedDeviceName = _device.Name ?? "QuailTracker";
            SetState(ConnectionState.Connected);

            await OnConnectedAsync();
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
            _scanCts?.Cancel();
        }
    }

    public async Task StartScanAsync()
    {
        if (CurrentState != ConnectionState.Disconnected)
            return;

        SetState(ConnectionState.Scanning);
        _scanCts = new CancellationTokenSource();
        _discoveredDevices.Clear();

        _adapter.DeviceDiscovered += OnScanDiscovered;
        // DeviceAdvertised fires for SUBSEQUENT advertisements of already-known
        // devices.  On iOS, the first DeviceDiscovered uses cached CBPeripheral
        // data (stale name, no local name in ad records).  DeviceAdvertised
        // carries fresh scan-response data including the current local name.
        _adapter.DeviceAdvertised += OnScanDiscovered;
        try
        {
            await _adapter.StartScanningForDevicesAsync(
                allowDuplicatesKey: true,
                cancellationToken: _scanCts.Token);
        }
        catch (OperationCanceledException)
        {
            // Scan stopped by user or timeout
        }
        finally
        {
            _adapter.DeviceDiscovered -= OnScanDiscovered;
            _adapter.DeviceAdvertised -= OnScanDiscovered;
        }

        if (CurrentState == ConnectionState.Scanning)
            SetState(ConnectionState.Disconnected);
    }

    private void OnScanDiscovered(object? sender, DeviceEventArgs e)
    {
        var dev = e.Device;

        // Prefer the advertisement local name over dev.Name -- iOS caches
        // CBPeripheral.Name across sessions, so it can return a stale name
        // (e.g. "AI-Thinker") even after the device renamed to "QT001".
        var name = dev.Name ?? "Unknown";
        var recs = dev.AdvertisementRecords;
        if (recs != null)
        {
            foreach (var rec in recs)
            {
                if (rec.Type == Plugin.BLE.Abstractions.AdvertisementRecordType.CompleteLocalName
                    || rec.Type == Plugin.BLE.Abstractions.AdvertisementRecordType.ShortLocalName)
                {
                    var advName = Encoding.UTF8.GetString(rec.Data).TrimEnd('\0');
                    if (advName.Length > 0)
                    {
                        name = advName;
                        break;
                    }
                }
            }
        }

        var rssi = dev.Rssi;
        var info = new DiscoveredDevice(dev.Id, name, rssi, DateTimeOffset.Now);
        _discoveredDevices[dev.Id] = (info, dev);
        DeviceDiscovered?.Invoke(this, info);
    }

    public async Task ConnectToDeviceAsync(DiscoveredDevice device)
    {
        // Explicitly stop the adapter's scan and await completion.
        // StopScan() only cancels the CTS; the adapter may still be scanning
        // at the CoreBluetooth level.  On iOS, connecting while a scan with
        // allowDuplicatesKey:true is active can silently fail.
        _scanCts?.Cancel();
        if (_adapter.IsScanning)
            await _adapter.StopScanningForDevicesAsync();

        // Let CoreBluetooth fully settle after stopping the scan.
        await Task.Delay(500);

        SetState(ConnectionState.Connecting);

        // Wrap the entire connect + service-discovery sequence in a 15s timeout.
        // Without this, any step (connect, MTU, service discovery) can hang forever.
        using var timeoutCts = new CancellationTokenSource(TimeSpan.FromSeconds(15));

        try
        {
            // Use ConnectToKnownDeviceAsync which connects by UUID, retrieving a
            // fresh CBPeripheral on iOS.  This avoids stale IDevice references that
            // can occur when allowDuplicatesKey scanning fires DeviceAdvertised
            // multiple times, replacing the dictionary entry.
            var connectParams = new ConnectParameters(autoConnect: false, forceBleTransport: true);
            _device = await _adapter.ConnectToKnownDeviceAsync(device.Id, connectParams, timeoutCts.Token);

            timeoutCts.Token.ThrowIfCancellationRequested();
            _mtu = await _device.RequestMtuAsync(185);

            await DiscoverUartCharacteristicsAsync(timeoutCts.Token);

            ConnectedDeviceName = device.Name is { Length: > 0 } ? device.Name : (_device.Name ?? "QuailTracker");
            SetState(ConnectionState.Connected);

            await OnConnectedAsync();
        }
        catch
        {
            await CleanupConnection();
            SetState(ConnectionState.Disconnected);
        }
    }

    /// <summary>
    /// Discover the UART write and notify characteristics on the connected device.
    /// Strategy: try HM-19 service (FFE0) first, then scan all services skipping
    /// known non-UART ones (180A Device Info, 5833FF01 PB-03F control).
    /// PB-03F transparent UART is on the PhyPlus 55535343 service with split chars.
    /// </summary>
    private async Task DiscoverUartCharacteristicsAsync(CancellationToken ct)
    {
        if (_device == null)
            throw new InvalidOperationException("Not connected");

        // Try HM-19 known service first (single combined characteristic)
        ct.ThrowIfCancellationRequested();
        var hmService = await _device.GetServiceAsync(HmServiceUuid);
        if (hmService != null)
        {
            var chars = await hmService.GetCharacteristicsAsync();
            foreach (var ch in chars)
            {
                if (ch.CanWrite && ch.CanUpdate)
                {
                    _writeCharacteristic = ch;
                    _notifyCharacteristic = ch;
                    break;
                }
            }
        }

        // Scan all services for transparent UART (skip known non-UART services)
        if (_writeCharacteristic == null || _notifyCharacteristic == null)
        {
            var services = await _device.GetServicesAsync();
            foreach (var svc in services)
            {
                ct.ThrowIfCancellationRequested();
                var svcId = svc.Id.ToString().ToLowerInvariant();

                // Skip Device Information and PB-03F control service
                if (svcId.StartsWith(SkipServicePrefix180A) ||
                    svcId.StartsWith(SkipServicePrefix5833))
                    continue;

                var chars = await svc.GetCharacteristicsAsync();
                ICharacteristic? w = null, n = null;
                foreach (var ch in chars)
                {
                    if (ch.CanWrite && ch.CanUpdate) { w = ch; n = ch; }
                    else if (ch.CanWrite) w = ch;
                    else if (ch.CanUpdate) n = ch;
                }

                if (w != null && n != null)
                {
                    _writeCharacteristic = w;
                    _notifyCharacteristic = n;
                    break;
                }
            }
        }

        if (_writeCharacteristic == null)
            throw new InvalidOperationException("No writable characteristic found");
        if (_notifyCharacteristic == null)
            throw new InvalidOperationException("No notifiable characteristic found");

        // Use WriteWithoutResponse if the characteristic supports it.
        if (_writeCharacteristic.Properties.HasFlag(CharacteristicPropertyType.WriteWithoutResponse))
            _writeCharacteristic.WriteType = CharacteristicWriteType.WithoutResponse;

        _notifyCharacteristic.ValueUpdated += OnValueUpdated;
        await _notifyCharacteristic.StartUpdatesAsync();
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

    // ── Post-connect setup ────────────────────────────────────────────────

    /// <summary>
    /// Called after BLE connection and characteristic discovery are complete.
    /// Sends a Ping and subscribes to periodic/on-change topics.
    /// </summary>
    private async Task OnConnectedAsync()
    {
        try { await SendFrameAsync(_frameBuilder.EncodeEmpty(BleProtoTopic.Ping)); } catch { }

        // Subscribe to topics the app needs
        try
        {
            await SendSubscribeAsync(BleProtoTopic.Status, 30000);
            await SendSubscribeAsync(BleProtoTopic.GpsFix, 10000);
            await SendSubscribeAsync(BleProtoTopic.RecordingState, 0);  // on-change
            await SendSubscribeAsync(BleProtoTopic.Detection, 0);      // on-change
        }
        catch { /* non-fatal */ }

        // Request initial status
        _ = RequestStatusAsync();
    }

    // ── Command methods (IBluetoothService) ───────────────────────────────

    public async Task RequestStatusAsync()
    {
        if (CurrentState != ConnectionState.Connected) return;

        try
        {
            _statusTcs = new TaskCompletionSource<Quailtracker.Status>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            await SendCommandFrameAsync(Quailtracker.CommandType.CmdGetStatus);

            using var timeoutCts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            timeoutCts.Token.Register(() => _statusTcs?.TrySetCanceled());

            var status = await _statusTcs.Task;
            var mapped = MapStatus(status);
            Dispatcher.UIThread.Post(() => StatusReceived?.Invoke(this, mapped));
        }
        catch
        {
            // Timeout or disconnect
        }
        finally
        {
            _statusTcs = null;
        }
    }

    public async Task RequestConfigAsync()
    {
        if (CurrentState != ConnectionState.Connected) return;

        try
        {
            _configTcs = new TaskCompletionSource<Quailtracker.Config>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            await SendCommandFrameAsync(Quailtracker.CommandType.CmdGetConfig);

            using var timeoutCts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            timeoutCts.Token.Register(() => _configTcs?.TrySetCanceled());

            var config = await _configTcs.Task;
            var mapped = MapConfig(config);
            Dispatcher.UIThread.Post(() => ConfigReceived?.Invoke(this, mapped));
        }
        catch
        {
            // Timeout or disconnect
        }
        finally
        {
            _configTcs = null;
        }
    }

    public async Task<bool> SendConfigAsync(DeviceConfig config)
    {
        if (CurrentState != ConnectionState.Connected) return false;

        try
        {
            var sc = new Quailtracker.SetConfig
            {
                StationId = config.StationId ?? "QT001",
                Gain = (uint)config.GainDb,
                BpfLowHz = (uint)config.BandPassLowHz,
                BpfHighHz = (uint)config.BandPassHighHz,
                RecFormat = (uint)(config.Format == RecordingFormat.WAV ? 1 : 0),
                TrigEnabled = config.AmplitudeTriggerEnabled,
                TrigDb = config.AmplitudeThresholdDb,
                TrigPreS = (uint)config.PreTriggerSeconds,
                TrigPostS = (uint)config.PostTriggerSeconds,
                LowBatPct = (uint)config.LowBatteryThresholdPercent,
                AutoStop = config.AutoStopOnLowBattery,
                ActMode = (uint)config.ActivityMode,
                ActMinPct = (uint)config.ActivityMinPercent,
                ActMaxPct = (uint)config.ActivityMaxPercent,
                ActHoldS = (uint)config.ActivityHoldSeconds,
            };

            var ack = await SendSetConfigAsync(sc);
            return ack?.Success ?? false;
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> SendScheduleAsync(DeviceConfig config)
    {
        if (CurrentState != ConnectionState.Connected) return false;

        try
        {
            var sc = new Quailtracker.SetConfig
            {
                SunriseEnabled = config.SunriseEnabled,
                SunriseBefore = (uint)config.SunriseBeforeMinutes,
                SunriseAfter = (uint)config.SunriseAfterMinutes,
                SunsetEnabled = config.SunsetEnabled,
                SunsetBefore = (uint)config.SunsetBeforeMinutes,
                SunsetAfter = (uint)config.SunsetAfterMinutes,
            };

            // Add freeform windows
            var wins = config.FreeformWindows ?? [];
            foreach (var w in wins)
            {
                sc.Windows.Add(new Quailtracker.TimeWindow
                {
                    StartHhmm = (uint)(w.Start.Hour * 100 + w.Start.Minute),
                    EndHhmm = (uint)(w.End.Hour * 100 + w.End.Minute),
                });
            }

            var ack = await SendSetConfigAsync(sc);
            return ack?.Success ?? false;
        }
        catch
        {
            return false;
        }
    }

    public async Task SendSdCommandAsync(string operation)
    {
        if (CurrentState != ConnectionState.Connected) return;

        var cmdType = operation.ToUpperInvariant() switch
        {
            "MOUNT" => Quailtracker.CommandType.CmdSdMount,
            "EJECT" => Quailtracker.CommandType.CmdSdEject,
            "FORMAT" => Quailtracker.CommandType.CmdSdFormat,
            _ => Quailtracker.CommandType.CmdSdMount,
        };

        try
        {
            await SendCommandFrameAsync(cmdType);
        }
        catch
        {
            // Timeout -- non-fatal
        }
    }

    public async Task SetStreamAudioAsync(int intervalMs)
    {
        if (CurrentState != ConnectionState.Connected) return;

        try
        {
            if (intervalMs > 0)
                await SendSubscribeAsync(BleProtoTopic.AudioLevel, (uint)intervalMs);
            else
                await SendUnsubscribeAsync(BleProtoTopic.AudioLevel);
        }
        catch
        {
            // Timeout -- non-fatal
        }
    }

    public async Task SetStreamRecAsync(int intervalMs)
    {
        if (CurrentState != ConnectionState.Connected) return;

        try
        {
            if (intervalMs > 0)
                await SendSubscribeAsync(BleProtoTopic.RecordingState, (uint)intervalMs);
            else
                await SendUnsubscribeAsync(BleProtoTopic.RecordingState);
        }
        catch
        {
            // Timeout -- non-fatal
        }
    }

    public async Task SendSurveyCommandAsync(string operation)
    {
        if (CurrentState != ConnectionState.Connected) return;

        var cmdType = operation.ToUpperInvariant() switch
        {
            "START" => Quailtracker.CommandType.CmdSurveyStart,
            "STOP" => Quailtracker.CommandType.CmdSurveyStop,
            "CLEAR" => Quailtracker.CommandType.CmdSurveyClear,
            _ => Quailtracker.CommandType.CmdSurveyStart,
        };

        try
        {
            await SendCommandFrameAsync(cmdType);
        }
        catch
        {
            // Timeout -- non-fatal
        }
    }

    public async Task SendModelCommandAsync(string operation)
    {
        if (CurrentState != ConnectionState.Connected) return;

        var cmdType = operation.ToUpperInvariant() switch
        {
            "RELOAD" => Quailtracker.CommandType.CmdModelReload,
            "STATUS" => Quailtracker.CommandType.CmdModelStatus,
            _ => Quailtracker.CommandType.CmdModelReload,
        };

        try
        {
            await SendCommandFrameAsync(cmdType);
        }
        catch
        {
            // Timeout -- non-fatal
        }
    }

    public async Task SendDetectionCommandAsync(string operation)
    {
        if (CurrentState != ConnectionState.Connected) return;

        try
        {
            // "STREAM,1000" -> subscribe to detection topic
            if (operation.StartsWith("STREAM", StringComparison.OrdinalIgnoreCase))
            {
                await SendSubscribeAsync(BleProtoTopic.Detection, 0);
            }
            else
            {
                await SendCommandFrameAsync(Quailtracker.CommandType.CmdDetStatus);
            }
        }
        catch
        {
            // Timeout -- non-fatal
        }
    }

    public async Task<bool> SendDetectionConfigAsync(DeviceConfig config)
    {
        if (CurrentState != ConnectionState.Connected) return false;

        try
        {
            var sc = new Quailtracker.SetConfig
            {
                MissionMode = (uint)config.Mission,
                DetThreshold = (uint)config.DetectionThresholdPercent,
                DetStepS = (uint)config.DetectionWindowStep,
            };

            var ack = await SendSetConfigAsync(sc);
            return ack?.Success ?? false;
        }
        catch
        {
            return false;
        }
    }

    public async Task SendCommandAsync(string command)
    {
        if (CurrentState != ConnectionState.Connected) return;

        var cmdType = command.ToUpperInvariant() switch
        {
            "START" => Quailtracker.CommandType.CmdRecStart,
            "STOP" => Quailtracker.CommandType.CmdRecStop,
            "TOGGLE" => Quailtracker.CommandType.CmdRecToggle,
            _ => Quailtracker.CommandType.CmdNone,
        };

        if (cmdType == Quailtracker.CommandType.CmdNone)
            return;

        try
        {
            await SendCommandFrameAsync(cmdType);
        }
        catch
        {
            // Timeout -- non-fatal
        }

        // Auto-refresh status after recording commands
        await Task.Delay(100); // Brief settle time
        _ = RequestStatusAsync();
    }

    // ── Frame sending helpers ─────────────────────────────────────────────

    /// <summary>
    /// Send a Command protobuf frame and await the CommandAck response.
    /// </summary>
    private async Task<Quailtracker.CommandAck?> SendCommandFrameAsync(Quailtracker.CommandType cmdType)
    {
        await _cmdLock.WaitAsync();
        try
        {
            _ackTcs = new TaskCompletionSource<Quailtracker.CommandAck>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            var cmd = new Quailtracker.Command { Type = cmdType };
            var frame = _frameBuilder.Encode(BleProtoTopic.Command, cmd);
            await SendFrameAsync(frame);

            using var timeoutCts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            timeoutCts.Token.Register(() => _ackTcs?.TrySetCanceled());

            return await _ackTcs.Task;
        }
        finally
        {
            _ackTcs = null;
            _cmdLock.Release();
        }
    }

    /// <summary>
    /// Send a SetConfig protobuf frame and await the CommandAck response.
    /// </summary>
    private async Task<Quailtracker.CommandAck?> SendSetConfigAsync(Quailtracker.SetConfig setConfig)
    {
        await _cmdLock.WaitAsync();
        try
        {
            _ackTcs = new TaskCompletionSource<Quailtracker.CommandAck>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            var frame = _frameBuilder.Encode(BleProtoTopic.SetConfig, setConfig);
            await SendFrameAsync(frame);

            using var timeoutCts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            timeoutCts.Token.Register(() => _ackTcs?.TrySetCanceled());

            return await _ackTcs.Task;
        }
        finally
        {
            _ackTcs = null;
            _cmdLock.Release();
        }
    }

    /// <summary>
    /// Send a Subscribe frame (fire-and-forget, no ack expected).
    /// </summary>
    private async Task SendSubscribeAsync(BleProtoTopic topic, uint intervalMs)
    {
        var sub = new Quailtracker.Subscribe
        {
            Topic = (uint)topic,
            IntervalMs = intervalMs,
        };
        var frame = _frameBuilder.Encode(BleProtoTopic.Subscribe, sub);
        await SendFrameAsync(frame);
    }

    /// <summary>
    /// Send an Unsubscribe frame (fire-and-forget, no ack expected).
    /// </summary>
    private async Task SendUnsubscribeAsync(BleProtoTopic topic)
    {
        var unsub = new Quailtracker.Unsubscribe
        {
            Topic = (uint)topic,
        };
        var frame = _frameBuilder.Encode(BleProtoTopic.Unsubscribe, unsub);
        await SendFrameAsync(frame);
    }

    /// <summary>
    /// Write a COBS-framed byte array to the BLE characteristic, chunked to fit MTU.
    /// </summary>
    private async Task SendFrameAsync(byte[] frame)
    {
        if (_writeCharacteristic == null)
            throw new InvalidOperationException("Not connected");

        var chunkSize = Math.Max(_mtu - 3, 20); // ATT overhead = 3 bytes

        for (var offset = 0; offset < frame.Length; offset += chunkSize)
        {
            var len = Math.Min(chunkSize, frame.Length - offset);
            var chunk = new byte[len];
            Array.Copy(frame, offset, chunk, 0, len);
            await _writeCharacteristic.WriteAsync(chunk);
        }
    }

    // ── Frame receiving ───────────────────────────────────────────────────

    /// <summary>
    /// Called by Plugin.BLE when the characteristic sends a notification.
    /// Feeds raw bytes into the COBS frame assembler.
    /// </summary>
    private void OnValueUpdated(object? sender, CharacteristicUpdatedEventArgs e)
    {
        var data = e.Characteristic.Value;
        if (data == null || data.Length == 0) return;

        _frameAssembler.Feed(data, OnFrameReceived);
    }

    /// <summary>
    /// Called by BleFrameAssembler when a complete COBS frame (without delimiter) is ready.
    /// Decodes the frame header, parses the protobuf payload, and dispatches to handlers.
    /// </summary>
    private void OnFrameReceived(ReadOnlySpan<byte> cobsFrame)
    {
        if (!BleProtoFrame.TryDecode(cobsFrame, out var topic, out var sequence, out var payload))
            return;

        switch (topic)
        {
            case BleProtoTopic.Status:
            {
                var status = Quailtracker.Status.Parser.ParseFrom(payload);
                // If a RequestStatusAsync is waiting, deliver via TCS
                if (_statusTcs != null)
                {
                    _statusTcs.TrySetResult(status);
                }
                else
                {
                    // Unsolicited push (subscription)
                    var mapped = MapStatus(status);
                    Dispatcher.UIThread.Post(() => StatusReceived?.Invoke(this, mapped));
                }
                break;
            }

            case BleProtoTopic.GpsFix:
            {
                var gps = Quailtracker.GpsFix.Parser.ParseFrom(payload);
                var mapped = MapGpsToPartialStatus(gps);
                Dispatcher.UIThread.Post(() => StatusReceived?.Invoke(this, mapped));
                break;
            }

            case BleProtoTopic.AudioLevel:
            {
                var audio = Quailtracker.AudioLevel.Parser.ParseFrom(payload);
                var mapped = MapAudioToPartialStatus(audio);
                Dispatcher.UIThread.Post(() => StatusReceived?.Invoke(this, mapped));
                break;
            }

            case BleProtoTopic.RecordingState:
            {
                var rec = Quailtracker.RecordingState.Parser.ParseFrom(payload);
                var mapped = MapRecordingToPartialStatus(rec);
                Dispatcher.UIThread.Post(() => StatusReceived?.Invoke(this, mapped));
                break;
            }

            case BleProtoTopic.Detection:
            {
                var det = Quailtracker.Detection.Parser.ParseFrom(payload);
                var mapped = MapDetection(det);
                Dispatcher.UIThread.Post(() => DetectionReceived?.Invoke(this, mapped));
                break;
            }

            case BleProtoTopic.ConfigDump:
            {
                var config = Quailtracker.Config.Parser.ParseFrom(payload);
                if (_configTcs != null)
                {
                    _configTcs.TrySetResult(config);
                }
                else
                {
                    var mapped = MapConfig(config);
                    Dispatcher.UIThread.Post(() => ConfigReceived?.Invoke(this, mapped));
                }
                break;
            }

            case BleProtoTopic.CommandAck:
            {
                var ack = Quailtracker.CommandAck.Parser.ParseFrom(payload);
                _ackTcs?.TrySetResult(ack);
                break;
            }

            case BleProtoTopic.Pong:
                // Pong received -- connection confirmed, nothing to do
                break;

            case BleProtoTopic.Log:
                // Log lines -- could be forwarded to a debug console in the future
                break;
        }
    }

    // ── Connection lifecycle ──────────────────────────────────────────────

    private void OnDeviceDisconnected(object? sender, DeviceEventArgs e)
    {
        if (_device != null && e.Device.Id == _device.Id)
        {
            // Unexpected disconnect
            _ackTcs?.TrySetCanceled();
            _statusTcs?.TrySetCanceled();
            _configTcs?.TrySetCanceled();
            _frameAssembler.Reset();
            _device = null;
            _writeCharacteristic = null;
            _notifyCharacteristic = null;
            ConnectedDeviceName = null;
            SetState(ConnectionState.Disconnected);
        }
    }

    private async Task CleanupConnection()
    {
        _ackTcs?.TrySetCanceled();
        _statusTcs?.TrySetCanceled();
        _configTcs?.TrySetCanceled();
        _frameAssembler.Reset();

        if (_notifyCharacteristic != null)
        {
            try
            {
                _notifyCharacteristic.ValueUpdated -= OnValueUpdated;
                await _notifyCharacteristic.StopUpdatesAsync();
            }
            catch { /* best-effort */ }
            _notifyCharacteristic = null;
        }
        _writeCharacteristic = null;

        if (_device != null)
        {
            try { await _adapter.DisconnectDeviceAsync(_device); }
            catch { /* best-effort */ }
            _device = null;
        }

        ConnectedDeviceName = null;
    }

    private void SetState(ConnectionState state)
    {
        CurrentState = state;
        ConnectionStateChanged?.Invoke(this, state);
    }

    // ── Protobuf -> App model mapping ─────────────────────────────────────

    private static DeviceStatus MapStatus(Quailtracker.Status s)
    {
        var sdTotal = (long)s.SdTotalKb * 1024;
        var sdFree = (long)s.SdFreeKb * 1024;

        return new DeviceStatus
        {
            StationId = s.StationId is { Length: > 0 } ? s.StationId : "Unknown",
            FirmwareVersion = s.FirmwareVersion is { Length: > 0 } ? s.FirmwareVersion : "0.0.0",

            BatteryVoltage = s.BatteryMv / 1000f,
            BatteryPercentage = (int)s.BatteryPct,

            BleModuleReady = s.BleReady,
            BleModuleName = s.BleName ?? "",

            Temperature = s.TemperatureC100 / 100f,
            Humidity = s.HumidityRh100 / 100f,

            SdCardMounted = s.SdMounted,
            SdTotalBytes = sdTotal,
            SdFreeBytes = sdFree,
            SdUsedBytes = sdTotal - sdFree,

            IsRecording = s.Recording,
            CurrentFilename = s.RecFilename is { Length: > 0 } ? s.RecFilename : null,
            CurrentFileSize = s.RecBytes,
            BufferOverflows = s.RecOverruns,

            DetectionActive = s.DetActive,
            DetectionWindows = s.DetWindows,
            DetectionHits = s.DetHits,
            DetectionLastSpecies = s.DetLastSpecies ?? "",
            ModelLoaded = s.ModelLoaded,
            ModelSize = s.ModelSize,
            ModelLabels = (int)s.ModelLabels,

            LastUpdated = DateTime.Now,
        };
    }

    private static DeviceStatus MapGpsToPartialStatus(Quailtracker.GpsFix g)
    {
        DateTime? gpsTime = null;
        if (g.UtcTime is { Length: >= 14 } timeStr &&
            DateTime.TryParseExact(timeStr, "yyyyMMddHHmmss",
                System.Globalization.CultureInfo.InvariantCulture,
                System.Globalization.DateTimeStyles.AssumeUniversal, out var t))
        {
            gpsTime = t;
        }

        return new DeviceStatus
        {
            GpsValid = g.Valid,
            GpsFixType = (int)g.FixType,
            GpsSatellites = (int)g.Satellites,
            Latitude = g.LatitudeE7 / 1e7,
            Longitude = g.LongitudeE7 / 1e7,
            Altitude = g.AltitudeMm / 1000f,
            PpsValid = g.PpsSynced,
            PpsCount = g.PpsCount,
            GpsTime = gpsTime,
            LastUpdated = DateTime.Now,
        };
    }

    private static DeviceStatus MapAudioToPartialStatus(Quailtracker.AudioLevel a)
    {
        return new DeviceStatus
        {
            PeakLevel = (int)a.Peak,
            ActivityRatio = (int)a.ActivityPct,
            LimiterClipCount = a.ClipCount,
            BufferUsed = (int)a.BufUsed,
            BufferCapacity = (int)a.BufCapacity,
            LastUpdated = DateTime.Now,
        };
    }

    private static DeviceStatus MapRecordingToPartialStatus(Quailtracker.RecordingState r)
    {
        return new DeviceStatus
        {
            IsRecording = r.Active,
            CurrentFilename = r.Filename is { Length: > 0 } ? r.Filename : null,
            CurrentFileSize = r.BytesWritten,
            BufferOverflows = r.Overruns,
            SdFreeBytes = (long)r.SdFreeKb * 1024,
            LastUpdated = DateTime.Now,
        };
    }

    private static DetectionEvent MapDetection(Quailtracker.Detection d)
    {
        return new DetectionEvent
        {
            Species = d.Species ?? "",
            Confidence = (int)d.Confidence,
            Timestamp = d.UtcTime ?? "",
            ReceivedAt = DateTime.Now,
        };
    }

    private static DeviceConfig MapConfig(Quailtracker.Config c)
    {
        // Map windows
        var windows = new List<TimeWindow>();
        foreach (var w in c.Windows)
        {
            var startH = (int)(w.StartHhmm / 100);
            var startM = (int)(w.StartHhmm % 100);
            var endH = (int)(w.EndHhmm / 100);
            var endM = (int)(w.EndHhmm % 100);
            windows.Add(new TimeWindow(
                new TimeOnly(Math.Clamp(startH, 0, 23), Math.Clamp(startM, 0, 59)),
                new TimeOnly(Math.Clamp(endH, 0, 23), Math.Clamp(endM, 0, 59))));
        }

        return new DeviceConfig
        {
            StationId = c.StationId is { Length: > 0 } ? c.StationId : "QT001",
            GainDb = (int)c.Gain,
            BandPassLowHz = (int)c.BpfLowHz,
            BandPassHighHz = (int)c.BpfHighHz,
            SampleRate = (int)c.SampleRate,
            Format = c.RecFormat == 1 ? RecordingFormat.WAV : RecordingFormat.FLAC,

            AmplitudeTriggerEnabled = c.TrigEnabled,
            AmplitudeThresholdDb = c.TrigDb,
            PreTriggerSeconds = (int)c.TrigPreS,
            PostTriggerSeconds = (int)c.TrigPostS,

            LowBatteryThresholdPercent = (int)c.LowBatPct,
            AutoStopOnLowBattery = c.AutoStop,

            ActivityMode = (ActivityFilterMode)c.ActMode,
            ActivityMinPercent = (int)c.ActMinPct,
            ActivityMaxPercent = (int)c.ActMaxPct,
            ActivityHoldSeconds = (int)c.ActHoldS,

            SunriseEnabled = c.SunriseEnabled,
            SunriseBeforeMinutes = (int)c.SunriseBefore,
            SunriseAfterMinutes = (int)c.SunriseAfter,
            SunsetEnabled = c.SunsetEnabled,
            SunsetBeforeMinutes = (int)c.SunsetBefore,
            SunsetAfterMinutes = (int)c.SunsetAfter,
            FreeformWindows = windows.ToArray(),

            Mission = (MissionMode)c.MissionMode,
            DetectionThresholdPercent = (int)c.DetThreshold,
            DetectionWindowStep = (int)c.DetStepS,

            SurveyLatitude = c.SurveyLatE7 / 1e7,
            SurveyLongitude = c.SurveyLonE7 / 1e7,
            SurveyAltitude = c.SurveyAltMm / 1000f,
            SurveyCount = (int)c.SurveyCount,
        };
    }
}
