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

using QuailTracker.App.Models;

namespace QuailTracker.App.Services;

/// <summary>
/// Mock Bluetooth service for testing and development without hardware.
/// Simulates a connected QuailTracker device with realistic data.
/// </summary>
public class MockBluetoothService : IBluetoothService
{
    private readonly Random _random = new();
    private bool _isRecording = false;
    private DeviceConfig _currentConfig = new();

    public ConnectionState CurrentState { get; private set; } = ConnectionState.Disconnected;
    public string? ConnectedDeviceName { get; private set; }

    public event EventHandler<ConnectionState>? ConnectionStateChanged;
    public event EventHandler<DeviceStatus>? StatusReceived;
    public event EventHandler<DeviceConfig>? ConfigReceived;

    public async Task ScanAndConnectAsync()
    {
        // Simulate scanning
        SetState(ConnectionState.Scanning);
        await Task.Delay(1500);

        // Simulate finding device
        SetState(ConnectionState.Connecting);
        await Task.Delay(800);

        // Connected
        ConnectedDeviceName = "QuailTracker-QT001";
        SetState(ConnectionState.Connected);

        // Send initial status
        await Task.Delay(200);
        await RequestStatusAsync();
    }

    public void StopScan()
    {
        if (CurrentState == ConnectionState.Scanning)
        {
            SetState(ConnectionState.Disconnected);
        }
    }

    public async Task DisconnectAsync()
    {
        await Task.Delay(200);
        ConnectedDeviceName = null;
        SetState(ConnectionState.Disconnected);
    }

    public async Task RequestStatusAsync()
    {
        if (CurrentState != ConnectionState.Connected) return;

        await Task.Delay(100);

        var status = new DeviceStatus
        {
            StationId = "QT001",
            FirmwareVersion = "0.1.0",

            BatteryVoltage = 3.7f + (float)(_random.NextDouble() * 0.3),
            BatteryPercentage = 65 + _random.Next(-10, 10),
            BatteryLevel = BatteryLevel.Ok,

            GpsValid = true,
            GpsSatellites = 8 + _random.Next(-3, 4),
            Latitude = 32.2226 + _random.NextDouble() * 0.001,
            Longitude = -110.9747 + _random.NextDouble() * 0.001,
            Altitude = 728f + (float)(_random.NextDouble() * 10),
            PpsValid = true,
            GpsTime = DateTime.UtcNow,

            Temperature = 24.5f + (float)(_random.NextDouble() * 3),
            Humidity = 45f + (float)(_random.NextDouble() * 10),

            SdCardMounted = true,
            SdTotalBytes = 32L * 1024 * 1024 * 1024,
            SdUsedBytes = 8L * 1024 * 1024 * 1024 + _random.Next(0, 100) * 1024 * 1024,
            SdFreeBytes = 24L * 1024 * 1024 * 1024,

            IsRecording = _isRecording,
            CurrentFilename = _isRecording ? "20260121_063042_QT001.wav" : null,
            CurrentFileSize = _isRecording ? (uint)_random.Next(1000000, 50000000) : 0,
            SamplesCaptured = _isRecording ? (uint)_random.Next(1000000, 10000000) : 0,
            BufferOverflows = 0,

            PeakLevel = _random.Next(1000, 20000),
            BufferUsed = _random.Next(1000, 40000),
            BufferCapacity = 48000,

            LastUpdated = DateTime.Now
        };

        StatusReceived?.Invoke(this, status);
    }

    public async Task RequestConfigAsync()
    {
        if (CurrentState != ConnectionState.Connected) return;

        await Task.Delay(100);
        ConfigReceived?.Invoke(this, _currentConfig);
    }

    public async Task<bool> SendConfigAsync(DeviceConfig config)
    {
        if (CurrentState != ConnectionState.Connected) return false;

        await Task.Delay(500);
        _currentConfig = config;
        return true;
    }

    public async Task SendCommandAsync(string command)
    {
        if (CurrentState != ConnectionState.Connected) return;

        await Task.Delay(100);

        switch (command.ToUpperInvariant())
        {
            case "START":
                _isRecording = true;
                break;
            case "STOP":
                _isRecording = false;
                break;
        }

        // Send updated status
        await RequestStatusAsync();
    }

    private void SetState(ConnectionState state)
    {
        CurrentState = state;
        ConnectionStateChanged?.Invoke(this, state);
    }
}
