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

using System.Reflection;

namespace QuailTracker.Analyzer.Shared;

/// <summary>
/// Application identity (name and version) sourced from assembly metadata so the
/// version lives in exactly one place — <c>Directory.Build.props</c>. Uses
/// Apple-style calendar versioning (major tracks the year, e.g. "26.0.0").
/// </summary>
public static class AppInfo
{
    public const string Name = "QuailTracker Analyzer";

    /// <summary>Version string, e.g. "26.0.0" (any build metadata after '+' is stripped).</summary>
    public static string Version { get; } = ResolveVersion();

    /// <summary>Display title, e.g. "QuailTracker Analyzer v26.0.0".</summary>
    public static string Title => $"{Name} v{Version}";

    private static string ResolveVersion()
    {
        var info = typeof(AppInfo).Assembly
            .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
        if (string.IsNullOrEmpty(info))
            info = typeof(AppInfo).Assembly.GetName().Version?.ToString() ?? "0.0.0";

        var plus = info.IndexOf('+');
        return plus >= 0 ? info[..plus] : info;
    }
}
