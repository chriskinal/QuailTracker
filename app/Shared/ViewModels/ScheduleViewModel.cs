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

public partial class ScheduleViewModel : ObservableObject
{
    private readonly IBluetoothService _bluetoothService;

    // Schedule
    [ObservableProperty] private int _selectedScheduleModeIndex = 0;
    [ObservableProperty] private int _sunriseOffset = -30;
    [ObservableProperty] private int _sunsetOffset = 30;
    [ObservableProperty] private TimeSpan _manualStartTime = new(6, 0, 0);
    [ObservableProperty] private TimeSpan _manualEndTime = new(9, 0, 0);

    // UI State
    [ObservableProperty] private bool _hasChanges = false;
    [ObservableProperty] private string _statusMessage = "";

    public string[] ScheduleModeOptions { get; } = ["Manual", "Sunrise/Sunset", "Continuous"];

    public ScheduleViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.ConfigReceived += OnConfigReceived;

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
        SelectedScheduleModeIndex = (int)config.ScheduleMode;
        SunriseOffset = config.SunriseOffsetMinutes;
        SunsetOffset = config.SunsetOffsetMinutes;
        ManualStartTime = config.ManualStartTime.ToTimeSpan();
        ManualEndTime = config.ManualEndTime.ToTimeSpan();

        HasChanges = false;
        StatusMessage = "Schedule loaded";
    }

    [RelayCommand]
    private async Task LoadScheduleAsync()
    {
        StatusMessage = "Loading...";
        await _bluetoothService.RequestConfigAsync();
    }

    [RelayCommand]
    private async Task SaveScheduleAsync()
    {
        StatusMessage = "Saving...";

        var config = new DeviceConfig
        {
            ScheduleMode = (ScheduleMode)SelectedScheduleModeIndex,
            SunriseOffsetMinutes = SunriseOffset,
            SunsetOffsetMinutes = SunsetOffset,
            ManualStartTime = TimeOnly.FromTimeSpan(ManualStartTime),
            ManualEndTime = TimeOnly.FromTimeSpan(ManualEndTime),
        };

        var success = await _bluetoothService.SendScheduleAsync(config);

        if (success)
        {
            HasChanges = false;
            StatusMessage = "Schedule saved";
        }
        else
        {
            StatusMessage = "Save failed!";
        }
    }
}
