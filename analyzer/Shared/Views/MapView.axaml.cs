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

using Avalonia.Controls;
using Avalonia.Platform.Storage;
using QuailTracker.Analyzer.Shared.ViewModels;

namespace QuailTracker.Analyzer.Shared.Views;

public partial class MapView : UserControl
{
    private bool _mapInitialized;

    public MapView()
    {
        InitializeComponent();
    }

    protected override async void OnDataContextChanged(System.EventArgs e)
    {
        base.OnDataContextChanged(e);

        if (_mapInitialized) return;
        if (DataContext is not MapViewModel vm) return;

        _mapInitialized = true;
        await vm.InitializeMapAsync(MapWebView);
    }

    private async void OnExportKmlClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (DataContext is not MapViewModel vm) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var file = await topLevel.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Export to KML",
            DefaultExtension = "kml",
            SuggestedFileName = "QuailTracker_Export.kml",
            FileTypeChoices = new[]
            {
                new FilePickerFileType("KML File") { Patterns = new[] { "*.kml" } }
            }
        });

        if (file != null)
        {
            await vm.ExportKmlCommand.ExecuteAsync(file.Path.LocalPath);
        }
    }
}
