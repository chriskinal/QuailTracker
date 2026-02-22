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
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using MathNet.Numerics.LinearAlgebra;
using MathNet.Numerics.Optimization;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

public class PopulationService : IPopulationService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    public List<DailyCount> BuildCountMatrix(
        IReadOnlyList<Detection> detections,
        IReadOnlyList<Station> stations,
        IReadOnlyList<Localization> localizations,
        SurveyConfig config,
        ITdoaService tdoaService)
    {
        // Filter detections by species, confidence, date range
        // Empty TargetSpecies means all quail species
        var allQuail = string.IsNullOrEmpty(config.TargetSpecies);
        var filtered = detections
            .Where(d => allQuail
                ? TargetSpecies.QuailSpecies.Contains(d.ScientificName)
                : d.ScientificName == config.TargetSpecies)
            .Where(d => d.Confidence >= config.MinConfidence)
            .Where(d => d.Timestamp.Date >= config.StartDate.Date && d.Timestamp.Date <= config.EndDate.Date)
            .ToList();

        var stationDict = stations.ToDictionary(s => s.Id);
        var result = new List<DailyCount>();

        // Group by date
        var byDate = filtered.GroupBy(d => d.Timestamp.Date).ToList();

        foreach (var dayGroup in byDate)
        {
            var date = dayGroup.Key;
            var dayDetections = dayGroup.ToList();

            // Try localization-based dedup first, fall back to time-based
            var localizedDetections = dayDetections.Where(d => d.LocalizationId.HasValue).ToList();
            var unlocalizedDetections = dayDetections.Where(d => !d.LocalizationId.HasValue).ToList();

            // Station counts from localized detections
            var stationCounts = new Dictionary<string, (int count, int raw)>();

            if (localizedDetections.Count > 0)
            {
                var dayLocalizations = localizations
                    .Where(l => l.Timestamp.Date == date)
                    .Where(l => allQuail
                        ? TargetSpecies.QuailSpecies.Contains(l.Species)
                        : l.Species == config.TargetSpecies)
                    .ToList();

                // Cluster localizations within dedup radius
                var clusters = ClusterLocalizations(dayLocalizations, config.DeduplicationRadiusMeters, tdoaService);

                // Optional truncation: only keep clusters within truncation radius of a station
                if (config.TruncationRadiusMeters > 0)
                {
                    clusters = clusters.Where(c =>
                        stations.Any(s => s.HasValidLocation &&
                            tdoaService.CalculateDistance(c.lat, c.lon, s.Latitude, s.Longitude)
                                <= config.TruncationRadiusMeters))
                        .ToList();
                }

                // Assign each cluster to nearest station
                foreach (var cluster in clusters)
                {
                    var nearestStation = stations
                        .Where(s => s.HasValidLocation)
                        .OrderBy(s => tdoaService.CalculateDistance(cluster.lat, cluster.lon, s.Latitude, s.Longitude))
                        .FirstOrDefault();

                    if (nearestStation != null)
                    {
                        if (!stationCounts.ContainsKey(nearestStation.Id))
                            stationCounts[nearestStation.Id] = (0, 0);
                        var (c, r) = stationCounts[nearestStation.Id];
                        stationCounts[nearestStation.Id] = (c + 1, r + cluster.detectionCount);
                    }
                }
            }

            // Time-based dedup for unlocalized detections
            if (unlocalizedDetections.Count > 0)
            {
                var byStation = unlocalizedDetections.GroupBy(d => d.StationId);
                foreach (var stationGroup in byStation)
                {
                    var stationId = stationGroup.Key;
                    var sorted = stationGroup.OrderBy(d => d.Timestamp).ToList();
                    int dedupCount = DeduplicateByTime(sorted, TimeSpan.FromSeconds(30));

                    if (!stationCounts.ContainsKey(stationId))
                        stationCounts[stationId] = (0, 0);
                    var (c, r) = stationCounts[stationId];
                    stationCounts[stationId] = (c + dedupCount, r + sorted.Count);
                }
            }

            // Create DailyCount for each station that had any activity
            foreach (var (stationId, (count, raw)) in stationCounts)
            {
                result.Add(new DailyCount
                {
                    StationId = stationId,
                    Date = date,
                    Count = count,
                    RawDetectionCount = raw
                });
            }

            // Add zero counts for stations with no detections this day
            foreach (var station in stations)
            {
                if (!stationCounts.ContainsKey(station.Id))
                {
                    result.Add(new DailyCount
                    {
                        StationId = station.Id,
                        Date = date,
                        Count = 0,
                        RawDetectionCount = 0
                    });
                }
            }
        }

        return result.OrderBy(d => d.Date).ThenBy(d => d.StationId).ToList();
    }

    public PopulationEstimate ComputeSimpleSummary(
        List<DailyCount> dailyCounts,
        SurveyConfig config)
    {
        var estimate = new PopulationEstimate
        {
            SurveyYear = config.StartDate.Year,
            SurveyType = config.SurveyType,
            AnalysisTimestamp = DateTime.UtcNow,
            TotalSurveyDays = dailyCounts.Select(d => d.Date).Distinct().Count(),
            TotalStations = dailyCounts.Select(d => d.StationId).Distinct().Count()
        };

        // Max count per station across all days
        var byStation = dailyCounts.GroupBy(d => d.StationId);
        foreach (var group in byStation)
        {
            estimate.MaxCountPerStation[group.Key] = group.Max(d => d.Count);
        }

        if (estimate.MaxCountPerStation.Count > 0)
        {
            estimate.MeanMaxCount = estimate.MaxCountPerStation.Values.Average();
            estimate.TotalCoveys = estimate.MaxCountPerStation.Values.Sum();
            estimate.EstimatedPopulation = config.SurveyType == SurveyType.FallCoveyCount
                ? estimate.TotalCoveys * config.DefaultCoveySize
                : estimate.TotalCoveys;
        }

        return estimate;
    }

    public Task<PopulationEstimate> FitNMixtureModelAsync(
        List<DailyCount> dailyCounts,
        SurveyConfig config,
        bool useWeatherCovariates = false,
        CancellationToken ct = default)
    {
        return Task.Run(() =>
        {
            var estimate = ComputeSimpleSummary(dailyCounts, config);

            // Build Y[i,t] matrix: stations (i) x days (t)
            var stationIds = dailyCounts.Select(d => d.StationId).Distinct().OrderBy(s => s).ToList();
            var dates = dailyCounts.Select(d => d.Date).Distinct().OrderBy(d => d).ToList();
            var lookup = dailyCounts.ToDictionary(d => (d.StationId, d.Date));

            int nSites = stationIds.Count;
            int nVisits = dates.Count;

            if (nSites == 0 || nVisits < 2)
            {
                estimate.ModelConverged = false;
                return estimate;
            }

            var Y = new int[nSites, nVisits];
            int maxY = 0;
            for (int i = 0; i < nSites; i++)
            {
                for (int t = 0; t < nVisits; t++)
                {
                    int count = lookup.TryGetValue((stationIds[i], dates[t]), out var dc) ? dc.Count : 0;
                    Y[i, t] = count;
                    if (count > maxY) maxY = count;
                }
            }

            int K = Math.Max(maxY * 3, 50);

            // Precompute log-factorials for Poisson/Binomial
            var logFact = PrecomputeLogFactorials(K + 1);

            // Weather covariates
            double[]? tempCov = null;
            double[]? windCov = null;
            double[]? cloudCov = null;

            if (useWeatherCovariates)
            {
                tempCov = new double[nVisits];
                windCov = new double[nVisits];
                cloudCov = new double[nVisits];

                for (int t = 0; t < nVisits; t++)
                {
                    // Use first station's weather as representative (all stations share same Open-Meteo grid)
                    var dc = lookup.GetValueOrDefault((stationIds[0], dates[t]));
                    tempCov[t] = dc?.Temperature ?? 0;
                    windCov[t] = dc?.WindSpeed ?? 0;
                    cloudCov[t] = dc?.CloudCover ?? 0;
                }

                // Standardize covariates
                StandardizeInPlace(tempCov);
                StandardizeInPlace(windCov);
                StandardizeInPlace(cloudCov);
            }

            // Objective function: negative log-likelihood
            int nParams = useWeatherCovariates ? 5 : 2; // log(lambda), logit(p) [, beta_temp, beta_wind, beta_cloud]

            Func<Vector<double>, double> objective = (Vector<double> theta) =>
            {
                double logLambda = theta[0];
                double beta0 = theta[1]; // logit(p) intercept
                double lambda = Math.Exp(logLambda);

                double betaTemp = nParams > 2 ? theta[2] : 0;
                double betaWind = nParams > 2 ? theta[3] : 0;
                double betaCloud = nParams > 2 ? theta[4] : 0;

                double totalLL = 0;

                for (int i = 0; i < nSites; i++)
                {
                    ct.ThrowIfCancellationRequested();

                    int maxYi = 0;
                    for (int t = 0; t < nVisits; t++)
                        if (Y[i, t] > maxYi) maxYi = Y[i, t];

                    // Sum over latent N from max(y_i) to K
                    double logSumN = double.NegativeInfinity;

                    for (int n = maxYi; n <= K; n++)
                    {
                        // log Poisson(n | lambda)
                        double logPois = n * logLambda - lambda - logFact[n];

                        // Sum of log Binomial(y[i,t] | n, p(t)) across visits
                        double logBinomSum = 0;
                        for (int t = 0; t < nVisits; t++)
                        {
                            double logitP = beta0;
                            if (useWeatherCovariates)
                            {
                                logitP += betaTemp * tempCov![t]
                                        + betaWind * windCov![t]
                                        + betaCloud * cloudCov![t];
                            }
                            double p = Logistic(logitP);
                            p = Math.Clamp(p, 1e-10, 1 - 1e-10);

                            int y = Y[i, t];
                            // log C(n,y) + y*log(p) + (n-y)*log(1-p)
                            double logBinom = logFact[n] - logFact[y] - logFact[n - y]
                                            + y * Math.Log(p)
                                            + (n - y) * Math.Log(1 - p);
                            logBinomSum += logBinom;
                        }

                        double logTerm = logPois + logBinomSum;
                        logSumN = LogSumExp(logSumN, logTerm);
                    }

                    totalLL += logSumN;
                }

                return -totalLL; // Minimize negative log-likelihood
            };

            // Initial values
            double initLogLambda = Math.Log(Math.Max(estimate.MeanMaxCount, 0.5));
            double initLogitP = 0; // p = 0.5

            var initialGuess = Vector<double>.Build.Dense(nParams);
            initialGuess[0] = initLogLambda;
            initialGuess[1] = initLogitP;

            // Multiple random restarts for robustness
            var rng = new Random(42);
            double bestNLL = double.PositiveInfinity;
            Vector<double>? bestParams = null;

            for (int restart = 0; restart < 5; restart++)
            {
                ct.ThrowIfCancellationRequested();

                var startPoint = restart == 0
                    ? initialGuess
                    : Vector<double>.Build.Dense(nParams, j => initialGuess[j] + (rng.NextDouble() - 0.5) * 2);

                try
                {
                    var solver = new NelderMeadSimplex(1e-8, 10000);
                    var objWrapper = ObjectiveFunction.Value(v => objective(v));
                    var result = solver.FindMinimum(objWrapper, startPoint);

                    if (result.FunctionInfoAtMinimum.Value < bestNLL)
                    {
                        bestNLL = result.FunctionInfoAtMinimum.Value;
                        bestParams = result.MinimizingPoint;
                    }
                }
                catch (Exception)
                {
                    // Optimization failed for this restart, try next
                }
            }

            if (bestParams == null)
            {
                estimate.ModelConverged = false;
                return estimate;
            }

            // Extract parameters
            double fitLambda = Math.Exp(bestParams[0]);
            double fitP = Logistic(bestParams[1]);

            // Confidence intervals via numerical Hessian
            var hessian = NumericalHessian(objective, bestParams, 1e-4);
            double[]? stdErrors = null;

            try
            {
                var covMatrix = hessian.Inverse();
                stdErrors = new double[nParams];
                for (int j = 0; j < nParams; j++)
                {
                    double variance = covMatrix[j, j];
                    stdErrors[j] = variance > 0 ? Math.Sqrt(variance) : double.NaN;
                }
            }
            catch
            {
                // Hessian not invertible
            }

            estimate.ModelConverged = true;
            estimate.Lambda = fitLambda;
            estimate.DetectionProbability = fitP;
            estimate.LogLikelihood = -bestNLL;
            estimate.AIC = 2 * nParams + 2 * bestNLL;

            // Delta method CIs for lambda: lambda = exp(logLambda), SE(lambda) = lambda * SE(logLambda)
            if (stdErrors != null && !double.IsNaN(stdErrors[0]))
            {
                double seLambda = fitLambda * stdErrors[0];
                estimate.LambdaLower95 = Math.Max(0, fitLambda - 1.96 * seLambda);
                estimate.LambdaUpper95 = fitLambda + 1.96 * seLambda;
            }

            // Delta method CIs for p: p = logistic(beta0), SE(p) = p*(1-p)*SE(beta0)
            if (stdErrors != null && !double.IsNaN(stdErrors[1]))
            {
                double seP = fitP * (1 - fitP) * stdErrors[1];
                estimate.DetectionProbabilityLower95 = Math.Clamp(fitP - 1.96 * seP, 0, 1);
                estimate.DetectionProbabilityUpper95 = Math.Clamp(fitP + 1.96 * seP, 0, 1);
            }

            // Per-station abundance (posterior mean approximation = lambda for homogeneous model)
            foreach (var stationId in stationIds)
            {
                estimate.StationAbundance[stationId] = fitLambda;
            }

            // Covariate effects
            if (useWeatherCovariates && nParams > 2)
            {
                estimate.CovariateEffects["Temperature"] = bestParams[2];
                estimate.CovariateEffects["Wind Speed"] = bestParams[3];
                estimate.CovariateEffects["Cloud Cover"] = bestParams[4];
            }

            return estimate;
        }, ct);
    }

    public string ExportToJson(PopulationEstimate estimate) =>
        JsonSerializer.Serialize(estimate, JsonOptions);

    public PopulationEstimate? ImportFromJson(string json) =>
        JsonSerializer.Deserialize<PopulationEstimate>(json, JsonOptions);

    // --- Helpers ---

    private static List<(double lat, double lon, int detectionCount)> ClusterLocalizations(
        IReadOnlyList<Localization> localizations,
        double radiusMeters,
        ITdoaService tdoaService)
    {
        var clusters = new List<(double lat, double lon, int count)>();
        var assigned = new bool[localizations.Count];

        for (int i = 0; i < localizations.Count; i++)
        {
            if (assigned[i]) continue;

            var centroid = (lat: localizations[i].Latitude, lon: localizations[i].Longitude);
            int count = localizations[i].DetectionIds.Count;
            assigned[i] = true;

            // Greedily absorb nearby localizations
            for (int j = i + 1; j < localizations.Count; j++)
            {
                if (assigned[j]) continue;
                double dist = tdoaService.CalculateDistance(
                    centroid.lat, centroid.lon,
                    localizations[j].Latitude, localizations[j].Longitude);

                if (dist <= radiusMeters)
                {
                    assigned[j] = true;
                    count += localizations[j].DetectionIds.Count;
                }
            }

            clusters.Add((centroid.lat, centroid.lon, count));
        }

        return clusters;
    }

    private static int DeduplicateByTime(List<Detection> sorted, TimeSpan window)
    {
        if (sorted.Count == 0) return 0;
        int count = 1;
        var lastTime = sorted[0].Timestamp;

        for (int i = 1; i < sorted.Count; i++)
        {
            if (sorted[i].Timestamp - lastTime > window)
            {
                count++;
                lastTime = sorted[i].Timestamp;
            }
        }

        return count;
    }

    private static double[] PrecomputeLogFactorials(int n)
    {
        var logFact = new double[n + 1];
        logFact[0] = 0;
        for (int i = 1; i <= n; i++)
            logFact[i] = logFact[i - 1] + Math.Log(i);
        return logFact;
    }

    private static double Logistic(double x) => 1.0 / (1.0 + Math.Exp(-x));

    private static double LogSumExp(double a, double b)
    {
        if (double.IsNegativeInfinity(a)) return b;
        if (double.IsNegativeInfinity(b)) return a;
        double max = Math.Max(a, b);
        return max + Math.Log(Math.Exp(a - max) + Math.Exp(b - max));
    }

    private static void StandardizeInPlace(double[] values)
    {
        if (values.Length == 0) return;
        double mean = values.Average();
        double sd = Math.Sqrt(values.Select(v => (v - mean) * (v - mean)).Average());
        if (sd < 1e-10) sd = 1; // Avoid division by zero for constant covariates
        for (int i = 0; i < values.Length; i++)
            values[i] = (values[i] - mean) / sd;
    }

    private static Matrix<double> NumericalHessian(
        Func<Vector<double>, double> f,
        Vector<double> x,
        double eps)
    {
        int n = x.Count;
        var H = Matrix<double>.Build.Dense(n, n);
        double fx = f(x);

        for (int i = 0; i < n; i++)
        {
            for (int j = i; j < n; j++)
            {
                var xpp = x.Clone();
                var xpm = x.Clone();
                var xmp = x.Clone();
                var xmm = x.Clone();

                xpp[i] += eps; xpp[j] += eps;
                xpm[i] += eps; xpm[j] -= eps;
                xmp[i] -= eps; xmp[j] += eps;
                xmm[i] -= eps; xmm[j] -= eps;

                double d2f = (f(xpp) - f(xpm) - f(xmp) + f(xmm)) / (4 * eps * eps);
                H[i, j] = d2f;
                H[j, i] = d2f;
            }
        }

        return H;
    }
}
