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
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public partial class MapViewModel : ObservableObject
{
    private readonly IMapService _mapService;
    private readonly IKmlExportService _kmlExportService;
    private readonly ObservableCollection<Station> _stations;
    private readonly ObservableCollection<Detection> _detections;
    private readonly ObservableCollection<Localization> _localizations;
    private readonly Action<string> _setStatus;

    [ObservableProperty]
    private bool _isMapReady;

    [ObservableProperty]
    private bool _showStations = true;

    [ObservableProperty]
    private bool _showDetections = true;

    [ObservableProperty]
    private bool _showLocalizations = true;

    [ObservableProperty]
    private DateTime? _filterStartTime;

    [ObservableProperty]
    private DateTime? _filterEndTime;

    [ObservableProperty]
    private Station? _selectedStation;

    [ObservableProperty]
    private Detection? _selectedDetection;

    [ObservableProperty]
    private Localization? _selectedLocalization;

    public ObservableCollection<Station> Stations => _stations;
    public ObservableCollection<Detection> Detections => _detections;
    public ObservableCollection<Localization> Localizations => _localizations;

    public MapViewModel(
        IMapService mapService,
        IKmlExportService kmlExportService,
        ObservableCollection<Station> stations,
        ObservableCollection<Detection> detections,
        ObservableCollection<Localization> localizations,
        Action<string> setStatus)
    {
        _mapService = mapService;
        _kmlExportService = kmlExportService;
        _stations = stations;
        _detections = detections;
        _localizations = localizations;
        _setStatus = setStatus;

        _mapService.MapReady += (_, _) =>
        {
            IsMapReady = true;
            _ = RefreshMapAsync();
        };

        _mapService.StationClicked += (_, id) =>
        {
            SelectedStation = _stations.FirstOrDefault(s => s.Id == id);
        };

        _mapService.DetectionClicked += (_, id) =>
        {
            SelectedDetection = _detections.FirstOrDefault(d => d.Id == id);
        };

        _mapService.LocalizationClicked += (_, id) =>
        {
            SelectedLocalization = _localizations.FirstOrDefault(l => l.Id == id);
        };

        // Auto-refresh when collections change
        _stations.CollectionChanged += async (_, _) => await RefreshStationsAsync();
        _detections.CollectionChanged += async (_, _) => await RefreshDetectionsAsync();
        _localizations.CollectionChanged += async (_, _) => await RefreshLocalizationsAsync();
    }

    public async Task InitializeMapAsync(object webView)
    {
        try
        {
            _setStatus("Initializing map...");
            await _mapService.InitializeAsync(webView);
        }
        catch (Exception ex)
        {
            _setStatus($"Map initialization failed: {ex.Message}");
        }
    }

    partial void OnShowStationsChanged(bool value) => _ = UpdateLayerVisibilityAsync();
    partial void OnShowDetectionsChanged(bool value) => _ = UpdateLayerVisibilityAsync();
    partial void OnShowLocalizationsChanged(bool value) => _ = UpdateLayerVisibilityAsync();
    partial void OnFilterStartTimeChanged(DateTime? value) => _ = UpdateTimeFilterAsync();
    partial void OnFilterEndTimeChanged(DateTime? value) => _ = UpdateTimeFilterAsync();

    private async Task UpdateLayerVisibilityAsync()
    {
        if (!IsMapReady) return;
        await _mapService.SetLayerVisibilityAsync(ShowStations, ShowDetections, ShowLocalizations);
    }

    private async Task UpdateTimeFilterAsync()
    {
        if (!IsMapReady) return;
        await _mapService.SetTimeFilterAsync(FilterStartTime, FilterEndTime);
    }

    [RelayCommand]
    private async Task RefreshMapAsync()
    {
        if (!IsMapReady) return;

        try
        {
            await RefreshStationsAsync();
            await RefreshDetectionsAsync();
            await RefreshLocalizationsAsync();
            _setStatus("Map refreshed");
        }
        catch (Exception ex)
        {
            _setStatus($"Map refresh failed: {ex.Message}");
        }
    }

    private async Task RefreshStationsAsync()
    {
        if (!IsMapReady) return;
        await _mapService.SetStationsAsync(_stations.Where(s => s.HasValidLocation).ToList());
    }

    private async Task RefreshDetectionsAsync()
    {
        if (!IsMapReady) return;
        await _mapService.SetDetectionsAsync(_detections.ToList(), _stations.ToList());
    }

    private async Task RefreshLocalizationsAsync()
    {
        if (!IsMapReady) return;
        await _mapService.SetLocalizationsAsync(_localizations.ToList());
    }

    [RelayCommand]
    private async Task FlyToAllAsync()
    {
        if (!IsMapReady) return;
        await _mapService.FlyToAllAsync();
    }

    [RelayCommand]
    private async Task FlyToStationAsync()
    {
        if (!IsMapReady || SelectedStation == null || !SelectedStation.HasValidLocation) return;
        await _mapService.FlyToAsync(SelectedStation.Latitude, SelectedStation.Longitude);
        await _mapService.HighlightEntityAsync(stationId: SelectedStation.Id);
    }

    [RelayCommand]
    private async Task FlyToLocalizationAsync()
    {
        if (!IsMapReady || SelectedLocalization == null) return;
        await _mapService.FlyToAsync(SelectedLocalization.Latitude, SelectedLocalization.Longitude);
        await _mapService.HighlightEntityAsync(localizationId: SelectedLocalization.Id);
    }

    [RelayCommand]
    private async Task ExportKmlAsync(string filePath)
    {
        if (string.IsNullOrEmpty(filePath)) return;

        try
        {
            _setStatus("Exporting to KML...");

            await _kmlExportService.ExportAsync(
                filePath,
                _stations.Where(s => s.HasValidLocation).ToList(),
                _detections.ToList(),
                _localizations.ToList(),
                new KmlExportOptions
                {
                    IncludeStations = ShowStations,
                    IncludeDetections = ShowDetections,
                    IncludeLocalizations = ShowLocalizations,
                    DocumentName = "QuailTracker Analysis"
                });

            _setStatus($"Exported to {filePath}");
        }
        catch (Exception ex)
        {
            _setStatus($"Export failed: {ex.Message}");
        }
    }

    [RelayCommand]
    private void ClearTimeFilter()
    {
        FilterStartTime = null;
        FilterEndTime = null;
    }
}
