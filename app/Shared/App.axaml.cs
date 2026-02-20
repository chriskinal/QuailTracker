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
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using QuailTracker.Shared.Services;
using QuailTracker.Shared.ViewModels;
using QuailTracker.Shared.Views;

namespace QuailTracker.Shared;

public partial class App : Application
{
    public static IBluetoothService? BluetoothServiceOverride { get; set; }

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        var btService = BluetoothServiceOverride ?? CreateBluetoothService();
        var viewModel = new MainWindowViewModel(btService);

        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = new MainWindow
            {
                DataContext = viewModel
            };
        }
        else if (ApplicationLifetime is ISingleViewApplicationLifetime singleView)
        {
            singleView.MainView = new MainView
            {
                DataContext = viewModel
            };
        }

        base.OnFrameworkInitializationCompleted();
    }

    private static IBluetoothService CreateBluetoothService()
    {
        try
        {
            // Try real BLE — works on Android, iOS, and macOS (Core Bluetooth)
            var _ = Plugin.BLE.CrossBluetoothLE.Current;
            System.Console.WriteLine($"[BLE] Using real BluetoothService (state: {_.State})");
            return new BluetoothService();
        }
        catch (Exception ex)
        {
            // BLE not available on this platform — fall back to mock
            System.Console.WriteLine($"[BLE] Plugin.BLE not available: {ex.Message} — using MockBluetoothService");
            return new MockBluetoothService();
        }
    }
}
