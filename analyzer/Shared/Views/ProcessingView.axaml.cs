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
using System.Collections.Generic;
using System.Linq;
using Avalonia.Collections;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using QuailTracker.Analyzer.Shared.Services;
using QuailTracker.Analyzer.Shared.ViewModels;

namespace QuailTracker.Analyzer.Shared.Views;

public partial class ProcessingView : UserControl
{
    public ProcessingView()
    {
        InitializeComponent();
    }

    private async void OnLoadModelClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (DataContext is not ProcessingViewModel vm) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select BirdNet ONNX Model",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("ONNX Model") { Patterns = new[] { "*.onnx" } }
            }
        });

        if (files.Count > 0)
        {
            await vm.LoadModelCommand.ExecuteAsync(files[0].Path.LocalPath);
        }
    }

    // Tracks group instances we've already collapsed once, so virtualized
    // re-loads of the same header don't override the user's expand action.
    // Re-grouping (filter/toggle changes) creates new group instances, so this
    // set self-resets without explicit clearing.
    private readonly HashSet<DataGridCollectionViewGroup> _initiallyCollapsed = new();

    private void OnLoadingRowGroup(object? sender, DataGridRowGroupHeaderEventArgs e)
    {
        // CollapseRowGroup must NOT run synchronously here — it triggers focus
        // changes + edit commits that throw "Items cannot be added… while rows
        // are loading". Defer past the current layout pass.
        if (sender is not DataGrid grid) return;
        if (e.RowGroupHeader.DataContext is not DataGridCollectionViewGroup group) return;
        if (!_initiallyCollapsed.Add(group)) return;

        Dispatcher.UIThread.Post(
            () => grid.CollapseRowGroup(group, false),
            DispatcherPriority.Background);
    }

    private async void OnExportClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (DataContext is not ProcessingViewModel vm) return;
        if (vm.FilteredDetections.Count == 0) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var file = await topLevel.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Export Detections",
            SuggestedFileName = "detections_export.csv",
            FileTypeChoices = new[]
            {
                new FilePickerFileType("BirdNET CSV") { Patterns = new[] { "*.csv" } },
                new FilePickerFileType("Raven Selection Table") { Patterns = new[] { "*.txt" } }
            }
        });

        if (file == null) return;

        await vm.ExportDetectionsCommand.ExecuteAsync(file.Path.LocalPath);
    }
}
