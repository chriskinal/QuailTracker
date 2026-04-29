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

using CommunityToolkit.Mvvm.ComponentModel;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Shared ephemeral application state. Observable so multiple ViewModels
/// can react to changes without coupling to each other.
///
/// Single-writer rule (per Plans/threading_model.md): the Apply* methods are
/// the only sanctioned writers of model-load state. Call them on the UI
/// thread (typically inside the await continuation of a load command).
/// </summary>
public partial class AppStateService : ObservableObject
{
    [ObservableProperty]
    private bool _isModelLoaded;

    [ObservableProperty]
    private string _modelPath = string.Empty;

    public void ApplyModelLoaded(string path)
    {
        ModelPath = path;
        IsModelLoaded = true;
    }

    public void ApplyModelUnloaded()
    {
        IsModelLoaded = false;
        ModelPath = string.Empty;
    }
}
