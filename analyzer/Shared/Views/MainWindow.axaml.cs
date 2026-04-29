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
using Avalonia.Controls;
using QuailTracker.Analyzer.Shared.ViewModels;

namespace QuailTracker.Analyzer.Shared.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();

        Opened += OnOpened;
        Closing += OnClosing;
    }

    private void OnOpened(object? sender, EventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm) return;

        var saved = vm.GetSavedWindowState();
        if (saved is null) return;

        Width = saved.Value.Width;
        Height = saved.Value.Height;
        Position = new Avalonia.PixelPoint((int)saved.Value.X, (int)saved.Value.Y);

        if (saved.Value.IsMaximized)
            WindowState = WindowState.Maximized;
    }

    private void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm) return;

        vm.SaveWindowState(new MainWindowViewModel.WindowState(
            X: Position.X,
            Y: Position.Y,
            Width: Width,
            Height: Height,
            IsMaximized: WindowState == WindowState.Maximized));
    }
}
