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
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Shared.Models;
using QuailTracker.Shared.Services;

namespace QuailTracker.Shared.ViewModels;

public partial class DetectViewModel : ObservableObject
{
    private readonly IBluetoothService _bluetoothService;

    // Model Status
    [ObservableProperty] private bool _modelLoaded;
    [ObservableProperty] private string _modelInfo = "No model";
    [ObservableProperty] private string _modelColor = "#808080";

    // Mission Mode
    [ObservableProperty] private int _selectedMissionIndex;
    public string[] MissionOptions { get; } = ["Record Only", "Detect Only", "Record + Detect"];

    // Detection Config
    [ObservableProperty] private int _detectionThreshold = 70;
    [ObservableProperty] private int _selectedWindowStepIndex;
    public string[] WindowStepOptions { get; } = ["1 second", "2 seconds", "3 seconds"];

    // Detection Stats
    [ObservableProperty] private bool _detectionActive;
    [ObservableProperty] private string _detectionStatus = "Inactive";
    [ObservableProperty] private string _detectionColor = "#808080";
    [ObservableProperty] private string _windowsProcessed = "0";
    [ObservableProperty] private string _detectionHits = "0";

    // Last Detection
    [ObservableProperty] private string _lastSpecies = "--";
    [ObservableProperty] private string _lastConfidence = "--%";
    [ObservableProperty] private string _lastTime = "--:--:--";

    // Live Detection Stream
    [ObservableProperty] private bool _streamEnabled;

    // Change tracking
    [ObservableProperty] private bool _hasChanges;
    [ObservableProperty] private string _statusMessage = "";

    // Live detection log
    public ObservableCollection<string> DetectionLog { get; } = new();

    public DetectViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.StatusReceived += OnStatusReceived;
        _bluetoothService.ConfigReceived += OnConfigReceived;
        _bluetoothService.DetectionReceived += OnDetectionReceived;
    }

    private void OnStatusReceived(object? sender, DeviceStatus status)
    {
        ModelLoaded = status.ModelLoaded;
        ModelInfo = status.ModelLoaded
            ? $"Loaded ({status.ModelSize / 1024f:F1} KB, {status.ModelLabels} classes)"
            : "No model loaded";
        ModelColor = status.ModelLoaded ? "#4CAF50" : "#F44336";

        DetectionActive = status.DetectionActive;
        DetectionStatus = status.DetectionActive ? "Active" : "Inactive";
        DetectionColor = status.DetectionActive ? "#4CAF50" : "#808080";

        WindowsProcessed = status.DetectionWindows.ToString("N0");
        DetectionHits = status.DetectionHits.ToString("N0");

        if (!string.IsNullOrEmpty(status.DetectionLastSpecies))
        {
            LastSpecies = status.DetectionLastSpecies;
        }
    }

    private void OnConfigReceived(object? sender, DeviceConfig config)
    {
        _selectedMissionIndex = (int)config.Mission;
        OnPropertyChanged(nameof(SelectedMissionIndex));

        _detectionThreshold = config.DetectionThresholdPercent;
        OnPropertyChanged(nameof(DetectionThreshold));

        _selectedWindowStepIndex = config.DetectionWindowStep - 1;
        OnPropertyChanged(nameof(SelectedWindowStepIndex));

        HasChanges = false;
    }

    private void OnDetectionReceived(object? sender, DetectionEvent det)
    {
        LastSpecies = det.Species;
        LastConfidence = $"{det.Confidence}%";
        LastTime = det.ReceivedAt.ToString("HH:mm:ss");

        DetectionLog.Insert(0, $"[{det.ReceivedAt:HH:mm:ss}] {det.Species} ({det.Confidence}%)");

        // Keep log to 50 entries
        while (DetectionLog.Count > 50)
            DetectionLog.RemoveAt(DetectionLog.Count - 1);
    }

    partial void OnSelectedMissionIndexChanged(int value) => HasChanges = true;
    partial void OnDetectionThresholdChanged(int value) => HasChanges = true;
    partial void OnSelectedWindowStepIndexChanged(int value) => HasChanges = true;

    [RelayCommand]
    private async Task SaveAsync()
    {
        var config = new DeviceConfig
        {
            Mission = (MissionMode)SelectedMissionIndex,
            DetectionThresholdPercent = DetectionThreshold,
            DetectionWindowStep = SelectedWindowStepIndex + 1,
        };

        StatusMessage = "Saving...";
        var ok = await _bluetoothService.SendDetectionConfigAsync(config);
        StatusMessage = ok ? "Saved" : "Save failed";
        if (ok) HasChanges = false;

        await Task.Delay(2000);
        StatusMessage = "";
    }

    [RelayCommand]
    private async Task ReloadModelAsync()
    {
        StatusMessage = "Reloading model...";
        await _bluetoothService.SendModelCommandAsync("RELOAD");
        StatusMessage = "";

        // Refresh status to see if model loaded
        await Task.Delay(2000);
        await _bluetoothService.RequestStatusAsync();
    }

    partial void OnStreamEnabledChanged(bool value)
    {
        _ = _bluetoothService.SendDetectionCommandAsync($"STREAM,{(value ? 1000 : 0)}");
    }
}
