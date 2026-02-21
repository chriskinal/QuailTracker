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
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Shared.Models;
using QuailTracker.Shared.Services;

namespace QuailTracker.Shared.ViewModels;

public partial class TimeWindowItem : ObservableObject
{
    [ObservableProperty] private TimeSpan _startTime;
    [ObservableProperty] private TimeSpan _endTime;

    public TimeWindowItem(TimeSpan start, TimeSpan end)
    {
        _startTime = start;
        _endTime = end;
    }
}

public partial class ScheduleViewModel : ObservableObject
{
    private readonly IBluetoothService _bluetoothService;

    // Sunrise
    [ObservableProperty] private bool _sunriseEnabled = true;
    [ObservableProperty] private int _sunriseBefore = 30;
    [ObservableProperty] private int _sunriseAfter = 60;

    // Sunset
    [ObservableProperty] private bool _sunsetEnabled = true;
    [ObservableProperty] private int _sunsetBefore = 30;
    [ObservableProperty] private int _sunsetAfter = 30;

    // Freeform windows
    public ObservableCollection<TimeWindowItem> Windows { get; } = new();

    // UI state
    [ObservableProperty] private bool _hasChanges;
    [ObservableProperty] private string _statusMessage = "";

    public ScheduleViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.ConfigReceived += OnConfigReceived;

        Windows.CollectionChanged += (_, _) => HasChanges = true;

        PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is not (nameof(HasChanges) or nameof(StatusMessage)))
            {
                HasChanges = true;
            }
        };
    }

    private void OnConfigReceived(object? sender, DeviceConfig config)
    {
        SunriseEnabled = config.SunriseEnabled;
        SunriseBefore = config.SunriseBeforeMinutes;
        SunriseAfter = config.SunriseAfterMinutes;
        SunsetEnabled = config.SunsetEnabled;
        SunsetBefore = config.SunsetBeforeMinutes;
        SunsetAfter = config.SunsetAfterMinutes;

        Windows.Clear();
        foreach (var w in config.FreeformWindows)
            Windows.Add(new TimeWindowItem(w.Start.ToTimeSpan(), w.End.ToTimeSpan()));

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
            SunriseEnabled = SunriseEnabled,
            SunriseBeforeMinutes = SunriseBefore,
            SunriseAfterMinutes = SunriseAfter,
            SunsetEnabled = SunsetEnabled,
            SunsetBeforeMinutes = SunsetBefore,
            SunsetAfterMinutes = SunsetAfter,
            FreeformWindows = Windows
                .Select(w => new TimeWindow(
                    TimeOnly.FromTimeSpan(w.StartTime),
                    TimeOnly.FromTimeSpan(w.EndTime)))
                .ToArray(),
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

    [RelayCommand]
    private void AddWindow()
    {
        Windows.Add(new TimeWindowItem(new TimeSpan(6, 0, 0), new TimeSpan(9, 0, 0)));
    }

    [RelayCommand]
    private void RemoveWindow(TimeWindowItem? item)
    {
        if (item != null)
            Windows.Remove(item);
    }

    [RelayCommand]
    private void PresetDawnDusk()
    {
        SunriseEnabled = true;
        SunriseBefore = 30;
        SunriseAfter = 90;
        SunsetEnabled = true;
        SunsetBefore = 30;
        SunsetAfter = 60;
        Windows.Clear();
    }

    [RelayCommand]
    private void PresetDawnOnly()
    {
        SunriseEnabled = true;
        SunriseBefore = 30;
        SunriseAfter = 120;
        SunsetEnabled = false;
        SunsetBefore = 30;
        SunsetAfter = 30;
        Windows.Clear();
    }

    [RelayCommand]
    private void PresetNocturnal()
    {
        SunriseEnabled = false;
        SunsetEnabled = false;
        Windows.Clear();
        Windows.Add(new TimeWindowItem(new TimeSpan(20, 0, 0), new TimeSpan(23, 59, 0)));
        Windows.Add(new TimeWindowItem(new TimeSpan(0, 0, 0), new TimeSpan(5, 0, 0)));
    }

    [RelayCommand]
    private void PresetAllDay()
    {
        SunriseEnabled = false;
        SunsetEnabled = false;
        Windows.Clear();
        Windows.Add(new TimeWindowItem(new TimeSpan(0, 0, 0), new TimeSpan(23, 59, 0)));
    }
}
