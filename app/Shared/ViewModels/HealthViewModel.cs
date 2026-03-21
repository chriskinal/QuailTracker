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
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Shared.Models;
using QuailTracker.Shared.Services;

namespace QuailTracker.Shared.ViewModels;

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
    [ObservableProperty] private string _ppsCount = "";
    [ObservableProperty] private string _ppsAge = "";
    [ObservableProperty] private string _gpsColor = "#808080";

    // Environment
    [ObservableProperty] private string _temperature = "-- C";
    [ObservableProperty] private string _humidity = "-- %";

    // SD Card
    [ObservableProperty] private string _sdStatus = "Not Mounted";
    [ObservableProperty] private string _sdFree = "-- GB free";
    [ObservableProperty] private int _sdFreePercent = 0;
    [ObservableProperty] private string _sdColor = "#808080";

    // BLE Connection
    [ObservableProperty] private string _bleStatus = "Disconnected";
    [ObservableProperty] private string _bleDeviceName = "--";
    [ObservableProperty] private string _bleAddress = "--";

    // Last Update
    [ObservableProperty] private string _lastUpdated = "Never";

    // Since Last Visit
    [ObservableProperty] private bool _hasHealthReport = false;
    [ObservableProperty] private string _healthRec = "No data";
    [ObservableProperty] private string _healthDet = "No data";
    [ObservableProperty] private string _healthBat = "No data";
    [ObservableProperty] private string _healthSys = "No data";
    [ObservableProperty] private string _healthOverallColor = "#808080";
    [ObservableProperty] private string _healthOverallText = "Unknown";
    [ObservableProperty] private string _healthRecColor = "White";
    [ObservableProperty] private string _healthBatColor = "White";
    [ObservableProperty] private string _healthSysColor = "White";

    public HealthViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.StatusReceived += OnStatusReceived;
        _bluetoothService.ConnectionStateChanged += OnConnectionStateChanged;
        _bluetoothService.HealthReportReceived += OnHealthReportReceived;
    }

    private void OnConnectionStateChanged(object? sender, ConnectionState state)
    {
        BleStatus = state switch
        {
            ConnectionState.Disconnected => "Disconnected",
            ConnectionState.Scanning => "Scanning...",
            ConnectionState.Connecting => "Connecting...",
            ConnectionState.Connected => "Connected",
            _ => "Unknown"
        };
        BleDeviceName = state == ConnectionState.Connected
            ? _bluetoothService.ConnectedDeviceName ?? "QuailTracker"
            : "--";
        if (state != ConnectionState.Connected)
            BleAddress = "--";
    }

    private void OnHealthReportReceived(object? sender, Models.HealthReport hr)
    {
        HasHealthReport = true;

        // Recording line
        var totalGb = hr.TotalBytes / (1024.0 * 1024.0 * 1024.0);
        var recHours = hr.RecordingSecs / 3600;
        var recMins = (hr.RecordingSecs % 3600) / 60;
        HealthRec = $"{hr.FilesWritten} files   {totalGb:F1} GB   {recHours}h {recMins:D2}m   {hr.WriteErrors} errors";

        // Detection line
        if (hr.Detections > 0 && hr.LastSpecies.Length > 0)
            HealthDet = $"{hr.Detections} detections   {hr.LastSpecies} ({hr.LastConfidence}%)";
        else if (hr.Detections > 0)
            HealthDet = $"{hr.Detections} detections";
        else
            HealthDet = "0 detections";

        // Battery/temp line
        var batMin = hr.BatteryMinMv / 1000.0;
        var batMax = hr.BatteryMaxMv / 1000.0;
        var tempMin = hr.TempMinC100 / 100.0;
        var tempMax = hr.TempMaxC100 / 100.0;
        if (hr.BatteryMaxMv > 0)
            HealthBat = $"{batMin:F2} - {batMax:F2} V      Temp  {tempMin:F1} - {tempMax:F1} C";
        else
            HealthBat = "No data";

        // Overall status — collect reasons, color problem lines red
        var reasons = new System.Collections.Generic.List<string>();
        bool recIssue = hr.WriteErrors > 0;
        bool batIssue = hr.BatteryMinMv > 0 && hr.BatteryMinMv < 3300;
        bool sysIssue = hr.SdErrors > 0 || hr.BootCount > 1;

        if (recIssue) reasons.Add($"{hr.WriteErrors} write err");
        if (batIssue) reasons.Add("low bat");
        if (hr.SdErrors > 0) reasons.Add($"{hr.SdErrors} SD err");
        if (hr.BootCount > 1) reasons.Add($"{hr.BootCount} reboots");

        HealthRecColor = recIssue ? "#F44336" : "White";
        HealthBatColor = batIssue ? "#F44336" : "White";
        HealthSysColor = sysIssue ? "#F44336" : "White";

        if (reasons.Count > 0)
        {
            HealthOverallColor = "#F44336";
            HealthOverallText = string.Join(", ", reasons);
        }
        else
        {
            HealthOverallColor = "#4CAF50";
            HealthOverallText = "All Good";
        }

        // System line
        var days = hr.UptimeSecs / 86400;
        var hours = (hr.UptimeSecs % 86400) / 3600;
        var uptimeStr = days > 0 ? $"{days}d {hours}h" : $"{hours}h";
        HealthSys = $"{hr.BootCount} boot   {hr.SdErrors} SD err   {hr.GpsFixLosses} GPS loss   {uptimeStr} up";
    }

    private void OnStatusReceived(object? sender, DeviceStatus status)
    {
        // Pick up cached health report if we missed the event
        if (!HasHealthReport && _bluetoothService.LastHealthReport is { } cached)
            OnHealthReportReceived(this, cached);

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
        GpsSatellites = $"{status.GpsSatellites} sats";
        GpsPosition = status.GpsValid
            ? $"{status.Latitude:F6}, {status.Longitude:F6}"
            : "---, ---";
        GpsTime = status.GpsTime?.ToString("HH:mm:ss UTC") ?? "--:--:--";
        PpsStatus = status.PpsValid ? "OK" : "--";
        PpsCount = status.PpsCount > 0 ? $"Count {status.PpsCount}" : "";
        PpsAge = status.PpsAgeMs > 0 ? $"{status.PpsAgeMs}ms" : "";
        GpsColor = status.GpsValid ? "#4CAF50" : "#F44336";

        // Environment
        Temperature = $"{status.Temperature:F1} C";
        Humidity = $"{status.Humidity:F1} %";

        // SD Card
        SdStatus = status.SdCardMounted ? "Mounted" : "Not Mounted";
        if (status.SdCardMounted)
        {
            var freeGb = status.SdFreeBytes / (1024.0 * 1024.0 * 1024.0);
            SdFree = $"{freeGb:F1} GB free";
            SdFreePercent = status.SdTotalBytes > 0
                ? (int)(100 * status.SdFreeBytes / status.SdTotalBytes)
                : 0;
            SdColor = SdFreePercent < 10 ? "#F44336" : SdFreePercent < 25 ? "#FF9800" : "#4CAF50";
        }
        else
        {
            SdFree = "-- GB free";
            SdFreePercent = 0;
            SdColor = "#F44336";
        }

        // BLE address from firmware
        if (status.BleModuleAddr is { Length: > 0 })
            BleAddress = status.BleModuleAddr;

        // Timestamp
        LastUpdated = status.LastUpdated.ToString("HH:mm:ss");
    }

    [RelayCommand]
    private async Task RefreshAsync()
    {
        await _bluetoothService.RequestStatusAsync();
    }
}
