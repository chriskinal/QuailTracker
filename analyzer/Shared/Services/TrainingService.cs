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
using System.Net;
using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using QuailTracker.Analyzer.Shared.Models;

namespace QuailTracker.Analyzer.Shared.Services;

public sealed class TrainingService : ITrainingService, IDisposable
{
    public const string DefaultBaseUrl = "http://localhost:5050";

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    private readonly HttpClient _http;
    private readonly bool _ownsHttp;

    public Uri BaseAddress => _http.BaseAddress!;

    public TrainingService(string? baseUrl = null)
    {
        _http = new HttpClient
        {
            BaseAddress = new Uri(NormalizeUrl(baseUrl) ?? DefaultBaseUrl),
            // Short timeout for control endpoints. SSE streaming uses its own
            // long-lived request that bypasses this.
            Timeout = TimeSpan.FromSeconds(15),
        };
        _ownsHttp = true;
    }

    public TrainingService(ConfigService config)
        : this(config.TrainingApiBaseUrl)
    {
    }

    private static string? NormalizeUrl(string? url)
    {
        if (string.IsNullOrWhiteSpace(url)) return null;
        var trimmed = url.Trim();
        return Uri.TryCreate(trimmed, UriKind.Absolute, out _) ? trimmed : null;
    }

    // For tests / DI: caller-supplied HttpClient (BaseAddress must be set).
    public TrainingService(HttpClient http)
    {
        if (http.BaseAddress is null)
            throw new ArgumentException("HttpClient.BaseAddress must be set", nameof(http));
        _http = http;
        _ownsHttp = false;
    }

    public async Task<TrainingStatus> GetStatusAsync(CancellationToken ct = default)
    {
        var resp = await _http.GetAsync("/api/status", ct).ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();
        var json = await resp.Content.ReadAsStringAsync(ct).ConfigureAwait(false);
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        return new TrainingStatus(
            State: GetString(root, "state") ?? "unknown",
            Stage: GetString(root, "stage") ?? string.Empty,
            Error: GetString(root, "error") ?? string.Empty,
            Running: root.TryGetProperty("running", out var r) && r.ValueKind == JsonValueKind.True);
    }

    public Task<bool> StartTrainingAsync(QuickTrainConfig config, CancellationToken ct = default)
    {
        var body = new
        {
            data_dir = config.DataDir,
            output_dir = config.OutputDir,
            epochs = config.Epochs,
            batch_size = config.BatchSize,
            augment = config.Augment,
        };
        return PostStartAsync("/api/train", body, ct);
    }

    public Task<bool> StartFullPipelineAsync(FullPipelineConfig config, CancellationToken ct = default)
    {
        var body = new
        {
            species_list = config.SpeciesList,
            api_key = config.ApiKey,
            skip_download = config.SkipDownload,
            output_dir = config.OutputDir,
            noise_dir = config.NoiseDir,
            max_recordings = config.MaxRecordings,
            quality_min = config.QualityMin,
            min_conf = config.MinConf,
            epochs = config.Epochs,
            batch_size = config.BatchSize,
            augment = config.Augment,
        };
        return PostStartAsync("/api/full-pipeline", body, ct);
    }

    public Task<bool> DownloadSpeciesAsync(SpeciesDownloadConfig config, CancellationToken ct = default)
    {
        var body = new
        {
            species = config.Species,
            api_key = config.ApiKey,
            output_dir = config.OutputDir,
            max_recordings = config.MaxRecordings,
            quality_min = config.QualityMin,
            min_conf = config.MinConf,
        };
        return PostStartAsync("/api/download-species", body, ct);
    }

    public async Task<bool> CancelAsync(CancellationToken ct = default)
    {
        using var resp = await _http.PostAsync("/api/cancel", content: null, ct).ConfigureAwait(false);
        if (resp.StatusCode == HttpStatusCode.Conflict) return false;
        resp.EnsureSuccessStatusCode();
        return true;
    }

    public async Task<IReadOnlyList<string>> GetSpeciesAsync(CancellationToken ct = default)
    {
        var resp = await _http.GetAsync("/api/species", ct).ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();
        var json = await resp.Content.ReadAsStringAsync(ct).ConfigureAwait(false);
        using var doc = JsonDocument.Parse(json);
        var list = new List<string>();
        if (doc.RootElement.TryGetProperty("species", out var arr) && arr.ValueKind == JsonValueKind.Array)
        {
            foreach (var el in arr.EnumerateArray())
            {
                if (el.ValueKind == JsonValueKind.String)
                {
                    var s = el.GetString();
                    if (!string.IsNullOrEmpty(s)) list.Add(s);
                }
            }
        }
        return list;
    }

    public async Task<IReadOnlyList<OutputArtifact>> ListOutputsAsync(CancellationToken ct = default)
    {
        var resp = await _http.GetAsync("/api/outputs", ct).ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();
        var json = await resp.Content.ReadAsStringAsync(ct).ConfigureAwait(false);
        using var doc = JsonDocument.Parse(json);
        var list = new List<OutputArtifact>();
        if (doc.RootElement.TryGetProperty("files", out var files) && files.ValueKind == JsonValueKind.Array)
        {
            foreach (var f in files.EnumerateArray())
            {
                var name = GetString(f, "name");
                if (string.IsNullOrEmpty(name)) continue;
                long size = 0;
                if (f.TryGetProperty("size", out var sz) && sz.ValueKind == JsonValueKind.Number)
                    sz.TryGetInt64(out size);
                list.Add(new OutputArtifact(name, size));
            }
        }
        return list;
    }

    public async Task DownloadOutputAsync(string filename, string destPath, CancellationToken ct = default)
    {
        if (string.IsNullOrEmpty(filename))
            throw new ArgumentException("filename required", nameof(filename));

        using var resp = await _http
            .GetAsync($"/api/outputs/{Uri.EscapeDataString(filename)}",
                HttpCompletionOption.ResponseHeadersRead, ct)
            .ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();

        await using var src = await resp.Content.ReadAsStreamAsync(ct).ConfigureAwait(false);
        await using var dst = new FileStream(destPath, FileMode.Create, FileAccess.Write, FileShare.None);
        await src.CopyToAsync(dst, ct).ConfigureAwait(false);
    }

    public async Task<byte[]> DownloadOutputBytesAsync(string filename, CancellationToken ct = default)
    {
        if (string.IsNullOrEmpty(filename))
            throw new ArgumentException("filename required", nameof(filename));

        using var resp = await _http
            .GetAsync($"/api/outputs/{Uri.EscapeDataString(filename)}", ct)
            .ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsByteArrayAsync(ct).ConfigureAwait(false);
    }

    public async Task StreamProgressAsync(IProgress<TrainingEvent> sink, CancellationToken ct = default)
    {
        // SSE is a long-lived GET. We can't use _http.Timeout for this — the
        // server only emits keepalive comments every 30s. Build a per-request
        // message so we can opt out of the client timeout.
        using var req = new HttpRequestMessage(HttpMethod.Get, "/api/progress");

        using var resp = await _http
            .SendAsync(req, HttpCompletionOption.ResponseHeadersRead, ct)
            .ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();

        await using var stream = await resp.Content.ReadAsStreamAsync(ct).ConfigureAwait(false);
        using var reader = new StreamReader(stream);

        while (!ct.IsCancellationRequested)
        {
            var line = await reader.ReadLineAsync(ct).ConfigureAwait(false);
            if (line is null) break;             // server closed
            if (line.Length == 0) continue;      // SSE dispatch boundary
            if (line.StartsWith(":")) continue;  // comment / keepalive

            // Flask format: "data: <payload>". Single-line events only.
            const string prefix = "data: ";
            if (!line.StartsWith(prefix)) continue;
            var payload = line.Substring(prefix.Length);

            var evt = ParseEvent(payload);
            if (evt is not null) sink.Report(evt);
            if (evt is TrainingDoneEvent) break;
        }
    }

    private static TrainingEvent? ParseEvent(string payload)
    {
        // Try JSON first; if not JSON, treat as a plain log line.
        if (!LooksLikeJson(payload))
            return new TrainingLogEvent(payload);

        JsonDocument doc;
        try { doc = JsonDocument.Parse(payload); }
        catch (JsonException) { return new TrainingLogEvent(payload); }

        using (doc)
        {
            var root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return new TrainingLogEvent(payload);

            // Terminal sentinel — pushed after every job.
            if (root.TryGetProperty("done", out var d) && d.ValueKind == JsonValueKind.True)
                return new TrainingDoneEvent();

            // Status update from _set_status.
            if (root.TryGetProperty("_status", out var st) && st.ValueKind == JsonValueKind.String)
            {
                return new TrainingStatusEvent(
                    State: st.GetString() ?? string.Empty,
                    Stage: GetString(root, "stage") ?? string.Empty,
                    Error: GetString(root, "error") ?? string.Empty);
            }

            var stage = GetString(root, "stage");

            // Epoch tick: stage="training" + numeric "epoch".
            if (!string.IsNullOrEmpty(stage)
                && root.TryGetProperty("epoch", out var ep) && ep.ValueKind == JsonValueKind.Number)
            {
                int epoch = ep.GetInt32();
                int total = 0;
                if (root.TryGetProperty("total", out var tp) && tp.ValueKind == JsonValueKind.Number)
                    tp.TryGetInt32(out total);
                double? auc = null;
                if (root.TryGetProperty("val_auc", out var av))
                {
                    if (av.ValueKind == JsonValueKind.Number && av.TryGetDouble(out var aucVal))
                        auc = aucVal;
                    else if (av.ValueKind == JsonValueKind.String
                             && double.TryParse(av.GetString(),
                                 System.Globalization.NumberStyles.Float,
                                 System.Globalization.CultureInfo.InvariantCulture,
                                 out var aucStr))
                        auc = aucStr;
                }
                return new TrainingEpochEvent(stage!, epoch, total, auc);
            }

            // Stage transition: stage + status.
            var status = GetString(root, "status");
            if (!string.IsNullOrEmpty(stage) && !string.IsNullOrEmpty(status))
                return new TrainingStageEvent(stage!, status!);

            // Anything else: surface raw JSON as a log line.
            return new TrainingLogEvent(payload);
        }
    }

    private async Task<bool> PostStartAsync(string path, object body, CancellationToken ct)
    {
        using var resp = await _http.PostAsJsonAsync(path, body, ct).ConfigureAwait(false);
        if (resp.StatusCode == HttpStatusCode.Conflict) return false;
        if (!resp.IsSuccessStatusCode)
        {
            // Surface the API's error message if we got one, otherwise default.
            var text = await SafeReadBodyAsync(resp, ct).ConfigureAwait(false);
            throw new HttpRequestException(
                $"{path} failed: {(int)resp.StatusCode} {resp.ReasonPhrase}. {text}");
        }
        return true;
    }

    private static async Task<string> SafeReadBodyAsync(HttpResponseMessage resp, CancellationToken ct)
    {
        try { return await resp.Content.ReadAsStringAsync(ct).ConfigureAwait(false); }
        catch { return string.Empty; }
    }

    private static string? GetString(JsonElement el, string name)
        => el.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String
            ? v.GetString()
            : null;

    private static bool LooksLikeJson(string s)
    {
        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            if (c == ' ' || c == '\t') continue;
            return c == '{' || c == '[';
        }
        return false;
    }

    public void Dispose()
    {
        if (_ownsHttp) _http.Dispose();
    }
}
