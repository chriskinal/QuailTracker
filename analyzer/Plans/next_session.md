# Next session — native Training & Evaluation UIs

Pick up here when you're ready to fill in the two placeholder sub-tabs
under **Modeling** with native Avalonia/MVVM views backed by the existing
training-container REST API.

---

## Prompt to paste at the start of tomorrow's session

> I want to continue work on the QuailTracker analyzer. Read
> `analyzer/Plans/next_session.md` for the handoff context. We're on
> branch `feature/modeling-tabs`, last commit `a0eb7d4` reorganized the
> seven flat tabs into four top-level groups with sub-tabs. The Modeling
> tab has Data / Training / Evaluation sub-tabs; Training and Evaluation
> are currently placeholders. Today's goal: build the native Training UI
> backed by the training container's REST API at `http://localhost:5050`.
> Start by reading the plan in that file, confirm the approach, then
> begin with Phase 1 (TrainingService HttpClient wrapper).

---

## Where we are

**Branch:** `feature/modeling-tabs`
**HEAD:** `a0eb7d4` (Reorganize tabs into 4 top-level groups with sub-tabs)
**Parent of branch:** `main` at `a94b528`

**Working state:**
- Avalonia 12 upgrade complete (4 phases on `main`).
- Tab reorg complete (this branch).
- Modeling tab has three sub-tabs: Data (real, existing TrainingDataView),
  Training (placeholder), Evaluation (placeholder).
- Training container exposes `/api/...` REST endpoints — see "API surface" below.
- Flask web UI at `http://localhost:5050` works as the interim fallback;
  the placeholder tabs explain how to start it (`docker compose up` from `training/`).

## Why the native UIs are next

User feedback at end of last session: "The web UI makes you scroll to the
bottom and using a separate browser just makes jumping between the app
and browser a pain." That's the motivation — getting training and
evaluation in-app eliminates the context switch.

## API surface (already exists, no container changes needed)

Defined in `training/webapp/app.py`:

```
GET  /api/status             current training state
GET  /api/progress           training progress (epoch, loss, etc.)
POST /api/train              start a training run
POST /api/cancel             cancel an in-flight run
POST /api/download-species   fetch xeno-canto recordings
POST /api/full-pipeline      end-to-end download → train
GET  /api/outputs            list output artifacts (model files, charts)
GET  /api/outputs/<file>     download a specific artifact
```

The Flask UI consumes these same endpoints. We're just adding a second
client — an MVVM-native one in the analyzer.

## Phased plan

### Phase 1 — `TrainingService` (HttpClient wrapper)

`analyzer/Shared/Services/TrainingService.cs` + interface
`ITrainingService.cs`. Methods, all `Task<...>`-returning, all accepting
`CancellationToken`:

- `GetStatusAsync(ct)` → `TrainingStatus` record (idle / running / error / etc.)
- `GetProgressAsync(ct)` → `TrainingProgress` record (epoch, loss, accuracy, AUC)
- `StartTrainingAsync(TrainingConfig config, ct)`
- `CancelAsync(ct)`
- `DownloadSpeciesAsync(string species, int count, ct)`
- `RunFullPipelineAsync(PipelineConfig config, ct)`
- `ListOutputsAsync(ct)` → list of `OutputArtifact` records
- `DownloadOutputAsync(string filename, string destPath, ct)` — streams
  to disk (don't load big artifacts into memory).

Configuration: base URL is `http://localhost:5050` (default; persist in
`ConfigService.TrainingApiBaseUrl` so users can point at a remote
container if they ever want to).

Records (immutable; one-way data flow per the threading model):

```csharp
public sealed record TrainingStatus(string State, string? Message);
public sealed record TrainingProgress(
    int Epoch, int TotalEpochs, double Loss, double Accuracy,
    double? AucValidation, DateTime UpdatedAt);
public sealed record TrainingConfig(...); // fields per the Flask form
public sealed record OutputArtifact(string FileName, long SizeBytes, DateTime ModifiedAt);
```

Use `System.Net.Http.HttpClient` (one instance held by the service, not
per-call) and `System.Text.Json` for (de)serialization.

**Acceptance:** `TrainingService` is unit-testable in principle (could
hit a stubbed HTTP endpoint), all methods accept and respect a
`CancellationToken`, no UI knowledge in the class.

### Phase 2 — Training sub-tab native UI

Replace the Training placeholder in `MainWindow.axaml` with
`<views:ModelingTrainingView DataContext="{Binding ModelingTrainingViewModel}" />`.

`ModelingTrainingViewModel`:
- `[ObservableProperty]` for hyperparameters (epochs, learning rate, batch
  size, augmentation toggles — match what the Flask form exposes).
- `[ObservableProperty]` for runtime: `IsRunning`, `CurrentEpoch`,
  `TotalEpochs`, `CurrentLoss`, `CurrentAccuracy`, `LogTail` (last N
  lines), `LastError`.
- `[RelayCommand]` `StartTraining`, `Cancel`.
- Polling loop using `CancellationHelpers.Replace` (Phase 4 of the
  MVVM/threading refactor — already in the codebase): every 1s while
  running, call `_trainingService.GetProgressAsync(ct)` and update
  observable state.
- Container reachability indicator: poll `/api/status` every few
  seconds when idle to set `IsContainerReachable`. Disables Start
  button + shows hint when down.

`ModelingTrainingView`:
- Hyperparameter form (left).
- Status panel (right): big progress bar bound to `CurrentEpoch / TotalEpochs`,
  loss/accuracy readouts, scrolling log.
- Below: a row of action buttons (Start / Cancel) plus the green/red
  container status dot.

**Acceptance:** start a training run from the analyzer, watch live
progress, cancel mid-run. No browser needed.

### Phase 3 — Evaluation sub-tab native UI

Similar shape. `ModelingEvaluationViewModel` exposes:
- `Outputs` ObservableCollection of `OutputArtifact`.
- `[ObservableProperty]` `SelectedOutput`.
- `[RelayCommand]` `Refresh` (re-list), `Download(string filename)`.
- Optionally: load and parse a known output (e.g. `metrics.json`) into
  bound chart data.

`ModelingEvaluationView`:
- Charts area (training/validation accuracy + AUC over epochs). The
  Flask UI presumably already produces these as PNG; for the native
  UI, either embed them as images via `IImage` from
  `DownloadOutputAsync`, or render natively from `metrics.json` using
  Avalonia chart libs (`OxyPlot.Avalonia` or `LiveChartsCore.SkiaSharpView.Avalonia`).
- Output file list (DataGrid) with name + size + Download button per row.
- Buttons to download the .tflite / .onnx model artifacts to a
  user-chosen folder via `IStorageProvider.OpenFolderPickerAsync`.

**Acceptance:** open Evaluation tab after a training run, see charts,
download model files into a chosen folder.

### Phase 4 — Polish (optional, do as time allows)

- Persist `TrainingApiBaseUrl` in `ConfigService` with a small settings
  field.
- "Open Flask UI" button as a fallback link (opens
  `http://localhost:5050` in default browser via `Process.Start`).
- "Start container" button that runs `docker compose up -d` from
  `training/` via `Process.Start` for one-click setup. Watch out for
  cross-platform `docker` location.
- Progress notification when container goes from unreachable to
  reachable.

## Open questions to settle before Phase 1

1. **Charting library for Evaluation.** Two reasonable picks:
   - `OxyPlot.Avalonia` — established, simple API, good for line charts.
   - `LiveChartsCore.SkiaSharpView.Avalonia` — modern, animated, better
     for live updates during training.
   Both are MIT-ish. If the training run page wants live loss/accuracy
   curves (Phase 2 stretch), LiveCharts is better.
2. **Hyperparameter form scope.** Look at `training/webapp/templates/index.html`
   to see exactly what fields the Flask form exposes. Mirror those in
   the native form. Sticking to the same fields keeps the API surface
   identical.
3. **Polling vs. server-sent events.** Polling is simpler. Flask
   doesn't speak WebSocket today. Polling at 1s during training is
   fine. If progress feels chunky, can swap to SSE later (Flask side
   has a small extension for it).

## Files to read at session start

- `analyzer/Plans/next_session.md` — this file.
- `analyzer/Plans/threading_model.md` — the cancellation / single-writer
  rules. Important for Phase 2's polling loop.
- `analyzer/Shared/Services/CancellationHelpers.cs` — the Replace pattern.
- `analyzer/Shared/Services/AppStateService.cs` — single-source-of-truth
  example to mirror for any cross-VM state we need.
- `training/webapp/app.py` — actual REST endpoint shapes and request /
  response payloads.
- `training/webapp/templates/index.html` — what fields the existing
  form has, so the native one can match.

## Won't-do

- Not embedding the Flask UI in a WebView. Same reason we ruled it out
  in the Map tab: tab virtualization tears it down on every visit, and
  the user already said the browser bounce is painful.
- Not changing the training container's API. Native client only.
- Not auto-starting the docker container by default. Keep that as
  optional polish in Phase 4 — many users will want to control it
  themselves.

## Branch hygiene

- Stay on `feature/modeling-tabs` for Phases 1–3. Each phase is one
  commit (or two if it gets large).
- When the native UIs are functional, merge into `main` as a single
  feature branch with all the work.
- Push afterward. We're currently 1 commit ahead of `origin` on the
  branch (the reorg).
