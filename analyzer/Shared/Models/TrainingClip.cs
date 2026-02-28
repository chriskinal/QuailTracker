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
using CommunityToolkit.Mvvm.ComponentModel;

namespace QuailTracker.Analyzer.Shared.Models;

public enum TrainingClipStatus
{
    Pending,
    Confirmed,
    Rejected
}

public partial class TrainingClip : ObservableObject
{
    public Detection Detection { get; init; } = null!;

    [ObservableProperty]
    private TrainingClipStatus _status = TrainingClipStatus.Pending;

    // Convenience forwarding properties for grid binding
    public string CommonName => Detection.CommonName;
    public string ScientificName => Detection.ScientificName;
    public double Confidence => Detection.Confidence;
    public string ConfidencePercent => Detection.ConfidencePercent;
    public string AudioFilePath => Detection.AudioFilePath;
    public double OffsetSeconds => Detection.OffsetSeconds;
    public double DurationSeconds => Detection.DurationSeconds;
    public string StationId => Detection.StationId;
    public DateTime Timestamp => Detection.Timestamp;

    public string StatusDisplay => Status switch
    {
        TrainingClipStatus.Confirmed => "Confirmed",
        TrainingClipStatus.Rejected => "Rejected",
        _ => "Pending"
    };

    partial void OnStatusChanged(TrainingClipStatus value)
    {
        OnPropertyChanged(nameof(StatusDisplay));
    }
}
