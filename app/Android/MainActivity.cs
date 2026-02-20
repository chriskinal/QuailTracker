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

using Android;
using Android.App;
using Android.Content.PM;
using Android.OS;
using Avalonia;
using Avalonia.Android;
using QuailTracker.Shared;
using QuailTracker.Shared.Services;

namespace QuailTracker.Android;

[Activity(
    Label = "QuailTracker",
    Theme = "@style/MyTheme.NoActionBar",
    Icon = "@mipmap/appicon",
    MainLauncher = true,
    ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize | ConfigChanges.UiMode)]
public class MainActivity : AvaloniaMainActivity<App>
{
    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        RequestBlePermissions();
    }

    protected override AppBuilder CustomizeAppBuilder(AppBuilder builder)
    {
        App.BluetoothServiceOverride = new BluetoothService();
        return base.CustomizeAppBuilder(builder)
            .WithInterFont();
    }

    private void RequestBlePermissions()
    {
        if (Build.VERSION.SdkInt < BuildVersionCodes.S)
        {
            // Pre-Android 12: location permission needed for BLE scan
            RequestPermissions([
                Manifest.Permission.AccessFineLocation,
                Manifest.Permission.AccessCoarseLocation,
            ], 1);
        }
        else
        {
            // Android 12+: dedicated BLE permissions
            RequestPermissions([
                Manifest.Permission.BluetoothScan,
                Manifest.Permission.BluetoothConnect,
                Manifest.Permission.AccessFineLocation,
            ], 1);
        }
    }
}
