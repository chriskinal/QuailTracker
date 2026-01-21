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

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.App.Models;
using QuailTracker.App.Services;

namespace QuailTracker.App.ViewModels;

public partial class HealthViewModel : ObservableObject
{
    private readonly IBluetoothService _bluetoothService;

    // Device Info
    [ObservableProperty] private string _stationId = "--";
    [ObservableProperty] private string _firmwareVersion = "--";

    // Battery
    [ObservableProperty] private string _batteryVoltage = "-- V";
    [ObservableProperty] private int _batteryPercentage = 0;
    [ObservableProperty] private string _batteryStatus = "Unknown";
    [ObservableProperty] private string _batteryColor = "#808080";

    // GPS
    [ObservableProperty] private string _gpsStatus = "No Fix";
    [ObservableProperty] private string _gpsSatellites = "0";
    [ObservableProperty] private string _gpsPosition = "---, ---";
    [ObservableProperty] private string _gpsTime = "--:--:--";
    [ObservableProperty] private string _ppsStatus = "No Signal";
    [ObservableProperty] private string _gpsColor = "#808080";

    // Environment
    [ObservableProperty] private string _temperature = "-- C";
    [ObservableProperty] private string _humidity = "-- %";

    // SD Card
    [ObservableProperty] private string _sdStatus = "Not Mounted";
    [ObservableProperty] private string _sdUsage = "-- / -- GB";
    [ObservableProperty] private int _sdUsagePercent = 0;
    [ObservableProperty] private string _sdColor = "#808080";

    // Recording
    [ObservableProperty] private bool _isRecording = false;
    [ObservableProperty] private string _recordingStatus = "Idle";
    [ObservableProperty] private string _currentFile = "--";
    [ObservableProperty] private string _fileSize = "0 KB";
    [ObservableProperty] private string _bufferStatus = "0 / 0";
    [ObservableProperty] private string _overflows = "0";

    // Audio Level
    [ObservableProperty] private int _audioLevel = 0;
    [ObservableProperty] private string _peakLevel = "0 dB";

    // Last Update
    [ObservableProperty] private string _lastUpdated = "Never";

    public HealthViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.StatusReceived += OnStatusReceived;
    }

    private void OnStatusReceived(object? sender, DeviceStatus status)
    {
        // Device Info
        StationId = status.StationId;
        FirmwareVersion = $"v{status.FirmwareVersion}";

        // Battery
        BatteryVoltage = $"{status.BatteryVoltage:F2} V";
        BatteryPercentage = status.BatteryPercentage;
        BatteryStatus = status.BatteryLevel switch
        {
            BatteryLevel.Ok => "OK",
            BatteryLevel.Low => "Low",
            BatteryLevel.Critical => "Critical!",
            _ => "Unknown"
        };
        BatteryColor = status.BatteryLevel switch
        {
            BatteryLevel.Ok => "#4CAF50",
            BatteryLevel.Low => "#FF9800",
            BatteryLevel.Critical => "#F44336",
            _ => "#808080"
        };

        // GPS
        GpsStatus = status.GpsValid ? "Fix OK" : "No Fix";
        GpsSatellites = status.GpsSatellites.ToString();
        GpsPosition = status.GpsValid
            ? $"{status.Latitude:F6}, {status.Longitude:F6}"
            : "---, ---";
        GpsTime = status.GpsTime?.ToString("HH:mm:ss UTC") ?? "--:--:--";
        PpsStatus = status.PpsValid ? "OK" : "No Signal";
        GpsColor = status.GpsValid ? "#4CAF50" : "#F44336";

        // Environment
        Temperature = $"{status.Temperature:F1} C";
        Humidity = $"{status.Humidity:F1} %";

        // SD Card
        SdStatus = status.SdCardMounted ? "Mounted" : "Not Mounted";
        if (status.SdCardMounted)
        {
            var totalGb = status.SdTotalBytes / (1024.0 * 1024.0 * 1024.0);
            var usedGb = status.SdUsedBytes / (1024.0 * 1024.0 * 1024.0);
            SdUsage = $"{usedGb:F1} / {totalGb:F1} GB";
            SdUsagePercent = status.SdTotalBytes > 0
                ? (int)(100 * status.SdUsedBytes / status.SdTotalBytes)
                : 0;
            SdColor = SdUsagePercent > 90 ? "#F44336" : "#4CAF50";
        }
        else
        {
            SdUsage = "-- / -- GB";
            SdUsagePercent = 0;
            SdColor = "#F44336";
        }

        // Recording
        IsRecording = status.IsRecording;
        RecordingStatus = status.IsRecording ? "Recording" : "Idle";
        CurrentFile = status.CurrentFilename ?? "--";
        FileSize = FormatFileSize(status.CurrentFileSize);
        BufferStatus = $"{status.BufferUsed} / {status.BufferCapacity}";
        Overflows = status.BufferOverflows.ToString();

        // Audio
        AudioLevel = Math.Clamp(status.PeakLevel * 100 / 32768, 0, 100);
        PeakLevel = $"{20 * Math.Log10(Math.Max(1, status.PeakLevel) / 32768.0):F0} dB";

        // Timestamp
        LastUpdated = status.LastUpdated.ToString("HH:mm:ss");
    }

    [RelayCommand]
    private async Task RefreshAsync()
    {
        await _bluetoothService.RequestStatusAsync();
    }

    [RelayCommand]
    private async Task ToggleRecordingAsync()
    {
        if (IsRecording)
        {
            await _bluetoothService.SendCommandAsync("STOP");
        }
        else
        {
            await _bluetoothService.SendCommandAsync("START");
        }
    }

    private static string FormatFileSize(long bytes)
    {
        if (bytes < 1024) return $"{bytes} B";
        if (bytes < 1024 * 1024) return $"{bytes / 1024.0:F1} KB";
        if (bytes < 1024 * 1024 * 1024) return $"{bytes / (1024.0 * 1024.0):F1} MB";
        return $"{bytes / (1024.0 * 1024.0 * 1024.0):F2} GB";
    }
}
