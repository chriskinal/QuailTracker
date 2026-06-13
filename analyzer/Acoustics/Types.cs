/*
 * QuailTracker - Localization engine: core types
 * Copyright (C) 2026 QuailTracker Project
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Acoustics;

/// <summary>Localization method whose error is being modelled.</summary>
public enum LocalizationMethod
{
    /// <summary>Time-of-arrival across PPS-synced stations (no front/back ambiguity).</summary>
    Tdoa,
    /// <summary>Stereo direction-of-arrival only (front hemisphere, ±90°).</summary>
    Bearing,
    /// <summary>TDOA + stereo bearing combined.</summary>
    Fusion,
}

/// <summary>
/// A recording station in local east/north metres relative to a survey-area origin.
/// <paramref name="HeadingDeg"/> is the compass bearing (0°=north, 90°=east) the
/// stereo mic faces; only used by <see cref="LocalizationMethod.Bearing"/>/<see cref="LocalizationMethod.Fusion"/>.
/// </summary>
public readonly record struct ArrayStation(double X, double Y, double HeadingDeg);

/// <summary>Aggregate localization quality over a survey area for one layout + method.</summary>
/// <param name="Coverage">Fraction of area points whose 1σ error is within the target.</param>
/// <param name="FixCount">Number of area points that produced a fix at all.</param>
/// <param name="MedianError">Median 1σ position error (m) over points with a fix.</param>
public readonly record struct AreaResult(double Coverage, int FixCount, double MedianError);

/// <summary>
/// Physical/noise assumptions feeding the Cramér-Rao error model. Stored in SI
/// (seconds, radians, metres); use <see cref="FromUnits"/> to build from ms/degrees.
/// </summary>
public sealed record LocalizationParams
{
    /// <summary>Arrival-time std, seconds (timing precision of cross-correlation).</summary>
    public double SigmaT { get; init; } = 1.0 / 1000.0;

    /// <summary>Stereo bearing std, radians.</summary>
    public double SigmaTheta { get; init; } = 10.0 * Math.PI / 180.0;

    /// <summary>Speed of sound, m/s.</summary>
    public double SpeedOfSound { get; init; } = 343.0;

    /// <summary>Call audibility radius, metres — beyond this a station can't hear the call.</summary>
    public double DetectRadius { get; init; } = 500.0;

    /// <summary>Acceptable 1σ position error, metres (defines "covered").</summary>
    public double TargetError { get; init; } = 50.0;

    /// <summary>Build params from human units: timing std in ms, bearing std in degrees.</summary>
    public static LocalizationParams FromUnits(
        double sigmaTms, double sigmaDeg,
        double speedOfSound = 343.0, double detectRadius = 500.0, double targetError = 50.0)
        => new()
        {
            SigmaT = sigmaTms / 1000.0,
            SigmaTheta = sigmaDeg * Math.PI / 180.0,
            SpeedOfSound = speedOfSound,
            DetectRadius = detectRadius,
            TargetError = targetError,
        };
}
