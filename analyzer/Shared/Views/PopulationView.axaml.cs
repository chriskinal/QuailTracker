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

using System.Linq;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using QuailTracker.Analyzer.Shared.ViewModels;

namespace QuailTracker.Analyzer.Shared.Views;

public partial class PopulationView : UserControl
{
    public PopulationView()
    {
        InitializeComponent();
    }

    private async void OnExportClick(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not PopulationViewModel vm) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var file = await topLevel.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Export Population Estimate",
            DefaultExtension = "json",
            SuggestedFileName = "population_estimate",
            FileTypeChoices =
            [
                new FilePickerFileType("JSON") { Patterns = ["*.json"] }
            ]
        });

        if (file != null)
        {
            vm.ExportResults(file.Path.LocalPath);
        }
    }

    private async void OnImportClick(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not PopulationViewModel vm) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Import Previous Population Estimate",
            AllowMultiple = false,
            FileTypeFilter =
            [
                new FilePickerFileType("JSON") { Patterns = ["*.json"] }
            ]
        });

        var file = files.FirstOrDefault();
        if (file != null)
        {
            vm.ImportPrevious(file.Path.LocalPath);
        }
    }
}
