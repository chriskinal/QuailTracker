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

using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Shared.Models;
using QuailTracker.Shared.Services;

namespace QuailTracker.Shared.ViewModels;

public partial class MainWindowViewModel : ObservableObject
{
    private readonly IBluetoothService _bluetoothService;

    private bool _deviceIsRecording;

    [ObservableProperty]
    private string _connectionStatus = "Not Connected";

    [ObservableProperty]
    private string _deviceName = "No device";

    [ObservableProperty]
    private string _connectButtonText = "Scan";

    [ObservableProperty]
    private bool _isConnected = false;

    [ObservableProperty]
    private int _selectedTabIndex = 0;

    [ObservableProperty]
    private HealthViewModel _healthViewModel;

    [ObservableProperty]
    private OperationsViewModel _operationsViewModel;

    [ObservableProperty]
    private ScheduleViewModel _scheduleViewModel;

    [ObservableProperty]
    private ConfigViewModel _configViewModel;

    public MainWindowViewModel() : this(new MockBluetoothService())
    {
    }

    public MainWindowViewModel(IBluetoothService bluetoothService)
    {
        _bluetoothService = bluetoothService;
        _healthViewModel = new HealthViewModel(bluetoothService);
        _operationsViewModel = new OperationsViewModel(bluetoothService);
        _scheduleViewModel = new ScheduleViewModel(bluetoothService);
        _configViewModel = new ConfigViewModel(bluetoothService);

        _bluetoothService.ConnectionStateChanged += OnConnectionStateChanged;
        _bluetoothService.StatusReceived += OnStatusReceived;
    }

    private void OnStatusReceived(object? sender, DeviceStatus status)
    {
        var wasRecording = _deviceIsRecording;
        _deviceIsRecording = status.IsRecording;

        // Auto-start/stop rec stream when recording state changes
        if (wasRecording != status.IsRecording)
        {
            _ = _bluetoothService.SetStreamRecAsync(status.IsRecording ? 1000 : 0);
        }
    }

    private void OnConnectionStateChanged(object? sender, ConnectionState state)
    {
        IsConnected = state == ConnectionState.Connected;

        ConnectionStatus = state switch
        {
            ConnectionState.Disconnected => "Not Connected",
            ConnectionState.Scanning => "Scanning...",
            ConnectionState.Connecting => "Connecting...",
            ConnectionState.Connected => $"Connected to {_bluetoothService.ConnectedDeviceName}",
            _ => "Unknown"
        };

        ConnectButtonText = state switch
        {
            ConnectionState.Disconnected => "Scan",
            ConnectionState.Scanning => "Stop",
            ConnectionState.Connecting => "Cancel",
            ConnectionState.Connected => "Disconnect",
            _ => "Scan"
        };

        DeviceName = IsConnected
            ? _bluetoothService.ConnectedDeviceName ?? "QuailTracker"
            : "No device";
    }

    [RelayCommand]
    private async Task ConnectAsync()
    {
        if (IsConnected)
        {
            await _bluetoothService.DisconnectAsync();
        }
        else if (_bluetoothService.CurrentState == ConnectionState.Scanning)
        {
            _bluetoothService.StopScan();
        }
        else
        {
            await _bluetoothService.ScanAndConnectAsync();
        }
    }
}
