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

using System.Collections.Generic;
using System.Globalization;
using System.IO;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

public static class DetectionExporter
{
    public static void WriteBirdNetCsv(IList<Detection> detections, string path)
    {
        using var writer = new StreamWriter(path);
        writer.WriteLine("Start (s),End (s),Scientific name,Common name,Confidence,Bearing (deg),TDOA confidence,File");
        foreach (var d in detections)
        {
            var end = d.OffsetSeconds + d.DurationSeconds;
            var file = Path.GetFileName(d.AudioFilePath);
            var bearing = double.IsNaN(d.BearingDeg) ? "" : d.BearingDeg.ToString("F1", CultureInfo.InvariantCulture);
            var tdoaConf = d.TdoaConfidence > 0 ? d.TdoaConfidence.ToString("F3", CultureInfo.InvariantCulture) : "";
            writer.WriteLine(string.Format(CultureInfo.InvariantCulture,
                "{0:F1},{1:F1},{2},{3},{4:F4},{5},{6},{7}",
                d.OffsetSeconds, end, d.ScientificName, d.CommonName, d.Confidence, bearing, tdoaConf, file));
        }
    }

    public static void WriteRavenTable(IList<Detection> detections, string path)
    {
        using var writer = new StreamWriter(path);
        writer.WriteLine("Selection\tView\tChannel\tBegin Time (s)\tEnd Time (s)\tLow Freq (Hz)\tHigh Freq (Hz)\tCommon Name\tSpecies Code\tConfidence\tBearing (deg)\tTDOA Confidence\tBegin Path\tFile Offset (s)");
        for (int i = 0; i < detections.Count; i++)
        {
            var d = detections[i];
            var end = d.OffsetSeconds + d.DurationSeconds;
            var file = Path.GetFileName(d.AudioFilePath);
            var bearing = double.IsNaN(d.BearingDeg) ? "" : d.BearingDeg.ToString("F1", CultureInfo.InvariantCulture);
            var tdoaConf = d.TdoaConfidence > 0 ? d.TdoaConfidence.ToString("F3", CultureInfo.InvariantCulture) : "";
            writer.WriteLine(string.Format(CultureInfo.InvariantCulture,
                "{0}\tSpectrogram 1\t1\t{1:F1}\t{2:F1}\t0\t15000\t{3}\t\t{4:F4}\t{5}\t{6}\t{7}\t{8:F1}",
                i + 1, d.OffsetSeconds, end, d.CommonName, d.Confidence, bearing, tdoaConf, file, d.OffsetSeconds));
        }
    }
}
