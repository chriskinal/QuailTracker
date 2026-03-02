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
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public partial class PopulationViewModel : ObservableObject
{
    private readonly IPopulationService _populationService;
    private readonly IWeatherService _weatherService;
    private readonly ITdoaService _tdoaService;
    private readonly ObservableCollection<Station> _stations;
    private readonly ObservableCollection<Detection> _detections;
    private readonly ObservableCollection<Localization> _localizations;
    [ObservableProperty]
    private string _statusMessage = string.Empty;

    private readonly Action<string> _setStatus;
    private CancellationTokenSource? _cts;

    // Survey configuration
    [ObservableProperty]
    private DateTime? _startDate = DateTime.Today.AddMonths(-3);

    [ObservableProperty]
    private DateTime? _endDate = DateTime.Today;

    [ObservableProperty]
    private int _selectedSeasonIndex; // 0=Auto, 1=Spring, 2=Fall

    [ObservableProperty]
    private int _selectedSpeciesIndex;

    [ObservableProperty]
    private double _minConfidence = 0.5;

    [ObservableProperty]
    private double _deduplicationRadius = 200;

    [ObservableProperty]
    private double _truncationRadius = 500;

    [ObservableProperty]
    private int _defaultCoveySize = 12;

    // State flags
    [ObservableProperty]
    private bool _isAnalyzing;

    [ObservableProperty]
    private bool _isWeatherLoaded;

    [ObservableProperty]
    private bool _hasResults;

    [ObservableProperty]
    private string _progressText = string.Empty;

    // Simple summary results
    [ObservableProperty]
    private int _totalSurveyDays;

    [ObservableProperty]
    private int _totalStationsCount;

    [ObservableProperty]
    private double _meanMaxCount;

    [ObservableProperty]
    private int _totalCoveys;

    [ObservableProperty]
    private int _estimatedPopulation;

    // N-mixture results
    [ObservableProperty]
    private bool _modelConverged;

    [ObservableProperty]
    private string _lambdaDisplay = string.Empty;

    [ObservableProperty]
    private string _detectionProbabilityDisplay = string.Empty;

    [ObservableProperty]
    private string _logLikelihoodDisplay = string.Empty;

    [ObservableProperty]
    private string _aicDisplay = string.Empty;

    [ObservableProperty]
    private string _covariateTemperature = string.Empty;

    [ObservableProperty]
    private string _covariateWind = string.Empty;

    [ObservableProperty]
    private string _covariateCloud = string.Empty;

    [ObservableProperty]
    private bool _hasCovariateEffects;

    public ObservableCollection<DailyCount> DailyCounts { get; } = [];
    public ObservableCollection<PopulationEstimate> PreviousEstimates { get; } = [];

    public ObservableCollection<Station> Stations => _stations;
    public ObservableCollection<Detection> Detections => _detections;

    public static string[] SeasonOptions { get; } =
        ["Auto (from dates)", "Spring Whistle Count", "Fall Covey Count"];

    public static string[] SpeciesOptions { get; } =
        ["All Quail", "Northern Bobwhite", "Scaled Quail", "Gambel's Quail", "California Quail", "Montezuma Quail"];

    /// <summary>
    /// Resolves the effective survey type: manual override or auto-detect from start date month.
    /// </summary>
    public SurveyType EffectiveSurveyType => SelectedSeasonIndex switch
    {
        1 => SurveyType.SpringWhistleCount,
        2 => SurveyType.FallCoveyCount,
        _ => AutoDetectSurveyType()
    };

    public bool IsFallCoveyCount => EffectiveSurveyType == SurveyType.FallCoveyCount;

    private SurveyType AutoDetectSurveyType()
    {
        int month = (StartDate ?? DateTime.Today).Month;
        return month switch
        {
            >= 3 and <= 6 => SurveyType.SpringWhistleCount,
            >= 9 and <= 12 => SurveyType.FallCoveyCount,
            _ => SurveyType.Continuous
        };
    }

    partial void OnStartDateChanged(DateTime? value) =>
        OnPropertyChanged(nameof(IsFallCoveyCount));

    partial void OnSelectedSeasonIndexChanged(int value) =>
        OnPropertyChanged(nameof(IsFallCoveyCount));

    public PopulationViewModel(
        IPopulationService populationService,
        IWeatherService weatherService,
        ITdoaService tdoaService,
        ObservableCollection<Station> stations,
        ObservableCollection<Detection> detections,
        ObservableCollection<Localization> localizations)
    {
        _populationService = populationService;
        _weatherService = weatherService;
        _tdoaService = tdoaService;
        _stations = stations;
        _detections = detections;
        _localizations = localizations;
        _setStatus = msg => StatusMessage = msg;
    }

    private SurveyConfig BuildConfig() => new()
    {
        SurveyType = EffectiveSurveyType,
        StartDate = StartDate ?? DateTime.Today.AddMonths(-3),
        EndDate = EndDate ?? DateTime.Today,
        TargetSpecies = SelectedSpeciesIndex switch
        {
            0 => string.Empty, // All Quail
            1 => TargetSpecies.NorthernBobwhite,
            2 => TargetSpecies.ScaledQuail,
            3 => TargetSpecies.GambelQuail,
            4 => TargetSpecies.CaliforniaQuail,
            5 => TargetSpecies.MontezumaQuail,
            _ => string.Empty
        },
        MinConfidence = MinConfidence,
        DeduplicationRadiusMeters = DeduplicationRadius,
        TruncationRadiusMeters = TruncationRadius,
        DefaultCoveySize = DefaultCoveySize
    };

    [RelayCommand]
    private void BuildCountMatrix()
    {
        if (_detections.Count == 0)
        {
            _setStatus("No detections available. Run BirdNET processing first.");
            return;
        }

        var config = BuildConfig();
        DailyCounts.Clear();

        var counts = _populationService.BuildCountMatrix(
            _detections.ToList(),
            _stations.ToList(),
            _localizations.ToList(),
            config,
            _tdoaService);

        foreach (var dc in counts)
            DailyCounts.Add(dc);

        // Compute simple summary
        var summary = _populationService.ComputeSimpleSummary(counts, config);
        TotalSurveyDays = summary.TotalSurveyDays;
        TotalStationsCount = summary.TotalStations;
        MeanMaxCount = summary.MeanMaxCount;
        TotalCoveys = summary.TotalCoveys;
        EstimatedPopulation = summary.EstimatedPopulation;
        HasResults = true;

        _setStatus($"Count matrix built: {DailyCounts.Count} cells, {TotalSurveyDays} days, {TotalStationsCount} stations");
    }

    [RelayCommand]
    private async Task FetchWeatherAsync()
    {
        if (_stations.Count == 0)
        {
            _setStatus("No stations available");
            return;
        }

        var stationsWithGps = _stations.Where(s => s.HasValidLocation).ToList();
        if (stationsWithGps.Count == 0)
        {
            _setStatus("No stations with GPS coordinates");
            return;
        }

        IsAnalyzing = true;
        ProgressText = "Fetching weather data from Open-Meteo...";
        _cts = new CancellationTokenSource();

        try
        {
            // Use centroid of all stations
            double lat = stationsWithGps.Average(s => s.Latitude);
            double lon = stationsWithGps.Average(s => s.Longitude);

            var config = BuildConfig();
            var hourlyData = await _weatherService.FetchWeatherAsync(
                lat, lon,
                config.StartDate,
                config.EndDate,
                _cts.Token);

            // Get morning averages (5 AM to 9 AM as default survey window)
            var morningConditions = _weatherService.GetMorningConditions(hourlyData, 5, 9);
            var weatherByDate = morningConditions.ToDictionary(w => w.Timestamp.Date);

            // Attach weather to daily counts
            foreach (var dc in DailyCounts)
            {
                if (weatherByDate.TryGetValue(dc.Date, out var weather))
                {
                    dc.Temperature = weather.TemperatureCelsius;
                    dc.WindSpeed = weather.WindSpeedKmh;
                    dc.CloudCover = weather.CloudCoverPercent;
                    dc.Precipitation = weather.PrecipitationMm;
                }
            }

            // Force DataGrid refresh
            var items = DailyCounts.ToList();
            DailyCounts.Clear();
            foreach (var item in items)
                DailyCounts.Add(item);

            IsWeatherLoaded = true;
            _setStatus($"Weather data loaded: {morningConditions.Count} days from Open-Meteo");
        }
        catch (OperationCanceledException)
        {
            _setStatus("Weather fetch cancelled");
        }
        catch (Exception ex)
        {
            _setStatus($"Weather fetch failed: {ex.Message}");
        }
        finally
        {
            IsAnalyzing = false;
            ProgressText = string.Empty;
            _cts?.Dispose();
            _cts = null;
        }
    }

    [RelayCommand]
    private async Task RunAnalysisAsync()
    {
        if (DailyCounts.Count == 0)
        {
            _setStatus("Build count matrix first");
            return;
        }

        IsAnalyzing = true;
        ProgressText = "Fitting N-mixture model...";
        _cts = new CancellationTokenSource();

        try
        {
            var config = BuildConfig();
            var estimate = await _populationService.FitNMixtureModelAsync(
                DailyCounts.ToList(),
                config,
                IsWeatherLoaded,
                _cts.Token);

            ModelConverged = estimate.ModelConverged;
            LambdaDisplay = estimate.LambdaDisplay;
            DetectionProbabilityDisplay = estimate.DetectionDisplay;
            LogLikelihoodDisplay = estimate.ModelConverged ? $"{estimate.LogLikelihood:F2}" : "-";
            AicDisplay = estimate.AicDisplay;

            if (estimate.CovariateEffects.Count > 0)
            {
                HasCovariateEffects = true;
                CovariateTemperature = estimate.CovariateEffects.TryGetValue("Temperature", out var temp)
                    ? $"{temp:F3}" : "-";
                CovariateWind = estimate.CovariateEffects.TryGetValue("Wind Speed", out var wind)
                    ? $"{wind:F3}" : "-";
                CovariateCloud = estimate.CovariateEffects.TryGetValue("Cloud Cover", out var cloud)
                    ? $"{cloud:F3}" : "-";
            }
            else
            {
                HasCovariateEffects = false;
            }

            HasResults = true;

            _setStatus(estimate.ModelConverged
                ? $"N-mixture model converged: lambda={estimate.Lambda:F2}, p={estimate.DetectionProbability:F3}, AIC={estimate.AIC:F1}"
                : "N-mixture model did not converge. Try adjusting parameters.");
        }
        catch (OperationCanceledException)
        {
            _setStatus("Analysis cancelled");
        }
        catch (Exception ex)
        {
            _setStatus($"Analysis error: {ex.Message}");
        }
        finally
        {
            IsAnalyzing = false;
            ProgressText = string.Empty;
            _cts?.Dispose();
            _cts = null;
        }
    }

    [RelayCommand]
    private void CancelAnalysis()
    {
        _cts?.Cancel();
    }

    [RelayCommand]
    private void Clear()
    {
        DailyCounts.Clear();
        HasResults = false;
        IsWeatherLoaded = false;
        HasCovariateEffects = false;
        ModelConverged = false;
        LambdaDisplay = string.Empty;
        DetectionProbabilityDisplay = string.Empty;
        LogLikelihoodDisplay = string.Empty;
        AicDisplay = string.Empty;
        TotalSurveyDays = 0;
        TotalStationsCount = 0;
        MeanMaxCount = 0;
        TotalCoveys = 0;
        EstimatedPopulation = 0;
        _setStatus("Cleared population analysis");
    }

    /// <summary>
    /// Exports results to a JSON file. Called from code-behind with file dialog path.
    /// </summary>
    public void ExportResults(string filePath)
    {
        if (!HasResults) return;

        var config = BuildConfig();
        var estimate = new PopulationEstimate
        {
            SurveyYear = config.StartDate.Year,
            SurveyType = config.SurveyType,
            AnalysisTimestamp = DateTime.UtcNow,
            TotalSurveyDays = TotalSurveyDays,
            TotalStations = TotalStationsCount,
            MeanMaxCount = MeanMaxCount,
            TotalCoveys = TotalCoveys,
            EstimatedPopulation = EstimatedPopulation,
            ModelConverged = ModelConverged
        };

        // Re-run to get full estimate with all fields
        if (ModelConverged)
        {
            var counts = DailyCounts.ToList();
            var summary = _populationService.ComputeSimpleSummary(counts, config);
            estimate.MaxCountPerStation = summary.MaxCountPerStation;
        }

        var json = _populationService.ExportToJson(estimate);
        File.WriteAllText(filePath, json);
        _setStatus($"Results exported to {Path.GetFileName(filePath)}");
    }

    /// <summary>
    /// Imports a previous year's estimate from JSON. Called from code-behind with file dialog path.
    /// </summary>
    public void ImportPrevious(string filePath)
    {
        var json = File.ReadAllText(filePath);
        var estimate = _populationService.ImportFromJson(json);

        if (estimate != null)
        {
            PreviousEstimates.Add(estimate);
            _setStatus($"Imported {estimate.SurveyYear} {estimate.SurveyType} estimate");
        }
        else
        {
            _setStatus("Failed to parse estimate file");
        }
    }
}
