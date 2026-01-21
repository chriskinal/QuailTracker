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
