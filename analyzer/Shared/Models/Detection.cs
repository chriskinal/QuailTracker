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

/// <summary>
/// Represents a BirdNet species detection result from audio analysis.
///
/// Implements <see cref="System.ComponentModel.INotifyPropertyChanged"/> via
/// <see cref="ObservableObject"/> so that services mutating BearingDeg /
/// TdoaSamples / TdoaConfidence / LocalizationId / IsSelected after the
/// detection is already in a bound collection produce automatic UI refresh.
/// (Without this, the DataGrid only reads new values on row re-render —
/// the "scroll to see bearings" bug.)
/// </summary>
public partial class Detection : ObservableObject
{
    [ObservableProperty]
    private Guid _id = Guid.NewGuid();

    [ObservableProperty]
    private string _audioFilePath = string.Empty;

    [ObservableProperty]
    private string _stationId = string.Empty;

    /// <summary>GPS-synchronized timestamp of the detection.</summary>
    [ObservableProperty]
    private DateTime _timestamp;

    /// <summary>Offset from the start of the audio file in seconds.</summary>
    [ObservableProperty]
    private double _offsetSeconds;

    /// <summary>Duration of the detection segment in seconds (typically 3s for BirdNet).</summary>
    [ObservableProperty]
    private double _durationSeconds = 3.0;

    /// <summary>Scientific name of the detected species.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(DisplayName))]
    private string _scientificName = string.Empty;

    /// <summary>Common name of the detected species.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(DisplayName))]
    private string _commonName = string.Empty;

    /// <summary>BirdNet confidence score (0.0 to 1.0).</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ConfidencePercent))]
    private double _confidence;

    /// <summary>Stereo TDOA bearing in degrees (0=forward, 90=right, -90=left). NaN if not computed.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(BearingDisplay))]
    private double _bearingDeg = double.NaN;

    /// <summary>TDOA in samples between L and R channels. Positive = sound arrives at R first.</summary>
    [ObservableProperty]
    private int _tdoaSamples;

    /// <summary>Cross-correlation peak value (0-1). Higher = more confident bearing.</summary>
    [ObservableProperty]
    private double _tdoaConfidence;

    /// <summary>Whether this detection has been selected for TDOA localization.</summary>
    [ObservableProperty]
    private bool _isSelected;

    /// <summary>Link to a localization result if this detection was localized.</summary>
    [ObservableProperty]
    private Guid? _localizationId;

    public string DisplayName => string.IsNullOrEmpty(CommonName) ? ScientificName : CommonName;
    public string ConfidencePercent => $"{Confidence:P0}";
    public string BearingDisplay => double.IsNaN(BearingDeg) ? "--" : $"{BearingDeg:F1}°";

    public override string ToString() =>
        $"{DisplayName} ({ConfidencePercent}) at {Timestamp:HH:mm:ss}";
}

/// <summary>
/// Common bird species constants for filtering.
/// </summary>
public static class TargetSpecies
{
    public const string NorthernBobwhite = "Colinus virginianus";
    public const string ScaledQuail = "Callipepla squamata";
    public const string GambelQuail = "Callipepla gambelii";
    public const string CaliforniaQuail = "Callipepla californica";
    public const string MontezumaQuail = "Cyrtonyx montezumae";

    public static readonly string[] QuailSpecies =
    [
        NorthernBobwhite,
        ScaledQuail,
        GambelQuail,
        CaliforniaQuail,
        MontezumaQuail
    ];
}
