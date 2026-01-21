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
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;
using SharpKml.Base;
using SharpKml.Dom;
using SharpKml.Engine;

namespace QuailTracker.Analyzer.Shared.Services;

/// <summary>
/// Service for exporting data to KML format for Google Earth.
/// </summary>
public class KmlExportService : IKmlExportService
{
    public async Task ExportAsync(
        string filePath,
        IReadOnlyList<Station> stations,
        IReadOnlyList<Detection> detections,
        IReadOnlyList<Localization> localizations,
        KmlExportOptions? options = null,
        CancellationToken ct = default)
    {
        options ??= new KmlExportOptions();

        await Task.Run(() =>
        {
            ct.ThrowIfCancellationRequested();

            var document = new Document
            {
                Name = options.DocumentName,
                Description = new Description { Text = options.DocumentDescription ?? "QuailTracker Analysis Export" }
            };

            // Define styles
            AddStyles(document);

            // Add stations
            if (options.IncludeStations && stations.Count > 0)
            {
                var stationsFolder = new Folder { Name = "Stations" };
                foreach (var station in stations)
                {
                    stationsFolder.AddFeature(CreateStationPlacemark(station));
                }
                document.AddFeature(stationsFolder);
            }

            // Add detections
            if (options.IncludeDetections && detections.Count > 0)
            {
                var detectionsFolder = new Folder { Name = "Detections" };
                foreach (var detection in detections)
                {
                    var stationLoc = FindStationLocation(detection.StationId, stations);
                    if (stationLoc.HasValue)
                    {
                        detectionsFolder.AddFeature(CreateDetectionPlacemark(detection, stationLoc.Value));
                    }
                }
                document.AddFeature(detectionsFolder);
            }

            // Add localizations
            if (options.IncludeLocalizations && localizations.Count > 0)
            {
                var localizationsFolder = new Folder { Name = "Localizations" };
                foreach (var localization in localizations)
                {
                    localizationsFolder.AddFeature(CreateLocalizationPlacemark(localization, options.IncludeConfidenceEllipses));
                }
                document.AddFeature(localizationsFolder);
            }

            var kml = new Kml { Feature = document };
            var serializer = new Serializer();
            serializer.Serialize(kml);

            File.WriteAllText(filePath, serializer.Xml);
        }, ct);
    }

    public async Task ExportStationsAsync(
        string filePath,
        IReadOnlyList<Station> stations,
        CancellationToken ct = default)
    {
        await ExportAsync(filePath, stations, Array.Empty<Detection>(), Array.Empty<Localization>(),
            new KmlExportOptions
            {
                IncludeStations = true,
                IncludeDetections = false,
                IncludeLocalizations = false,
                DocumentName = "QuailTracker Stations"
            }, ct);
    }

    public async Task ExportLocalizationsAsync(
        string filePath,
        IReadOnlyList<Localization> localizations,
        bool includeEllipses = true,
        CancellationToken ct = default)
    {
        await ExportAsync(filePath, Array.Empty<Station>(), Array.Empty<Detection>(), localizations,
            new KmlExportOptions
            {
                IncludeStations = false,
                IncludeDetections = false,
                IncludeLocalizations = true,
                IncludeConfidenceEllipses = includeEllipses,
                DocumentName = "QuailTracker Localizations"
            }, ct);
    }

    private static void AddStyles(Document document)
    {
        // Station style
        var stationStyle = new Style
        {
            Id = "stationStyle",
            Icon = new IconStyle
            {
                Icon = new IconStyle.IconLink(new Uri("http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png")),
                Color = new Color32(255, 0, 128, 255), // Blue
                Scale = 1.2
            }
        };
        document.AddStyle(stationStyle);

        // Detection style
        var detectionStyle = new Style
        {
            Id = "detectionStyle",
            Icon = new IconStyle
            {
                Icon = new IconStyle.IconLink(new Uri("http://maps.google.com/mapfiles/kml/shapes/shaded_dot.png")),
                Color = new Color32(255, 0, 255, 255), // Yellow
                Scale = 0.8
            }
        };
        document.AddStyle(detectionStyle);

        // Localization style
        var localizationStyle = new Style
        {
            Id = "localizationStyle",
            Icon = new IconStyle
            {
                Icon = new IconStyle.IconLink(new Uri("http://maps.google.com/mapfiles/kml/shapes/target.png")),
                Color = new Color32(255, 0, 0, 255), // Red
                Scale = 1.0
            },
            Polygon = new PolygonStyle
            {
                Color = new Color32(64, 0, 0, 255), // Semi-transparent red
                Outline = true
            },
            Line = new LineStyle
            {
                Color = new Color32(255, 0, 0, 255),
                Width = 2
            }
        };
        document.AddStyle(localizationStyle);
    }

    private static Placemark CreateStationPlacemark(Station station)
    {
        return new Placemark
        {
            Name = string.IsNullOrEmpty(station.Name) ? station.Id : station.Name,
            Description = new Description
            {
                Text = $"""
                    <![CDATA[
                    <b>Station ID:</b> {station.Id}<br/>
                    <b>Location:</b> {station.Latitude:F6}, {station.Longitude:F6}<br/>
                    <b>Recordings:</b> {station.RecordingCount}
                    ]]>
                    """
            },
            StyleUrl = new Uri("#stationStyle", UriKind.Relative),
            Geometry = new Point
            {
                Coordinate = new Vector(station.Latitude, station.Longitude, station.Elevation ?? 0)
            }
        };
    }

    private static Placemark CreateDetectionPlacemark(Detection detection, (double lat, double lon) location)
    {
        return new Placemark
        {
            Name = detection.DisplayName,
            Description = new Description
            {
                Text = $"""
                    <![CDATA[
                    <b>Species:</b> {detection.CommonName}<br/>
                    <b>Scientific Name:</b> {detection.ScientificName}<br/>
                    <b>Confidence:</b> {detection.Confidence:P1}<br/>
                    <b>Station:</b> {detection.StationId}<br/>
                    <b>Time:</b> {detection.Timestamp:yyyy-MM-dd HH:mm:ss}
                    ]]>
                    """
            },
            StyleUrl = new Uri("#detectionStyle", UriKind.Relative),
            Geometry = new Point
            {
                Coordinate = new Vector(location.lat, location.lon)
            },
            Time = new Timestamp { When = detection.Timestamp }
        };
    }

    private static Placemark CreateLocalizationPlacemark(Localization localization, bool includeEllipse)
    {
        var placemark = new Placemark
        {
            Name = $"{localization.Species} ({localization.QualityLabel})",
            Description = new Description
            {
                Text = $"""
                    <![CDATA[
                    <b>Species:</b> {localization.Species}<br/>
                    <b>Location:</b> {localization.CoordinateString}<br/>
                    <b>Quality:</b> {localization.QualityLabel} ({localization.QualityScore:P0})<br/>
                    <b>Error (major):</b> {localization.ErrorEllipseMajor:F1}m<br/>
                    <b>Error (minor):</b> {localization.ErrorEllipseMinor:F1}m<br/>
                    <b>Stations:</b> {string.Join(", ", localization.StationIds)}<br/>
                    <b>Time:</b> {localization.Timestamp:yyyy-MM-dd HH:mm:ss}
                    ]]>
                    """
            },
            StyleUrl = new Uri("#localizationStyle", UriKind.Relative),
            Time = new Timestamp { When = localization.Timestamp }
        };

        if (includeEllipse && localization.ErrorEllipseMajor > 0)
        {
            // Create ellipse as polygon
            var ellipseCoords = GenerateEllipseCoordinates(
                localization.Latitude,
                localization.Longitude,
                localization.ErrorEllipseMajor,
                localization.ErrorEllipseMinor,
                localization.ErrorEllipseRotation);

            var multiGeometry = new MultipleGeometry();
            multiGeometry.AddGeometry(new Point
            {
                Coordinate = new Vector(localization.Latitude, localization.Longitude)
            });

            var ring = new LinearRing();
            ring.Coordinates = ellipseCoords;

            multiGeometry.AddGeometry(new Polygon
            {
                OuterBoundary = new OuterBoundary { LinearRing = ring }
            });

            placemark.Geometry = multiGeometry;
        }
        else
        {
            placemark.Geometry = new Point
            {
                Coordinate = new Vector(localization.Latitude, localization.Longitude)
            };
        }

        return placemark;
    }

    private static CoordinateCollection GenerateEllipseCoordinates(
        double centerLat, double centerLon,
        double semiMajorMeters, double semiMinorMeters,
        double rotationDegrees)
    {
        const int points = 36;
        const double earthRadius = 6371000;

        var coords = new CoordinateCollection();
        var rotationRad = rotationDegrees * Math.PI / 180;

        for (var i = 0; i <= points; i++)
        {
            var angle = 2 * Math.PI * i / points;

            // Ellipse point in local coordinates
            var x = semiMajorMeters * Math.Cos(angle);
            var y = semiMinorMeters * Math.Sin(angle);

            // Rotate
            var xRot = x * Math.Cos(rotationRad) - y * Math.Sin(rotationRad);
            var yRot = x * Math.Sin(rotationRad) + y * Math.Cos(rotationRad);

            // Convert to lat/lon offset
            var latOffset = yRot / earthRadius * (180 / Math.PI);
            var lonOffset = xRot / (earthRadius * Math.Cos(centerLat * Math.PI / 180)) * (180 / Math.PI);

            coords.Add(new Vector(centerLat + latOffset, centerLon + lonOffset));
        }

        return coords;
    }

    private static (double lat, double lon)? FindStationLocation(string stationId, IReadOnlyList<Station> stations)
    {
        foreach (var station in stations)
        {
            if (station.Id == stationId && station.HasValidLocation)
            {
                return (station.Latitude, station.Longitude);
            }
        }
        return null;
    }
}
