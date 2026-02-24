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
using System.Collections.Specialized;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using QuailTracker.Analyzer.Shared.Models;
using QuailTracker.Analyzer.Shared.ViewModels;

namespace QuailTracker.Analyzer.Shared.Views;

public partial class SingleAnalysisView : UserControl
{
    private static readonly int[] FrequencyMarks = [1000, 2000, 4000, 6000, 8000, 12000, 16000, 20000, 24000];
    private static readonly IBrush LabelBrush = new SolidColorBrush(Color.Parse("#AAAAAA"));
    private static readonly IBrush MarkerLineBrush = new SolidColorBrush(Color.Parse("#AA00E5FF"));
    private static readonly IBrush MarkerFillBrush = new SolidColorBrush(Color.Parse("#2000E5FF"));
    private static readonly IBrush MarkerTextBrush = new SolidColorBrush(Color.Parse("#00E5FF"));
    private static readonly IBrush MarkerLabelBg = new SolidColorBrush(Color.Parse("#CC1a0533"));

    private SingleAnalysisViewModel? _currentVm;

    public SingleAnalysisView()
    {
        InitializeComponent();

        SpectrogramImageControl.PropertyChanged += OnSpectrogramBoundsChanged;
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);

        if (_currentVm != null)
        {
            _currentVm.PropertyChanged -= OnViewModelPropertyChanged;
            _currentVm.Detections.CollectionChanged -= OnDetectionsChanged;
        }

        _currentVm = DataContext as SingleAnalysisViewModel;

        if (_currentVm != null)
        {
            _currentVm.PropertyChanged += OnViewModelPropertyChanged;
            _currentVm.Detections.CollectionChanged += OnDetectionsChanged;
        }
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(SingleAnalysisViewModel.SpectrogramImage)
            or nameof(SingleAnalysisViewModel.SampleRate)
            or nameof(SingleAnalysisViewModel.FileDuration)
            or nameof(SingleAnalysisViewModel.MaxFrequencyHz))
        {
            UpdateAxisLabels();
            UpdateDetectionMarkers();
        }
    }

    private void OnSpectrogramBoundsChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        if (e.Property == BoundsProperty)
        {
            UpdateAxisLabels();
            UpdateDetectionMarkers();
        }
    }

    private void OnDetectionsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        UpdateDetectionMarkers();
    }

    private void UpdateAxisLabels()
    {
        UpdateFrequencyLabels();
        UpdateTimeLabels();
    }

    private void UpdateFrequencyLabels()
    {
        FreqLabelCanvas.Children.Clear();

        if (_currentVm == null || _currentVm.SampleRate <= 0) return;

        double maxFreq = _currentVm.MaxFrequencyHz;
        if (maxFreq <= 0) maxFreq = _currentVm.SampleRate / 2.0;

        double canvasHeight = SpectrogramImageControl.Bounds.Height;
        if (canvasHeight <= 0) return;

        double canvasWidth = FreqLabelCanvas.Bounds.Width;
        if (canvasWidth <= 0) canvasWidth = 44;

        foreach (int freq in FrequencyMarks)
        {
            if (freq > maxFreq) break;

            double yFrac = freq / maxFreq;
            double y = canvasHeight * (1.0 - yFrac);

            string label = freq >= 1000 ? $"{freq / 1000}k" : freq.ToString();

            var tb = new TextBlock
            {
                Text = label,
                FontSize = 10,
                Foreground = LabelBrush,
                Width = canvasWidth - 4,
                TextAlignment = TextAlignment.Right,
            };

            Canvas.SetLeft(tb, 0);
            Canvas.SetTop(tb, y - 7);
            FreqLabelCanvas.Children.Add(tb);
        }
    }

    private void UpdateTimeLabels()
    {
        TimeLabelCanvas.Children.Clear();

        if (_currentVm == null) return;

        double duration = _currentVm.FileDuration.TotalSeconds;
        if (duration <= 0) return;

        double canvasWidth = SpectrogramImageControl.Bounds.Width;
        if (canvasWidth <= 0) return;

        double interval;
        if (duration <= 30) interval = 5;
        else if (duration <= 120) interval = 15;
        else if (duration <= 600) interval = 60;
        else interval = 300;

        for (double t = 0; t <= duration; t += interval)
        {
            double x = (t / duration) * canvasWidth;

            string label;
            if (duration < 120)
                label = $"{t:F0}s";
            else
                label = TimeSpan.FromSeconds(t).ToString(@"m\:ss");

            var tb = new TextBlock
            {
                Text = label,
                FontSize = 10,
                Foreground = LabelBrush,
            };

            Canvas.SetLeft(tb, x - 8);
            Canvas.SetTop(tb, 4);
            TimeLabelCanvas.Children.Add(tb);
        }
    }

    private void UpdateDetectionMarkers()
    {
        DetectionMarkerCanvas.Children.Clear();

        if (_currentVm == null || _currentVm.Detections.Count == 0) return;

        double duration = _currentVm.FileDuration.TotalSeconds;
        if (duration <= 0) return;

        double canvasWidth = SpectrogramImageControl.Bounds.Width;
        double canvasHeight = SpectrogramImageControl.Bounds.Height;
        if (canvasWidth <= 0 || canvasHeight <= 0) return;

        foreach (var detection in _currentVm.Detections)
        {
            double x = (detection.OffsetSeconds / duration) * canvasWidth;
            double width = (detection.DurationSeconds / duration) * canvasWidth;

            // Semi-transparent region spanning the detection duration
            var rect = new Rectangle
            {
                Width = Math.Max(width, 2),
                Height = canvasHeight,
                Fill = MarkerFillBrush,
            };
            Canvas.SetLeft(rect, x);
            Canvas.SetTop(rect, 0);
            DetectionMarkerCanvas.Children.Add(rect);

            // Vertical line at detection start
            var line = new Line
            {
                StartPoint = new Point(0, 0),
                EndPoint = new Point(0, canvasHeight),
                Stroke = MarkerLineBrush,
                StrokeThickness = 1,
            };
            Canvas.SetLeft(line, x);
            DetectionMarkerCanvas.Children.Add(line);

            // Time label at top of the marker
            string timeText;
            if (duration < 120)
                timeText = $"{detection.OffsetSeconds:F0}s";
            else
                timeText = TimeSpan.FromSeconds(detection.OffsetSeconds).ToString(@"m\:ss");

            var label = new TextBlock
            {
                Text = timeText,
                FontSize = 9,
                Foreground = MarkerTextBrush,
                Background = MarkerLabelBg,
                Padding = new Thickness(2, 1),
            };
            Canvas.SetLeft(label, x + 2);
            Canvas.SetTop(label, 2);
            DetectionMarkerCanvas.Children.Add(label);
        }
    }

    private async void OnOpenFileClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (_currentVm == null) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select Audio File",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("Audio Files") { Patterns = new[] { "*.wav", "*.flac" } }
            }
        });

        if (files.Count > 0)
        {
            await _currentVm.LoadFileCommand.ExecuteAsync(files[0].Path.LocalPath);
        }
    }

    private async void OnLoadModelClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (_currentVm == null) return;

        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select BirdNet ONNX Model",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("ONNX Model") { Patterns = new[] { "*.onnx" } }
            }
        });

        if (files.Count > 0)
        {
            await _currentVm.LoadModelCommand.ExecuteAsync(files[0].Path.LocalPath);
        }
    }

    private async void OnPlayDetectionClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Button { Tag: Detection detection } && _currentVm != null)
        {
            await _currentVm.PlayDetectionAsync(detection);
        }
    }

    private void OnStopPlaybackClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        _currentVm?.StopPlayback();
    }
}
