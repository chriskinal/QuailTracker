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

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public partial class MainWindowViewModel : ObservableObject
{
    /// <summary>Window title including the app version, e.g. "QuailTracker Analyzer v26.0.0".</summary>
    public string WindowTitle => AppInfo.Title;

    public ConfigService ConfigService { get; }

    /// <summary>
    /// Persisted window geometry, in window units. Null means "use XAML defaults"
    /// (first launch, or saved state was incomplete).
    /// </summary>
    public readonly record struct WindowState(
        double X, double Y, double Width, double Height, bool IsMaximized);

    public WindowState? GetSavedWindowState()
    {
        var c = ConfigService;
        if (!c.WindowWidth.HasValue || !c.WindowHeight.HasValue) return null;
        return new WindowState(
            c.WindowX ?? 0,
            c.WindowY ?? 0,
            c.WindowWidth.Value,
            c.WindowHeight.Value,
            c.IsMaximized);
    }

    public void SaveWindowState(WindowState state)
    {
        var c = ConfigService;
        c.IsMaximized = state.IsMaximized;
        if (!state.IsMaximized)
        {
            c.WindowWidth = state.Width;
            c.WindowHeight = state.Height;
            c.WindowX = state.X;
            c.WindowY = state.Y;
        }
        c.Save();
    }

    // Shared data collections
    public ObservableCollection<AudioFile> AudioFiles { get; } = [];
    public ObservableCollection<Station> Stations { get; } = [];
    public ObservableCollection<Detection> Detections { get; } = [];
    public ObservableCollection<Localization> Localizations { get; } = [];

    // Child ViewModels
    [ObservableProperty]
    private SingleAnalysisViewModel _singleAnalysisViewModel;

    [ObservableProperty]
    private ImportViewModel _importViewModel;

    [ObservableProperty]
    private ProcessingViewModel _processingViewModel;

    [ObservableProperty]
    private LocalizationViewModel _localizationViewModel;

    [ObservableProperty]
    private PopulationViewModel _populationViewModel;

    [ObservableProperty]
    private TrainingDataViewModel _trainingDataViewModel;

    [ObservableProperty]
    private ModelingTrainingViewModel _modelingTrainingViewModel;

    [ObservableProperty]
    private XenoCantoDownloadViewModel _xenoCantoDownloadViewModel;

    [ObservableProperty]
    private ModelingEvaluationViewModel _modelingEvaluationViewModel;

    [ObservableProperty]
    private MapViewModel _mapViewModel;

    public MainWindowViewModel()
        : this(ConfigService.Load())
    {
    }

    private MainWindowViewModel(ConfigService config)
        : this(config, new TrainingService(config))
    {
    }

    private MainWindowViewModel(ConfigService config, ITrainingService trainingService)
        : this(
            new AudioFileService(),
            new BirdNetService(),
            new TdoaService(),
            new MapService(),
            new KmlExportService(),
            new PopulationService(),
            new WeatherService(),
            trainingService,
            new TrainingContainerStatusService(trainingService),
            config,
            new AppStateService())
    {
    }

    public MainWindowViewModel(
        IAudioFileService audioFileService,
        IBirdNetService birdNetService,
        ITdoaService tdoaService,
        IMapService mapService,
        IKmlExportService kmlExportService,
        IPopulationService populationService,
        IWeatherService weatherService,
        ITrainingService trainingService,
        TrainingContainerStatusService trainingStatus,
        ConfigService configService,
        AppStateService appState)
    {
        ConfigService = configService;

        var noiseReduction = new NoiseReductionService();

        _singleAnalysisViewModel = new SingleAnalysisViewModel(
            audioFileService,
            birdNetService,
            new SpectrogramService(),
            new AudioPlaybackService(),
            noiseReduction,
            configService,
            appState);

        _importViewModel = new ImportViewModel(
            audioFileService,
            AudioFiles,
            Stations);

        _processingViewModel = new ProcessingViewModel(
            audioFileService,
            birdNetService,
            configService,
            appState,
            AudioFiles,
            Detections);

        _trainingDataViewModel = new TrainingDataViewModel(
            audioFileService,
            AudioFiles,
            Detections);

        _modelingTrainingViewModel = new ModelingTrainingViewModel(trainingService, trainingStatus, configService);

        _xenoCantoDownloadViewModel = new XenoCantoDownloadViewModel(trainingService, trainingStatus, configService);

        _modelingEvaluationViewModel = new ModelingEvaluationViewModel(trainingService, trainingStatus);

        _localizationViewModel = new LocalizationViewModel(
            tdoaService,
            Stations,
            Detections,
            Localizations);

        _populationViewModel = new PopulationViewModel(
            populationService,
            weatherService,
            tdoaService,
            Stations,
            Detections,
            Localizations);

        _mapViewModel = new MapViewModel(
            mapService,
            kmlExportService,
            Stations,
            Detections,
            Localizations);
    }
}
