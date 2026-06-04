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

public partial class ImportViewModel : ObservableObject
{
    private readonly IAudioFileService _audioFileService;
    private readonly ObservableCollection<AudioFile> _audioFiles;
    private readonly ObservableCollection<Station> _stations;
    [ObservableProperty]
    private string _statusMessage = string.Empty;

    private readonly Action<string> _setStatus;
    private CancellationTokenSource? _cts;

    [ObservableProperty]
    private bool _isImporting;

    [ObservableProperty]
    private int _importProgress;

    [ObservableProperty]
    private int _importTotal;

    [ObservableProperty]
    private string _currentFileName = string.Empty;

    [ObservableProperty]
    private AudioFile? _selectedAudioFile;

    [ObservableProperty]
    private Station? _selectedStation;

    [ObservableProperty]
    private string _newStationId = string.Empty;

    [ObservableProperty]
    private string _newStationLatitude = string.Empty;

    [ObservableProperty]
    private string _newStationLongitude = string.Empty;

    public ObservableCollection<AudioFile> AudioFiles => _audioFiles;
    public ObservableCollection<Station> Stations => _stations;

    public ImportViewModel(
        IAudioFileService audioFileService,
        ObservableCollection<AudioFile> audioFiles,
        ObservableCollection<Station> stations)
    {
        _audioFileService = audioFileService;
        _audioFiles = audioFiles;
        _stations = stations;
        _setStatus = msg => StatusMessage = msg;
    }

    [RelayCommand]
    private async Task ImportFilesAsync(IEnumerable<string> filePaths)
    {
        if (IsImporting) return;

        IsImporting = true;
        _cts = new CancellationTokenSource();

        try
        {
            var paths = filePaths.ToList();
            ImportTotal = paths.Count;
            ImportProgress = 0;

            foreach (var path in paths)
            {
                if (_cts.Token.IsCancellationRequested) break;

                CurrentFileName = Path.GetFileName(path);
                _setStatus($"Importing {CurrentFileName}...");

                var audioFile = await _audioFileService.LoadFileAsync(path, _cts.Token);
                _audioFiles.Add(audioFile);

                // Auto-create or update station from file metadata
                if (!string.IsNullOrEmpty(audioFile.StationId))
                {
                    var existing = _stations.FirstOrDefault(s => s.Id == audioFile.StationId);
                    if (existing == null)
                    {
                        _stations.Add(new Station
                        {
                            Id = audioFile.StationId,
                            Latitude = audioFile.Latitude ?? 0,
                            Longitude = audioFile.Longitude ?? 0,
                            Elevation = audioFile.Altitude,
                            MicHeadingDeg = audioFile.MicHeadingDeg,
                        });
                    }
                    else
                    {
                        if (!existing.HasValidLocation && audioFile.Latitude.HasValue)
                        {
                            existing.Latitude = audioFile.Latitude.Value;
                            existing.Longitude = audioFile.Longitude ?? 0;
                            existing.Elevation = audioFile.Altitude;
                        }
                        existing.MicHeadingDeg ??= audioFile.MicHeadingDeg;
                    }
                }

                ImportProgress++;
            }

            _setStatus($"Imported {ImportProgress} files");
        }
        catch (OperationCanceledException)
        {
            _setStatus("Import cancelled");
        }
        catch (Exception ex)
        {
            _setStatus($"Import error: {ex.Message}");
        }
        finally
        {
            IsImporting = false;
            _cts?.Dispose();
            _cts = null;
        }
    }

    [RelayCommand]
    private async Task ImportFolderAsync(string folderPath)
    {
        if (IsImporting || string.IsNullOrEmpty(folderPath)) return;

        IsImporting = true;
        _cts = new CancellationTokenSource();

        try
        {
            _setStatus($"Scanning folder...");

            var progress = new Progress<(int current, int total, string fileName)>(p =>
            {
                ImportProgress = p.current;
                ImportTotal = p.total;
                CurrentFileName = p.fileName;
                _setStatus($"Importing {p.fileName} ({p.current}/{p.total})...");
            });

            var files = await _audioFileService.LoadFolderAsync(folderPath, progress, _cts.Token);

            foreach (var file in files)
            {
                _audioFiles.Add(file);

                if (!string.IsNullOrEmpty(file.StationId))
                {
                    var existing = _stations.FirstOrDefault(s => s.Id == file.StationId);
                    if (existing == null)
                    {
                        _stations.Add(new Station
                        {
                            Id = file.StationId,
                            Latitude = file.Latitude ?? 0,
                            Longitude = file.Longitude ?? 0,
                            Elevation = file.Altitude,
                            MicHeadingDeg = file.MicHeadingDeg,
                        });
                    }
                    else
                    {
                        if (!existing.HasValidLocation && file.Latitude.HasValue)
                        {
                            existing.Latitude = file.Latitude.Value;
                            existing.Longitude = file.Longitude ?? 0;
                            existing.Elevation = file.Altitude;
                        }
                        existing.MicHeadingDeg ??= file.MicHeadingDeg;
                    }
                }
            }

            _setStatus($"Imported {files.Count} files from folder");
        }
        catch (OperationCanceledException)
        {
            _setStatus("Import cancelled");
        }
        catch (Exception ex)
        {
            _setStatus($"Import error: {ex.Message}");
        }
        finally
        {
            IsImporting = false;
            _cts?.Dispose();
            _cts = null;
        }
    }

    [RelayCommand]
    private void CancelImport()
    {
        _cts?.Cancel();
    }

    [RelayCommand]
    private void AddStation()
    {
        if (string.IsNullOrWhiteSpace(NewStationId)) return;

        if (!double.TryParse(NewStationLatitude, out var lat) ||
            !double.TryParse(NewStationLongitude, out var lon))
        {
            _setStatus("Invalid coordinates");
            return;
        }

        var existing = _stations.FirstOrDefault(s => s.Id == NewStationId);
        if (existing != null)
        {
            existing.Latitude = lat;
            existing.Longitude = lon;
            _setStatus($"Updated station {NewStationId}");
        }
        else
        {
            _stations.Add(new Station
            {
                Id = NewStationId,
                Latitude = lat,
                Longitude = lon
            });
            _setStatus($"Added station {NewStationId}");
        }

        NewStationId = string.Empty;
        NewStationLatitude = string.Empty;
        NewStationLongitude = string.Empty;
    }

    [RelayCommand]
    private void RemoveStation()
    {
        if (SelectedStation == null) return;

        var id = SelectedStation.Id;
        _stations.Remove(SelectedStation);
        SelectedStation = null;
        _setStatus($"Removed station {id}");
    }

    [RelayCommand]
    private void RemoveAudioFile()
    {
        if (SelectedAudioFile == null) return;

        var name = SelectedAudioFile.FileName;
        _audioFiles.Remove(SelectedAudioFile);
        SelectedAudioFile = null;
        _setStatus($"Removed {name}");
    }

    [RelayCommand]
    private void ClearAllFiles()
    {
        _audioFiles.Clear();
        _setStatus("Cleared all files");
    }
}
