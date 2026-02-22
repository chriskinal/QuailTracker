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

namespace QuailTracker.Analyzer.Shared.Models;

/// <summary>
/// Population estimate results from simple summary and N-mixture model analysis.
/// </summary>
public class PopulationEstimate
{
    // Metadata
    public int SurveyYear { get; set; }
    public SurveyType SurveyType { get; set; }
    public DateTime AnalysisTimestamp { get; set; } = DateTime.UtcNow;

    // Simple summary statistics
    public int TotalSurveyDays { get; set; }
    public int TotalStations { get; set; }
    public Dictionary<string, int> MaxCountPerStation { get; set; } = [];
    public double MeanMaxCount { get; set; }
    public int TotalCoveys { get; set; }
    public int EstimatedPopulation { get; set; }

    // N-mixture model results
    public bool ModelConverged { get; set; }
    public double Lambda { get; set; }
    public double LambdaLower95 { get; set; }
    public double LambdaUpper95 { get; set; }
    public double DetectionProbability { get; set; }
    public double DetectionProbabilityLower95 { get; set; }
    public double DetectionProbabilityUpper95 { get; set; }
    public double LogLikelihood { get; set; }
    public double AIC { get; set; }
    public Dictionary<string, double> StationAbundance { get; set; } = [];
    public Dictionary<string, double> CovariateEffects { get; set; } = [];

    public string LambdaDisplay => ModelConverged
        ? $"{Lambda:F2} [{LambdaLower95:F2}, {LambdaUpper95:F2}]"
        : "Did not converge";

    public string DetectionDisplay => ModelConverged
        ? $"{DetectionProbability:F3} [{DetectionProbabilityLower95:F3}, {DetectionProbabilityUpper95:F3}]"
        : "-";

    public string AicDisplay => ModelConverged ? $"{AIC:F1}" : "-";

    public override string ToString() =>
        $"{SurveyYear} {SurveyType}: lambda={LambdaDisplay}, p={DetectionDisplay}";
}
