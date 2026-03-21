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

public partial class OperationsViewModel : ObservableObject
{
    private readonly IBluetoothService _bluetoothService;

    // Recording
    [ObservableProperty] private bool _isRecording = false;
    [ObservableProperty] private string _recordingStatus = "Idle";
    [ObservableProperty] private string _recordingColor = "#4CAF50";
    [ObservableProperty] private string _recordButtonText = "Start Recording";
    [ObservableProperty] private string _currentFile = "--";
    [ObservableProperty] private string _fileSize = "0 KB";
    [ObservableProperty] private string _bufferStatus = "0 / 0";
    [ObservableProperty] private string _overflows = "0";
    [ObservableProperty] private string _limiterClips = "0";

    // SD Card
    [ObservableProperty] private string _sdStatus = "Not Mounted";
    [ObservableProperty] private string _sdColor = "#808080";
    [ObservableProperty] private bool _isSdMounted = false;
    [ObservableProperty] private string _formatButtonText = "Format";
    [ObservableProperty] private string _formatButtonColor = "#E53935";
    [ObservableProperty] private bool _isFormatting = false;
    private bool _formatConfirmPending = false;

    // GPS Detail
    [ObservableProperty] private string _gpsFixType = "No Fix";
    [ObservableProperty] private string _gpsSatellites = "0";
    [ObservableProperty] private string _gpsPosition = "---, ---";
    [ObservableProperty] private string _gpsDate = "--";
    [ObservableProperty] private string _gpsTime = "--:--:--";
    [ObservableProperty] private string _ppsStatus = "No Signal";
    [ObservableProperty] private string _ppsCount = "0";
    [ObservableProperty] private string _ppsAge = "--";
    [ObservableProperty] private string _gpsColor = "#808080";

    // BLE Module Detail
    [ObservableProperty] private string _bleModule = "Not detected";
    [ObservableProperty] private string _bleName = "--";
    [ObservableProperty] private string _bleAddr = "--";
    [ObservableProperty] private string _bleLink = "Disconnected";
    [ObservableProperty] private string _bleColor = "#808080";

    // Survey-In
    [ObservableProperty] private string _surveyPosition = "---, ---";
    [ObservableProperty] private string _surveyAltitude = "--";
    [ObservableProperty] private string _surveyCount = "0";
    [ObservableProperty] private bool _surveyActive = false;
    [ObservableProperty] private string _surveyButtonText = "Start Survey";
    [ObservableProperty] private string _surveyButtonColor = "#DD34495E";
    [ObservableProperty] private string _surveyColor = "#808080";
    [ObservableProperty] private string _surveyBadge = "0 fixes";
    [ObservableProperty] private bool _canStartSurvey = false;
    private int _gpsSatCount = 0;

    public OperationsViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.StatusReceived += OnStatusReceived;
    }

    private void OnStatusReceived(object? sender, DeviceStatus status)
    {
        // Recording
        IsRecording = status.IsRecording;
        RecordingStatus = status.IsRecording ? "Recording" : "Idle";
        RecordingColor = status.IsRecording ? "#F44336" : "#4CAF50";
        RecordButtonText = status.IsRecording ? "Stop Recording" : "Start Recording";
        CurrentFile = status.CurrentFilename ?? "--";
        FileSize = FormatFileSize(status.CurrentFileSize);
        BufferStatus = $"{status.BufferUsed} / {status.BufferCapacity}";
        Overflows = status.BufferOverflows.ToString();
        LimiterClips = status.LimiterClipCount.ToString();

        // SD Card
        SdStatus = status.SdCardMounted ? "Mounted" : "Not Mounted";
        SdColor = status.SdCardMounted ? "#4CAF50" : "#F44336";
        IsSdMounted = status.SdCardMounted;

        // GPS Detail
        GpsFixType = status.GpsFixType switch
        {
            2 => "DGPS",
            1 => "GPS",
            _ => "No Fix"
        };
        GpsSatellites = status.GpsSatellites.ToString();
        GpsPosition = status.GpsValid
            ? $"{status.Latitude:F6}, {status.Longitude:F6}"
            : "---, ---";
        GpsDate = status.GpsDate is { Length: > 0 } d ? d : "--";
        GpsTime = status.GpsTime?.ToString("HH:mm:ss UTC") ?? "--:--:--";
        PpsStatus = status.PpsValid ? "Synced" : "No Signal";
        PpsCount = status.PpsCount.ToString();
        PpsAge = status.PpsCount > 0 ? $"{status.PpsAgeMs} ms" : "--";
        GpsColor = status.GpsValid ? "#4CAF50" : "#F44336";

        // BLE Module
        BleModule = status.BleModuleReady ? "HM-19" : "Not detected";
        BleName = status.BleModuleName is { Length: > 0 } n ? n : "--";
        BleAddr = status.BleModuleAddr is { Length: > 0 } a ? a : "--";
        BleLink = status.BleConnected ? "Connected" : "Idle";
        BleColor = status.BleModuleReady ? "#4CAF50" : "#F44336";

        // Survey-In
        _gpsSatCount = status.GpsSatellites;
        SurveyActive = status.SurveyActive;
        SurveyCount = status.SurveyCount > 0 ? $"{status.SurveyCount} fixes" : "0";
        SurveyButtonText = status.SurveyActive ? "Stop Survey" : "Start Survey";
        SurveyButtonColor = status.SurveyActive ? "#FF9800" : "#DD34495E";
        CanStartSurvey = status.GpsSatellites >= 4 && !status.IsRecording;

        if (status.SurveyActive)
        {
            var min = status.SurveySecondsLeft / 60;
            var sec = status.SurveySecondsLeft % 60;
            SurveyBadge = $"{min}:{sec:D2} left";
            SurveyColor = "#FF9800";
        }
        else if (status.SurveyCount > 0)
        {
            SurveyBadge = "Complete";
            SurveyColor = "#4CAF50";
        }
        else
        {
            SurveyBadge = "No data";
            SurveyColor = "#808080";
        }

        if (status.SurveyCount > 0)
        {
            SurveyPosition = $"{status.SurveyLatitude:F6}, {status.SurveyLongitude:F6}";
            SurveyAltitude = $"{status.SurveyAltitude:F1} m";
        }
        else
        {
            SurveyPosition = "---, ---";
            SurveyAltitude = "--";
        }
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

    [RelayCommand]
    private async Task EjectSdAsync()
    {
        await _bluetoothService.SendSdCommandAsync("EJECT");
        await _bluetoothService.RequestStatusAsync();
    }

    [RelayCommand]
    private async Task MountSdAsync()
    {
        await _bluetoothService.SendSdCommandAsync("MOUNT");
        await _bluetoothService.RequestStatusAsync();
    }

    [RelayCommand]
    private async Task FormatSdAsync()
    {
        if (IsFormatting) return;

        if (!_formatConfirmPending)
        {
            _formatConfirmPending = true;
            FormatButtonText = "Confirm?";
            FormatButtonColor = "#FF9800";
            // Auto-cancel after 5 seconds
            _ = Task.Delay(5000).ContinueWith(_ =>
            {
                if (_formatConfirmPending)
                {
                    _formatConfirmPending = false;
                    FormatButtonText = "Format";
                    FormatButtonColor = "#E53935";
                }
            });
            return;
        }

        _formatConfirmPending = false;
        IsFormatting = true;
        FormatButtonText = "Formatting...";
        FormatButtonColor = "#808080";
        SdStatus = "Formatting...";
        SdColor = "#FF9800";

        await _bluetoothService.SendSdCommandAsync("FORMAT");
        await _bluetoothService.RequestStatusAsync();

        IsFormatting = false;
        FormatButtonText = "Format";
        FormatButtonColor = "#E53935";
    }

    [RelayCommand]
    private async Task ToggleSurveyAsync()
    {
        if (SurveyActive)
        {
            await _bluetoothService.SendSurveyCommandAsync("STOP");
        }
        else
        {
            await _bluetoothService.SendSurveyCommandAsync("START");
        }
        await _bluetoothService.RequestStatusAsync();
    }

    [RelayCommand]
    private async Task ClearSurveyAsync()
    {
        await _bluetoothService.SendSurveyCommandAsync("CLEAR");
        await _bluetoothService.RequestStatusAsync();
    }

    private static string FormatFileSize(long bytes)
    {
        if (bytes < 1024) return $"{bytes} B";
        if (bytes < 1024 * 1024) return $"{bytes / 1024.0:F1} KB";
        if (bytes < 1024 * 1024 * 1024) return $"{bytes / (1024.0 * 1024.0):F1} MB";
        return $"{bytes / (1024.0 * 1024.0 * 1024.0):F2} GB";
    }
}
