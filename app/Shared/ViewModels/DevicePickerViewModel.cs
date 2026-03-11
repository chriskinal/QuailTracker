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
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Shared.Services;

namespace QuailTracker.Shared.ViewModels;

public partial class DevicePickerViewModel : ObservableObject
{
    private readonly IBluetoothService _bluetoothService;

    [ObservableProperty] private bool _isScanning;
    [ObservableProperty] private string _scanStatus = "Tap Scan to find devices";
    [ObservableProperty] private string _scanButtonText = "Scan";

    public ObservableCollection<DiscoveredDeviceItem> Devices { get; } = new();

    public DevicePickerViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _bluetoothService.DeviceDiscovered += OnDeviceDiscovered;
    }

    private void OnDeviceDiscovered(object? sender, DiscoveredDevice device)
    {
        // Plugin.BLE fires from a background thread; ObservableCollection must be
        // modified on the UI thread or iOS silently drops the update.
        Dispatcher.UIThread.Post(() =>
        {
            var existing = Devices.FirstOrDefault(d => d.Id == device.Id);
            if (existing != null)
            {
                existing.UpdateFrom(device);
            }
            else
            {
                var item = new DiscoveredDeviceItem(device);
                var index = 0;
                while (index < Devices.Count && Devices[index].Rssi > device.Rssi)
                    index++;
                Devices.Insert(index, item);
            }

            ScanStatus = $"Scanning... ({Devices.Count} found)";
        });
    }

    [RelayCommand]
    public async Task ScanAsync()
    {
        if (IsScanning)
        {
            _bluetoothService.StopScan();
            return;
        }

        Devices.Clear();
        IsScanning = true;
        ScanButtonText = "Stop Scan";
        ScanStatus = "Scanning...";

        await _bluetoothService.StartScanAsync();

        IsScanning = false;
        ScanButtonText = "Rescan";
        ScanStatus = Devices.Count > 0
            ? $"Scan complete ({Devices.Count} found)"
            : "No devices found";
    }

    [RelayCommand]
    private async Task SelectDeviceAsync(DiscoveredDeviceItem? item)
    {
        if (item == null) return;

        _bluetoothService.StopScan();
        IsScanning = false;
        ScanStatus = $"Connecting to {item.Name}...";

        var device = new DiscoveredDevice(item.Id, item.Name, item.Rssi, item.LastSeen);
        await _bluetoothService.ConnectToDeviceAsync(device);
    }

    [RelayCommand]
    private void Cancel()
    {
        _bluetoothService.StopScan();
        IsScanning = false;
        ScanButtonText = "Scan";
        Devices.Clear();
        ScanStatus = "Tap Scan to find devices";
    }
}

public partial class DiscoveredDeviceItem : ObservableObject
{
    public Guid Id { get; }

    [ObservableProperty] private string _name;
    [ObservableProperty] private int _rssi;
    [ObservableProperty] private DateTimeOffset _lastSeen;

    private static readonly string[] DefaultNames = ["AI-Thinker", "HMSoft", "PB-03F", "Unknown"];
    public bool IsDefaultName => DefaultNames.Contains(Name);
    public double NameOpacity => IsDefaultName ? 0.5 : 1.0;

    partial void OnNameChanged(string value)
    {
        OnPropertyChanged(nameof(IsDefaultName));
        OnPropertyChanged(nameof(NameOpacity));
    }

    public string SignalColor => Rssi switch
    {
        > -60 => "#4CAF50",  // green — strong
        > -75 => "#FF9800",  // orange — medium
        _ => "#F44336",      // red — weak
    };

    public string RssiText => $"{Rssi} dBm";

    public void UpdateFrom(DiscoveredDevice device)
    {
        Rssi = device.Rssi;
        LastSeen = device.LastSeen;
        // Update name if we got a better one (non-default replaces default)
        if (!DefaultNames.Contains(device.Name) && device.Name != Name)
            Name = device.Name;
    }

    public DiscoveredDeviceItem(DiscoveredDevice device)
    {
        Id = device.Id;
        _name = device.Name;
        _rssi = device.Rssi;
        _lastSeen = device.LastSeen;
    }
}
