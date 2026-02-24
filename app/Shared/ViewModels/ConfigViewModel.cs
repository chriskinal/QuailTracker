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
    [ObservableProperty] private int _bandPassLowHz = 150;
    [ObservableProperty] private int _bandPassHighHz = 8000;
    [ObservableProperty] private int _selectedSampleRateIndex = 0; // 48kHz
    [ObservableProperty] private int _selectedFormatIndex = 0; // FLAC

    // Triggering
    [ObservableProperty] private bool _amplitudeTriggerEnabled = false;
    [ObservableProperty] private int _amplitudeThreshold = -40;
    [ObservableProperty] private int _preTriggerSeconds = 2;
    [ObservableProperty] private int _postTriggerSeconds = 5;

    // Activity Filter
    [ObservableProperty] private int _selectedActivityModeIndex = 0;
    [ObservableProperty] private int _activityMinPercent = 5;
    [ObservableProperty] private int _activityMaxPercent = 80;
    [ObservableProperty] private int _activityHoldSeconds = 3;
    [ObservableProperty] private int _activityRatio = 0;
    public bool IsActivityFilterEnabled => SelectedActivityModeIndex > 0;
    public bool IsGateMode => SelectedActivityModeIndex == 3;

    partial void OnSelectedActivityModeIndexChanged(int value)
    {
        OnPropertyChanged(nameof(IsActivityFilterEnabled));
        OnPropertyChanged(nameof(IsGateMode));
    }

    // Audio Level (live from StatusReceived)
    [ObservableProperty] private int _audioLevel = 0;
    [ObservableProperty] private string _peakLevel = "-- dB";

    // Power
    [ObservableProperty] private int _lowBatteryThreshold = 10;
    [ObservableProperty] private bool _autoStopOnLowBattery = true;

    // UI State
    [ObservableProperty] private bool _hasChanges = false;
    [ObservableProperty] private string _statusMessage = "";

    // Options for ComboBoxes
    public string[] GainOptions { get; } = ["Low", "Low-Medium", "Medium", "Medium-High", "High"];
    public string[] ActivityModeOptions { get; } = ["Off", "Monitor", "Squelch", "Gate"];
    public string[] SampleRateOptions { get; } = ["48 kHz", "44.1 kHz", "32 kHz", "16 kHz"];
    public string[] FormatOptions { get; } = ["FLAC", "WAV"];

    public ConfigViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.ConfigReceived += OnConfigReceived;
        _bluetoothService.StatusReceived += OnStatusReceived;

        // Track changes
        PropertyChanged += (s, e) =>
        {
            if (e.PropertyName is not (nameof(HasChanges) or nameof(StatusMessage)
                or nameof(AudioLevel) or nameof(PeakLevel) or nameof(ActivityRatio)))
            {
                HasChanges = true;
            }
        };
    }

    private void OnStatusReceived(object? sender, DeviceStatus status)
    {
        AudioLevel = Math.Clamp(status.PeakLevel * 100 / 32768, 0, 100);
        PeakLevel = $"{20 * Math.Log10(Math.Max(1, status.PeakLevel) / 32768.0):F0} dB";
        ActivityRatio = status.ActivityRatio;
    }

    private void OnConfigReceived(object? sender, DeviceConfig config)
    {
        StationId = config.StationId;

        SelectedGainIndex = (int)config.Gain;
        BandPassLowHz = config.BandPassLowHz;
        BandPassHighHz = config.BandPassHighHz;
        SelectedFormatIndex = (int)config.Format;
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

        SelectedActivityModeIndex = (int)config.ActivityMode;
        ActivityMinPercent = config.ActivityMinPercent;
        ActivityMaxPercent = config.ActivityMaxPercent;
        ActivityHoldSeconds = config.ActivityHoldSeconds;

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
            BandPassLowHz = BandPassLowHz,
            BandPassHighHz = BandPassHighHz,
            Format = (RecordingFormat)SelectedFormatIndex,
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
            AutoStopOnLowBattery = AutoStopOnLowBattery,
            ActivityMode = (ActivityFilterMode)SelectedActivityModeIndex,
            ActivityMinPercent = ActivityMinPercent,
            ActivityMaxPercent = ActivityMaxPercent,
            ActivityHoldSeconds = ActivityHoldSeconds
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
        BandPassLowHz = 150;
        BandPassHighHz = 8000;
        SelectedSampleRateIndex = 0;
        SelectedFormatIndex = 0;
        AmplitudeTriggerEnabled = false;
        AmplitudeThreshold = -40;
        PreTriggerSeconds = 2;
        PostTriggerSeconds = 5;
        LowBatteryThreshold = 10;
        AutoStopOnLowBattery = true;
        SelectedActivityModeIndex = 0;
        ActivityMinPercent = 5;
        ActivityMaxPercent = 80;
        ActivityHoldSeconds = 3;

        HasChanges = true;
        StatusMessage = "Reset to defaults";
    }
}
