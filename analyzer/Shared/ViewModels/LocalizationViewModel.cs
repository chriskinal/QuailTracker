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
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.Services;

namespace QuailTracker.Analyzer.Shared.ViewModels;

public partial class LocalizationViewModel : ObservableObject
{
    private readonly ITdoaService _tdoaService;
    private readonly ObservableCollection<Station> _stations;
    private readonly ObservableCollection<Detection> _detections;
    private readonly ObservableCollection<Localization> _localizations;
    [ObservableProperty]
    private string _statusMessage = string.Empty;

    private readonly Action<string> _setStatus;
    private CancellationTokenSource? _cts;

    [ObservableProperty]
    private bool _isLocalizing;

    [ObservableProperty]
    private int _currentMatch;

    [ObservableProperty]
    private int _totalMatches;

    [ObservableProperty]
    private Localization? _selectedLocalization;

    [ObservableProperty]
    private double _maxTimeDifferenceMs = 3000;

    [ObservableProperty]
    private double _speedOfSound = 343.0;

    public ObservableCollection<Station> Stations => _stations;
    public ObservableCollection<Detection> Detections => _detections;
    public ObservableCollection<Localization> Localizations => _localizations;
    public ObservableCollection<DetectionMatch> Matches { get; } = [];

    public int SelectedDetectionCount => _detections.Count(d => d.IsSelected);
    public int StationsWithLocationCount => _stations.Count(s => s.HasValidLocation);

    public LocalizationViewModel(
        ITdoaService tdoaService,
        ObservableCollection<Station> stations,
        ObservableCollection<Detection> detections,
        ObservableCollection<Localization> localizations)
    {
        _tdoaService = tdoaService;
        _stations = stations;
        _detections = detections;
        _localizations = localizations;
        _setStatus = msg => StatusMessage = msg;

        _detections.CollectionChanged += (_, _) => OnPropertyChanged(nameof(SelectedDetectionCount));
        _stations.CollectionChanged += (_, _) => OnPropertyChanged(nameof(StationsWithLocationCount));
    }

    partial void OnMaxTimeDifferenceMsChanged(double value)
    {
        _tdoaService.MaxTimeDifferenceMs = value;
    }

    partial void OnSpeedOfSoundChanged(double value)
    {
        _tdoaService.SpeedOfSound = value;
    }

    [RelayCommand]
    private void FindMatches()
    {
        Matches.Clear();

        var selectedDetections = _detections.Where(d => d.IsSelected).ToList();
        var stationsWithLocation = _stations.Where(s => s.HasValidLocation).ToList();

        if (selectedDetections.Count == 0)
        {
            _setStatus("No detections selected");
            return;
        }

        if (stationsWithLocation.Count < 3)
        {
            _setStatus("Need at least 3 stations with GPS coordinates");
            return;
        }

        var matches = _tdoaService.MatchDetections(selectedDetections, stationsWithLocation);

        foreach (var match in matches.Where(m => m.IsValidForLocalization))
        {
            Matches.Add(match);
        }

        _setStatus($"Found {Matches.Count} potential localizations from {selectedDetections.Count} detections");
    }

    [RelayCommand(CanExecute = nameof(CanLocalize))]
    private async Task LocalizeAsync()
    {
        if (IsLocalizing || Matches.Count == 0) return;

        IsLocalizing = true;
        _cts = new CancellationTokenSource();
        TotalMatches = Matches.Count;
        CurrentMatch = 0;

        try
        {
            var progress = new Progress<(int current, int total)>(p =>
            {
                CurrentMatch = p.current;
                TotalMatches = p.total;
                _setStatus($"Localizing {p.current}/{p.total}...");
            });

            var results = await _tdoaService.LocalizeAllAsync(
                Matches.ToList(),
                progress,
                _cts.Token);

            foreach (var localization in results)
            {
                _localizations.Add(localization);

                // Link detections to their localization
                foreach (var detectionId in localization.DetectionIds)
                {
                    var detection = _detections.FirstOrDefault(d => d.Id == detectionId);
                    if (detection != null)
                    {
                        detection.LocalizationId = localization.Id;
                    }
                }
            }

            _setStatus($"Localization complete. {results.Count} positions calculated.");
        }
        catch (OperationCanceledException)
        {
            _setStatus("Localization cancelled");
        }
        catch (Exception ex)
        {
            _setStatus($"Localization error: {ex.Message}");
        }
        finally
        {
            IsLocalizing = false;
            _cts?.Dispose();
            _cts = null;
        }
    }

    private bool CanLocalize() => !IsLocalizing && Matches.Count > 0;

    [RelayCommand]
    private void CancelLocalization()
    {
        _cts?.Cancel();
    }

    [RelayCommand]
    private void ClearLocalizations()
    {
        _localizations.Clear();
        Matches.Clear();

        foreach (var detection in _detections)
        {
            detection.LocalizationId = null;
        }

        _setStatus("Cleared all localizations");
    }

    [RelayCommand]
    private void RemoveLocalization()
    {
        if (SelectedLocalization == null) return;

        foreach (var detectionId in SelectedLocalization.DetectionIds)
        {
            var detection = _detections.FirstOrDefault(d => d.Id == detectionId);
            if (detection != null)
            {
                detection.LocalizationId = null;
            }
        }

        _localizations.Remove(SelectedLocalization);
        _setStatus("Removed localization");
        SelectedLocalization = null;
    }
}
