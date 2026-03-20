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
using System.Threading.Tasks;
using QuailTracker.Shared.Models;

namespace QuailTracker.Shared.Services;

/// <summary>
/// Bluetooth Low Energy service for communicating with QuailTracker devices.
/// </summary>
public interface IBluetoothService
{
    /// <summary>
    /// Current connection state.
    /// </summary>
    ConnectionState CurrentState { get; }

    /// <summary>
    /// Name of connected device (null if not connected).
    /// </summary>
    string? ConnectedDeviceName { get; }

    /// <summary>
    /// Fired when connection state changes.
    /// </summary>
    event EventHandler<ConnectionState>? ConnectionStateChanged;

    /// <summary>
    /// Fired when device status is received.
    /// </summary>
    event EventHandler<DeviceStatus>? StatusReceived;

    /// <summary>
    /// Fired when device configuration is received.
    /// </summary>
    event EventHandler<DeviceConfig>? ConfigReceived;

    /// <summary>
    /// Scan for QuailTracker devices and connect to the first one found.
    /// </summary>
    Task ScanAndConnectAsync();

    /// <summary>
    /// Stop scanning for devices.
    /// </summary>
    void StopScan();

    /// <summary>
    /// Disconnect from the current device.
    /// </summary>
    Task DisconnectAsync();

    /// <summary>
    /// Request current status from the device.
    /// </summary>
    Task RequestStatusAsync();

    /// <summary>
    /// Request current configuration from the device.
    /// </summary>
    Task RequestConfigAsync();

    /// <summary>
    /// Send configuration to the device.
    /// </summary>
    /// <param name="config">Configuration to send</param>
    /// <returns>True if successful</returns>
    Task<bool> SendConfigAsync(DeviceConfig config);

    /// <summary>
    /// Send a command to the device.
    /// </summary>
    /// <param name="command">Command string (e.g., "START", "STOP")</param>
    Task SendCommandAsync(string command);

    /// <summary>
    /// Send only schedule-related configuration to the device.
    /// </summary>
    /// <param name="config">Configuration containing schedule fields</param>
    /// <returns>True if successful</returns>
    Task<bool> SendScheduleAsync(DeviceConfig config);

    /// <summary>
    /// Send an SD card command (EJECT, MOUNT, FORMAT).
    /// </summary>
    /// <param name="operation">Operation string</param>
    Task SendSdCommandAsync(string operation);

    /// <summary>
    /// Start or stop audio level streaming (peak + activity ratio).
    /// </summary>
    /// <param name="intervalMs">Push interval in ms (0=off, 100-30000)</param>
    Task SetStreamAudioAsync(int intervalMs);

    /// <summary>
    /// Start or stop recording stats streaming (buffer, file size, clips, overflows).
    /// </summary>
    /// <param name="intervalMs">Push interval in ms (0=off, 100-30000)</param>
    Task SetStreamRecAsync(int intervalMs);

    /// <summary>
    /// Send a survey-in command (START, STOP, CLEAR).
    /// </summary>
    /// <param name="operation">Operation string</param>
    Task SendSurveyCommandAsync(string operation);

    /// <summary>
    /// Fired when a health report is received on connect.
    /// </summary>
    event EventHandler<HealthReport>? HealthReportReceived;

    /// <summary>
    /// Fired when a detection is received via $DETECTION push.
    /// </summary>
    event EventHandler<DetectionEvent>? DetectionReceived;

    /// <summary>
    /// Fired when a device is discovered during scanning.
    /// </summary>
    event EventHandler<DiscoveredDevice>? DeviceDiscovered;

    /// <summary>
    /// Scan for QuailTracker devices without auto-connecting.
    /// Fires DeviceDiscovered for each device found. Scan runs until StopScan() or 30s timeout.
    /// </summary>
    Task StartScanAsync();

    /// <summary>
    /// Connect to a specific discovered device.
    /// </summary>
    Task ConnectToDeviceAsync(DiscoveredDevice device);

    /// <summary>
    /// Send a model command (STATUS, RELOAD).
    /// </summary>
    Task SendModelCommandAsync(string operation);

    /// <summary>
    /// Send a detection command (STATUS, STREAM,<ms>).
    /// </summary>
    Task SendDetectionCommandAsync(string operation);

    /// <summary>
    /// Send detection configuration (mission mode, threshold, window step).
    /// </summary>
    Task<bool> SendDetectionConfigAsync(DeviceConfig config);
}

/// <summary>
/// BLE connection states.
/// </summary>
public enum ConnectionState
{
    Disconnected,
    Scanning,
    Connecting,
    Connected
}

/// <summary>
/// A BLE device discovered during scanning.
/// </summary>
public record DiscoveredDevice(Guid Id, string Name, int Rssi, DateTimeOffset LastSeen);

/// <summary>
/// A detection event pushed by the device.
/// </summary>
public record DetectionEvent
{
    public string Species { get; init; } = "";
    public int Confidence { get; init; }
    public string Timestamp { get; init; } = "";
    public DateTime ReceivedAt { get; init; } = DateTime.Now;
}
