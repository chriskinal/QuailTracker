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
using System.IO;
using System.Linq;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Read the local training data directory to enumerate species the user
/// already has clips for. Used to populate the xeno-canto species picker so
/// the user picks from existing names instead of risking typos that create
/// near-duplicate directories (e.g. "Bobwhite" vs "Northern Bobwhite").
///
/// Returns an empty list on shipped builds where the training/ directory
/// isn't on disk — the picker still allows free-text entry in that case.
/// </summary>
public static class SpeciesCatalog
{
    public static IReadOnlyList<string> LoadKnownSpecies()
    {
        var trainingDir = TrainingDirectoryLocator.Find();
        if (trainingDir is null) return Array.Empty<string>();

        var clipsDir = Path.Combine(trainingDir, "data", "clips");
        if (!Directory.Exists(clipsDir)) return Array.Empty<string>();

        var set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);

        var labelsFile = Path.Combine(clipsDir, "labels.txt");
        if (File.Exists(labelsFile))
        {
            try
            {
                foreach (var line in File.ReadAllLines(labelsFile))
                {
                    var name = line.Trim();
                    if (name.Length > 0 && !name.Equals("noise", StringComparison.OrdinalIgnoreCase))
                        set.Add(name);
                }
            }
            catch { /* fall through to subdir scan */ }
        }

        try
        {
            foreach (var dir in Directory.EnumerateDirectories(clipsDir))
            {
                var name = Path.GetFileName(dir);
                if (string.IsNullOrEmpty(name)) continue;
                if (name.Equals("noise", StringComparison.OrdinalIgnoreCase)) continue;
                set.Add(name);
            }
        }
        catch { /* ignore */ }

        return set.ToList();
    }
}
