/*
 * QuailTracker - Localization engine: local ENU ↔ WGS84 projection
 * Copyright (C) 2026 QuailTracker Project
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Acoustics;

/// <summary>
/// Equirectangular projection between WGS84 lat/lon and a local east(X)/north(Y) metre
/// frame centred on a reference point. Accurate to well under a metre over a survey-area-
/// sized region (≈1 km), which is all the planner needs.
/// </summary>
public sealed class GeoProjection
{
    const double DegToRad = Math.PI / 180.0;
    const double MetersPerDegLat = 111_320.0;

    readonly double _metersPerDegLon;

    public double CenterLat { get; }
    public double CenterLon { get; }

    public GeoProjection(double centerLat, double centerLon)
    {
        CenterLat = centerLat;
        CenterLon = centerLon;
        _metersPerDegLon = MetersPerDegLat * Math.Cos(centerLat * DegToRad);
    }

    /// <summary>WGS84 lat/lon → local (east X, north Y) metres.</summary>
    public (double X, double Y) ToLocal(double lat, double lon)
        => ((lon - CenterLon) * _metersPerDegLon, (lat - CenterLat) * MetersPerDegLat);

    /// <summary>Local (east X, north Y) metres → WGS84 lat/lon.</summary>
    public (double Lat, double Lon) ToGeo(double x, double y)
        => (CenterLat + y / MetersPerDegLat, CenterLon + x / _metersPerDegLon);
}
