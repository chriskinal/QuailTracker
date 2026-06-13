/*
 * QuailTracker - Localization engine: ring station layout
 * Copyright (C) 2026 QuailTracker Project
 * GNU GPL v3 or later. See <https://www.gnu.org/licenses/>.
 */

namespace QuailTracker.Localization;

/// <summary>Generates candidate station layouts.</summary>
public static class RingLayout
{
    const double RadToDeg = 180.0 / Math.PI;

    /// <summary>
    /// <paramref name="n"/> stations evenly spaced on a ring of radius <paramref name="r"/>
    /// (metres) about the origin, each aimed inward at the centre.
    /// </summary>
    public static Station[] Ring(int n, double r)
    {
        var stns = new Station[n];
        for (var k = 0; k < n; k++)
        {
            double phi = 2 * Math.PI * k / n;
            double x = r * Math.Sin(phi), y = r * Math.Cos(phi);   // east, north
            double heading = (Math.Atan2(-x, -y) * RadToDeg + 360) % 360; // aim at centre
            stns[k] = new Station(x, y, heading);
        }
        return stns;
    }
}
