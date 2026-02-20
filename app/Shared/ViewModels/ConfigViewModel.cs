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

public partial class ConfigViewModel : ObservableObject
{
    private readonly IBluetoothService _bluetoothService;

    // Station Identity
    [ObservableProperty] private string _stationId = "QT001";

    // Audio Settings
    [ObservableProperty] private int _selectedGainIndex = 2; // Medium
    [ObservableProperty] private int _selectedHighPassIndex = 1; // 8 Hz
    [ObservableProperty] private int _selectedSampleRateIndex = 0; // 48kHz

    // Triggering
    [ObservableProperty] private bool _amplitudeTriggerEnabled = false;
    [ObservableProperty] private int _amplitudeThreshold = -40;
    [ObservableProperty] private int _preTriggerSeconds = 2;
    [ObservableProperty] private int _postTriggerSeconds = 5;

    // Power
    [ObservableProperty] private int _lowBatteryThreshold = 10;
    [ObservableProperty] private bool _autoStopOnLowBattery = true;

    // UI State
    [ObservableProperty] private bool _hasChanges = false;
    [ObservableProperty] private string _statusMessage = "";

    // Options for ComboBoxes
    public string[] GainOptions { get; } = ["Low", "Low-Medium", "Medium", "Medium-High", "High"];
    public string[] HighPassOptions { get; } = ["Disabled", "8 Hz", "48 Hz"];
    public string[] SampleRateOptions { get; } = ["48 kHz", "44.1 kHz", "32 kHz", "16 kHz"];

    public ConfigViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.ConfigReceived += OnConfigReceived;

        // Track changes
        PropertyChanged += (s, e) =>
        {
            if (e.PropertyName != nameof(HasChanges) && e.PropertyName != nameof(StatusMessage))
            {
                HasChanges = true;
            }
        };
    }

    private void OnConfigReceived(object? sender, DeviceConfig config)
    {
        StationId = config.StationId;

        SelectedGainIndex = (int)config.Gain;
        SelectedHighPassIndex = (int)config.HighPassFilter;
        SelectedSampleRateIndex = config.SampleRate switch
        {
            48000 => 0,
            44100 => 1,
            32000 => 2,
            16000 => 3,
            _ => 0
        };

        AmplitudeTriggerEnabled = config.AmplitudeTriggerEnabled;
        AmplitudeThreshold = config.AmplitudeThresholdDb;
        PreTriggerSeconds = config.PreTriggerSeconds;
        PostTriggerSeconds = config.PostTriggerSeconds;

        LowBatteryThreshold = config.LowBatteryThresholdPercent;
        AutoStopOnLowBattery = config.AutoStopOnLowBattery;

        HasChanges = false;
        StatusMessage = "Configuration loaded";
    }

    [RelayCommand]
    private async Task LoadConfigAsync()
    {
        StatusMessage = "Loading...";
        await _bluetoothService.RequestConfigAsync();
    }

    [RelayCommand]
    private async Task SaveConfigAsync()
    {
        StatusMessage = "Saving...";

        var config = new DeviceConfig
        {
            StationId = StationId,
            Gain = (GainLevel)SelectedGainIndex,
            HighPassFilter = (HighPassFilter)SelectedHighPassIndex,
            SampleRate = SelectedSampleRateIndex switch
            {
                0 => 48000,
                1 => 44100,
                2 => 32000,
                3 => 16000,
                _ => 48000
            },
            AmplitudeTriggerEnabled = AmplitudeTriggerEnabled,
            AmplitudeThresholdDb = AmplitudeThreshold,
            PreTriggerSeconds = PreTriggerSeconds,
            PostTriggerSeconds = PostTriggerSeconds,
            LowBatteryThresholdPercent = LowBatteryThreshold,
            AutoStopOnLowBattery = AutoStopOnLowBattery
        };

        var success = await _bluetoothService.SendConfigAsync(config);

        if (success)
        {
            HasChanges = false;
            StatusMessage = "Configuration saved";
        }
        else
        {
            StatusMessage = "Save failed!";
        }
    }

    [RelayCommand]
    private void ResetToDefaults()
    {
        StationId = "QT001";
        SelectedGainIndex = 2;
        SelectedHighPassIndex = 1;
        SelectedSampleRateIndex = 0;
        AmplitudeTriggerEnabled = false;
        AmplitudeThreshold = -40;
        PreTriggerSeconds = 2;
        PostTriggerSeconds = 5;
        LowBatteryThreshold = 10;
        AutoStopOnLowBattery = true;

        HasChanges = true;
        StatusMessage = "Reset to defaults";
    }
}
