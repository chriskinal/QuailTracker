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

    // Audio Level
    [ObservableProperty] private int _audioLevel = 0;
    [ObservableProperty] private string _peakLevel = "0 dB";

    // SD Card
    [ObservableProperty] private string _sdStatus = "Not Mounted";
    [ObservableProperty] private bool _isSdMounted = false;

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

        // Audio
        AudioLevel = Math.Clamp(status.PeakLevel * 100 / 32768, 0, 100);
        PeakLevel = $"{20 * Math.Log10(Math.Max(1, status.PeakLevel) / 32768.0):F0} dB";

        // SD Card
        SdStatus = status.SdCardMounted ? "Mounted" : "Not Mounted";
        IsSdMounted = status.SdCardMounted;
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
        await _bluetoothService.SendSdCommandAsync("FORMAT");
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
