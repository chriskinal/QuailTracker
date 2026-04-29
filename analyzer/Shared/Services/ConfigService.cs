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
using System.IO;
using System.Text.Json;
using System.Threading.Tasks;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Persistent application configuration. Saved to disk and restored on startup.
/// </summary>
public class ConfigService
{
    private static readonly string ConfigDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "QuailTracker");

    private static readonly string ConfigFile = Path.Combine(ConfigDir, "analyzer_config.json");

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    public string? BirdNetModelPath { get; set; }

    // Window bounds — null means use XAML defaults
    public double? WindowX { get; set; }
    public double? WindowY { get; set; }
    public double? WindowWidth { get; set; }
    public double? WindowHeight { get; set; }
    public bool IsMaximized { get; set; }

    /// <summary>
    /// Synchronous load. Used at cold startup from the
    /// <see cref="ViewModels.MainWindowViewModel"/> default constructor where
    /// async would force restructuring app bootstrap. Small JSON, runs once.
    /// </summary>
    public static ConfigService Load()
    {
        try
        {
            if (File.Exists(ConfigFile))
            {
                var json = File.ReadAllText(ConfigFile);
                return JsonSerializer.Deserialize<ConfigService>(json, JsonOptions) ?? new ConfigService();
            }
        }
        catch
        {
            // Corrupted config — start fresh
        }
        return new ConfigService();
    }

    /// <summary>
    /// Synchronous save. Used by <c>MainWindow.OnClosing</c> where awaiting
    /// inside the closing event handler is awkward. Small JSON; the brief
    /// blocking write at app shutdown is acceptable.
    /// </summary>
    public void Save()
    {
        try
        {
            Directory.CreateDirectory(ConfigDir);
            var json = JsonSerializer.Serialize(this, JsonOptions);
            File.WriteAllText(ConfigFile, json);
        }
        catch
        {
            // Non-critical — settings just won't persist this session
        }
    }

    /// <summary>
    /// Async save. Preferred from any <c>async</c> command path — keeps file
    /// I/O off the UI thread.
    /// </summary>
    public async Task SaveAsync()
    {
        try
        {
            Directory.CreateDirectory(ConfigDir);
            var json = JsonSerializer.Serialize(this, JsonOptions);
            await File.WriteAllTextAsync(ConfigFile, json);
        }
        catch
        {
            // Non-critical — settings just won't persist this session
        }
    }
}
