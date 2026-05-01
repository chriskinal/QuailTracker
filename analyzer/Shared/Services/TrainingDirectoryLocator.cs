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

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Walks up from the running assembly's BaseDirectory looking for a
/// sibling/ancestor `training/` directory containing `docker-compose.yml`.
/// Used by the "Start Container" button to know where to invoke docker.
/// Returns null on shipped builds where the repo isn't present.
/// </summary>
public static class TrainingDirectoryLocator
{
    public static string? Find()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, "training");
            if (Directory.Exists(candidate)
                && File.Exists(Path.Combine(candidate, "docker-compose.yml")))
            {
                return candidate;
            }
            dir = dir.Parent;
        }
        return null;
    }
}
