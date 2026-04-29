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

using System.Threading;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Cancel-and-replace helper for fire-and-forget jobs that should be
/// superseded by subsequent invocations (per Plans/threading_model.md):
/// cancel the old token, dispose it, return a fresh one.
///
/// Intended to be called only from the UI thread — no internal locking.
/// </summary>
public static class CancellationHelpers
{
    public static CancellationToken Replace(ref CancellationTokenSource? cts)
    {
        var old = cts;
        cts = new CancellationTokenSource();
        old?.Cancel();
        old?.Dispose();
        return cts.Token;
    }
}
