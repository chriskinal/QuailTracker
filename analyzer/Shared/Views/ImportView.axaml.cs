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

using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Platform.Storage;
using QuailTracker.Analyzer.Shared.ViewModels;

namespace QuailTracker.Analyzer.Shared.Views;

public partial class ImportView : UserControl
{
    public ImportView()
    {
        InitializeComponent();

        AddHandler(DragDrop.DropEvent, OnDrop);
        AddHandler(DragDrop.DragOverEvent, OnDragOver);
    }

    private void OnDragOver(object? sender, DragEventArgs e)
    {
        e.DragEffects = e.Data.Contains(DataFormats.Files)
            ? DragDropEffects.Copy
            : DragDropEffects.None;
    }

    private async void OnDrop(object? sender, DragEventArgs e)
    {
        if (DataContext is not ImportViewModel vm) return;

        if (e.Data.Contains(DataFormats.Files))
        {
            var files = e.Data.GetFiles();
            if (files == null) return;

            var wavFiles = new List<string>();

            foreach (var item in files)
            {
                if (item is IStorageFile file && file.Name.EndsWith(".wav", System.StringComparison.OrdinalIgnoreCase))
                {
                    wavFiles.Add(file.Path.LocalPath);
                }
                else if (item is IStorageFolder folder)
                {
                    await vm.ImportFolderCommand.ExecuteAsync(folder.Path.LocalPath);
                    return;
                }
            }

            if (wavFiles.Count > 0)
            {
                await vm.ImportFilesCommand.ExecuteAsync(wavFiles);
            }
        }
    }

    private async void OnImportFilesClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (DataContext is not ImportViewModel vm) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select WAV Files",
            AllowMultiple = true,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("WAV Audio Files") { Patterns = new[] { "*.wav" } }
            }
        });

        if (files.Count > 0)
        {
            await vm.ImportFilesCommand.ExecuteAsync(files.Select(f => f.Path.LocalPath));
        }
    }

    private async void OnImportFolderClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (DataContext is not ImportViewModel vm) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var folders = await topLevel.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "Select Folder with WAV Files",
            AllowMultiple = false
        });

        if (folders.Count > 0)
        {
            await vm.ImportFolderCommand.ExecuteAsync(folders[0].Path.LocalPath);
        }
    }
}
