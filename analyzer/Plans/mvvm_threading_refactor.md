# Analyzer — MVVM & Threading refactor plan

Unified phased plan addressing the findings from the MVVM audit and the
threading audit (run against `analyzer/Plans/threading_model.md`).
The two audits overlap heavily; each phase below is sized to ship as one PR
and fully resolves a named slice of findings.

## Conventions

- **Findings codes:** `MVVM-H1` etc. = the audit ordinal. Each phase lists which
  findings it closes so we can check coverage at the end.
- **"Done"** = code merged to `main`, app launches, manual smoke-test below
  passes, no regression in the path the phase didn't touch.
- **Threading rules** are the non-negotiables in `threading_model.md`. Any
  change in any phase must respect them — even if the phase is nominally
  about MVVM.

---

## Phase 1 — Single source of truth for model state

**Closes:** MVVM-H1, MVVM-H2, Threading-C1.
**Also fixes the user-reported "0 detections / Unknown Species 1473" bug.**

### Problem

`IsModelLoaded` and `ModelPath` are duplicated as `[ObservableProperty]`
fields on `ProcessingViewModel.cs:50` and `SingleAnalysisViewModel.cs:84`,
while the actual state lives on the shared `BirdNetService` instance.
`AppStateService` exists but is only consumed by `SingleAnalysisViewModel`.
The Process tab's local `IsModelLoaded` defaults to `false`, so the user
must click "Load Model…" again — that re-load nulls the labels array on the
shared service, and bulk processing returns `Unknown Species 1473` even
though Single Analysis still shows real names.

### Scope

1. **`AppStateService`** is the canonical owner of `IsModelLoaded` and
   `ModelPath`. (Already declared there — just promote it to the only writer.)
2. **`BirdNetService.LoadModelAsync`** stays the only place that loads a model;
   on success its `await` continuation calls
   `appState.ApplyModelLoaded(path)` on the UI thread (single-writer rule).
3. **`SingleAnalysisViewModel`** removes its local `IsModelLoaded` /
   `ModelPath` `[ObservableProperty]` fields. Properties become read-only
   projections of `AppStateService`. Same for the auto-load flow.
4. **`ProcessingViewModel`** takes `AppStateService` in its constructor.
   Same projection pattern. Local fields removed.
5. **`MainWindowViewModel`** wires `AppStateService` into `ProcessingViewModel`
   the same way it does for `SingleAnalysisViewModel`.
6. **`ProcessingView.axaml`** — "Load Model…" button now hides when the model
   is already loaded (`IsVisible="{Binding !IsModelLoaded}"` already in place,
   just needs to bind to the projected value).
7. **Defensive:** if `BirdNetService.FindLabels` returns null on a load,
   throw a clear `InvalidOperationException("Labels file not found near {modelPath}")`.
   Today it silently sets `_labels = null`, which is exactly the foot-gun that
   produced the user-reported bug.

### Files touched

- `analyzer/Shared/Services/AppStateService.cs` — add `ApplyModelLoaded(path)`.
- `analyzer/Shared/Services/BirdNetService.cs` — throw on null labels;
  callers (the VMs) call `appState.ApplyModelLoaded` on success.
- `analyzer/Shared/ViewModels/SingleAnalysisViewModel.cs` — drop local fields,
  project from `AppStateService`. Update `LoadModelAsync` and
  `TryAutoLoadModelAsync` continuations.
- `analyzer/Shared/ViewModels/ProcessingViewModel.cs` — accept
  `AppStateService`; drop local fields; project; update `LoadModelAsync`.
- `analyzer/Shared/ViewModels/MainWindowViewModel.cs` — pass `appState` into
  `ProcessingViewModel`.
- `analyzer/Shared/Views/ProcessingView.axaml` (verify binding still works
  after the projection rename, no logic change expected).

### Acceptance

- Open app cold → Single Analysis tab shows model loaded → switch to Process
  tab → "Load Model…" button is **hidden**, file list pickers are enabled.
- Run Single File analysis → 4 Bobwhite detections with correct names.
- Without re-loading the model, run Bulk processing on the same file → 4
  Bobwhite detections with correct names.
- Force the failure: pick a model file in a directory with no labels.txt →
  clear error toast / status message; no silent "Unknown Species" later.

### Risk

Low. The change is mostly mechanical (delete two fields, add a projection),
and the defensive labels check fails loudly instead of corrupting state.

---

## Phase 2 — Models implement Model→VM notifications

**Closes:** MVVM-M4 (and fixes the "scroll to see bearings" UX bug).

### Problem

The MS MVVM diagram has two notification flows:

```
View ←→ ViewModel ←→ Model
              (notify)        (notify)
```

`ViewModel → View` notifications work (Toolkit's `[ObservableProperty]` everywhere).
`Model → ViewModel` notifications are **broken across the codebase** — every
mutable model class is a plain POCO with auto-properties, no
`INotifyPropertyChanged`. When a service mutates a model that's already in a
bound collection, the UI doesn't know to refresh. Today's user-visible
symptom: bearings don't appear in the DataGrid until the user scrolls.

### Audit

Out of 9 model files in `Shared/Models/`, only `TrainingClip` is observable.
The rest are POCOs. Of those POCOs:

- **Must become observable** (mutated after binding):
  - `Detection` — `BearingDeg`, `TdoaSamples`, `TdoaConfidence`,
    `LocalizationId`, `IsSelected` all get set after the detection is in
    `ObservableCollection<Detection>`.
- **Should become observable** (probably mutated after binding):
  - `Station` — `RecordingCount` / `LastRecordingTime` update as new audio
    files import. Verify by checking call sites; if confirmed, convert.
- **Verify before converting** (might be POCO-safe):
  - `SurveyConfig` — depends on whether the Population view binds two-way
    to its properties directly or wraps it in a VM. If direct, INPC is
    required for round-tripping.
- **Stay POCO** (no post-binding mutation):
  - `AudioFile` — fully populated by `LoadFileAsync`, then read-only.
  - `Localization` — computed once, replaced if recomputed.
  - `PopulationEstimate` / `DailyCount` / `WeatherData` — read-only results.

### Scope

1. **`Detection`** — convert to `ObservableObject` with `[ObservableProperty]`
   on every mutable field. Computed display properties
   (`DisplayName`, `ConfidencePercent`, `BearingDisplay`) get
   `[NotifyPropertyChangedFor(...)]` annotations on the source backing fields
   so they refresh transitively (`BearingDeg` change → `BearingDisplay`
   notifies).
2. **`Station`** — same conversion, after a quick call-site check confirms
   `RecordingCount`/`LastRecordingTime` are mutated post-binding.
3. **`SurveyConfig`** — check Population view bindings; convert only if
   two-way bound to UI.
4. **Documentation** — add a one-paragraph note to
   `Plans/threading_model.md` (or a new `Plans/mvvm_pattern.md`) clarifying
   the rule: *"any model whose properties mutate after the model is added
   to a bound collection must implement `INotifyPropertyChanged`"*.

### Files touched

- `analyzer/Shared/Models/Detection.cs`
- `analyzer/Shared/Models/Station.cs` (pending verification)
- `analyzer/Shared/Models/SurveyConfig.cs` (pending verification)
- (No ViewModel or View changes expected — bindings remain the same; the
  models just start sending notifications.)

### Acceptance

- Run analysis on a stereo file → bearings appear in the DataGrid
  **immediately**, no scroll needed.
- Toggle a detection's `IsSelected` → UI reflects it without a refresh.
- (If Station converted) Import a new file with a known station → station
  list `RecordingCount` updates without manual refresh.

### Risk

Low. Conversion is mechanical. Watch for serialization compatibility if
any model is JSON-persisted with strict shape expectations
(`PopulationViewModel` import/export uses `Detection`/`Localization` JSON —
verify the converted classes still serialize identically; `[ObservableProperty]`
generates the same getter/setter shape).

---

## Phase 3 — Async I/O discipline

**Closes:** MVVM-H3, MVVM-H4, MVVM-H5, MVVM-H6, Threading-H2, Threading-M2.

### Problem

Three places do synchronous file I/O on the UI thread, and two views call a
static export service from code-behind, bypassing the VM:

- `PopulationViewModel.ExportResults:438` — `File.WriteAllText` sync.
- `PopulationViewModel.ImportPrevious:447` — `File.ReadAllText` sync.
  Both entered from `PopulationView.axaml.cs:54, 78` code-behind.
- `ConfigService.Load:56` and `Save:73` — sync `File.Read/WriteAllText`.
  `Load` runs from `MainWindowViewModel` ctor at startup; `Save` runs from
  `MainWindow.axaml.cs:76` `OnClosing`. Slow disk → hang.
- `SingleAnalysisView.axaml.cs:350-352` and `ProcessingView.axaml.cs:83-85`
  call `DetectionExporter.WriteBirdNetCsv/WriteRavenTable` directly.
  Bypasses VM, no async, no progress, no error surface.

### Scope

1. **`PopulationViewModel`**: `ExportResults` → `ExportResultsCommand` (`async Task`),
   wraps work in `Task.Run`. Same for `ImportPrevious` → `ImportPreviousCommand`.
   `PopulationView.axaml.cs` awaits the command via the file picker code.
2. **`ConfigService`**: add `LoadAsync` / `SaveAsync`. Keep sync `Load` for
   the cold-startup path *only* (acceptable for one-time small JSON), or move
   load into an `InitializeAsync` step on `MainWindowViewModel`. `Save` becomes
   async; `MainWindow.OnClosing` becomes `async void` and `await`s it (this is
   one of the legitimate `async void` cases — UI event handler).
3. **`DetectionExporter` calls in views**: each VM gets an
   `ExportDetectionsCommand(string path)` that wraps the static method in
   `Task.Run` and updates a status property when done. Code-behind picks the
   file, then `await vm.ExportDetectionsCommand.ExecuteAsync(path)`.

### Files touched

- `analyzer/Shared/ViewModels/PopulationViewModel.cs`
- `analyzer/Shared/Views/PopulationView.axaml.cs`
- `analyzer/Shared/Services/ConfigService.cs`
- `analyzer/Shared/ViewModels/MainWindowViewModel.cs` (init flow)
- `analyzer/Shared/Views/MainWindow.axaml.cs` (OnClosing)
- `analyzer/Shared/ViewModels/SingleAnalysisViewModel.cs` (new export command)
- `analyzer/Shared/ViewModels/ProcessingViewModel.cs` (new export command)
- `analyzer/Shared/Views/SingleAnalysisView.axaml.cs` (await command)
- `analyzer/Shared/Views/ProcessingView.axaml.cs` (await command)

### Acceptance

- Export a population JSON in the Population view → status shows progress,
  UI stays responsive while writing.
- Import a population JSON → same.
- Quit the app while a save is pending → window closes after save completes,
  no dropped writes (or background task drains in app shutdown — call out
  whichever we pick).
- Export detections from Single Analysis or Process view → progress, no UI
  freeze on a large CSV.

### Risk

Medium. `OnClosing` becoming `async void` is correct but slightly subtle —
make sure `Avalonia` understands the close should wait. If we hit close-cancel
issues, fallback is sync `Save()` on close (small file, accept the freeze)
and async everywhere else.

---

## Phase 4 — Cancellation discipline

**Closes:** Threading-H3, Threading-M1.

### Problem

Several fire-and-forget async sites have no `CancellationToken` and no way
to drop stale tasks when the user changes their mind:

- `SingleAnalysisViewModel.cs:147` — `_ = TryAutoLoadModelAsync()`.
- `SingleAnalysisViewModel.cs:342, 344, 383` —
  `_ = EstimateNoiseProfileThenRegenerateAsync()`,
  `_ = FullRegenerateSpectrogramAsync()`,
  `_ = RenderSpectrogramFromCacheAsync()`.
  User flipping NR / changing files / playing audio rapidly stacks stale
  spectrogram regenerations.
- `MapViewModel.cs:127–131` — `partial void OnShowStationsChanged(bool) => _ = UpdateLayerVisibilityAsync()`
  (and similar for other filter changes). Toggling a filter five times
  launches five overlapping refreshes.

### Scope

1. Each VM that has fire-and-forget background work owns a `CancellationTokenSource`
   field per logical job kind (auto-load, spectrogram, layer-update). Accept
   per-VM, not a global `_cts` — different jobs cancel independently.
2. Helper extension or local method `Replace(ref CancellationTokenSource? cts)`
   that cancels and disposes the old one and returns a fresh token.
3. Each fire-and-forget call site:
   - cancel and replace the token,
   - pass the token to the async method,
   - swallow `OperationCanceledException`.
4. `Dispose` (or VM's deactivation, where applicable) cancels all in-flight tokens.

### Files touched

- `analyzer/Shared/ViewModels/SingleAnalysisViewModel.cs`
- `analyzer/Shared/ViewModels/MapViewModel.cs`
- (Possibly a small helper in `analyzer/Shared/Services/` —
  `CancellationHelpers.cs` — if the pattern repeats enough.)

### Acceptance

- Toggle NR on/off rapidly → only the latest spectrogram regeneration finishes;
  earlier ones cancel cleanly without UI flicker.
- Toggle map filter checkboxes rapidly → no overlapping refresh; final state
  matches the last toggle.
- Switch files in Single Analysis mid-spectrogram-render → old render
  cancels, new file's render proceeds.

### Risk

Low. `OperationCanceledException` from inside a `_ = ...Async()` was already
unhandled — we're explicit about it now.

---

## Phase 5 — View↔Model decoupling (remaining)

**Closes:** MVVM-M1, MVVM-M2, MVVM-M3.

### Problem

Three places have view/VM boundary issues that don't depend on Phase 2:

- `LocalizationViewModel.cs:154–161, 209–215` — VM mutates `Detection.LocalizationId`
  in a manual loop. With Phase 2 done, this is a single observable property
  set; the VM can still drive it but the loop pattern is suspect — extract
  to a `LocalizationLinkingService` for traceability.
- `SingleAnalysisView.axaml.cs:95–117` — code-behind redraws Canvas
  detection markers / freq labels / time labels by hand on collection-change
  events. UI logic that should be `ItemsControl` + a small attached behavior,
  driven by VM-bound data.
- `MainWindow.axaml.cs:39–76` — code-behind reads/writes `ConfigService`
  for window geometry directly. Should be a `WindowState` service or a VM
  command.

### Scope

1. New `LocalizationLinkingService` (or method on an existing service)
   that takes the detections + the localization id and applies the link
   in one place. VM calls the service; service raises an event the VM
   subscribes to.
2. Replace the manual Canvas updates in `SingleAnalysisView.axaml.cs` with
   either:
   - `ItemsControl` over a VM-bound `ObservableCollection<DetectionMarker>`
     with an `ItemsPanel` of `Canvas`, plus a converter for positioning;
     OR
   - A small `AttachedBehavior` that reacts to `INotifyCollectionChanged`
     on a bound collection.
   Pick the simpler one for the use case (probably `ItemsControl`).
3. `MainWindow.axaml.cs` window-geometry persistence:
   - Move `Save()` calls into a `WindowState` service that the VM owns.
   - Code-behind only raises `SaveWindowState` on the relevant events, the
     VM's command does the rest.

### Files touched

- `analyzer/Shared/ViewModels/LocalizationViewModel.cs`
- New `analyzer/Shared/Services/LocalizationLinkingService.cs` (or extend an
  existing one).
- `analyzer/Shared/Views/SingleAnalysisView.axaml(.cs)` — Canvas refactor.
- `analyzer/Shared/ViewModels/MainWindowViewModel.cs` — window-state command.
- `analyzer/Shared/Views/MainWindow.axaml.cs` — drop direct ConfigService
  access; raise commands.

### Acceptance

- Localization linking still works end-to-end; the link is set by the service,
  the VM observes the change, the view updates from binding.
- Detection markers still appear on the spectrogram, with no Canvas update
  code in `SingleAnalysisView.axaml.cs`.
- Resize / move / maximize the window, quit, restart → window state restores.
  Code-behind has no `ConfigService` references.

### Risk

Medium. Canvas refactor can be visually fiddly (positioning math moves into
a converter). Plan to take screenshots before/after to verify no regression.

---

## Phase 6 — Cleanup

**Closes:** MVVM-L1, MVVM-L2.

### Scope

1. Move `HalfValueConverter` from
   `analyzer/Shared/ViewModels/SingleAnalysisViewModel.cs:503–519` to
   `analyzer/Shared/Converters/HalfValueConverter.cs`. Update XAML namespaces.
2. Introduce a DI container (`Microsoft.Extensions.DependencyInjection`)
   in `Desktop/Program.cs` and `analyzer/Shared/App.axaml.cs`. Register all
   services as singletons (they currently are — just `new`'d in `MainWindowViewModel`).
   `MainWindowViewModel`'s no-arg ctor goes away; the DI container resolves
   it. Optional but unlocks unit-testable VMs.

### Files touched

- `analyzer/Shared/Converters/HalfValueConverter.cs` (new).
- `analyzer/Shared/ViewModels/SingleAnalysisViewModel.cs` (removed converter).
- `analyzer/Shared/Views/*.axaml` (namespace import update).
- `analyzer/Desktop/Program.cs` — DI bootstrap.
- `analyzer/Shared/App.axaml.cs` — resolve `MainWindow` from container.

### Acceptance

- App starts identically; nothing functional changed.
- Adding a unit test for `MainWindowViewModel` is now possible (write one
  trivial test as proof; doesn't have to land in this PR).

### Risk

Low. Pure refactor. DI part can be deferred if we don't want test plumbing yet.

---

## Order and sequencing

**Recommended:** 1 → 2 → 3 → 4 → 5 → 6.

- Phase 1 (DONE) — single source of truth for service state.
- Phase 2 — Model→VM notifications. Foundational architecture; closes a
  whole class of subtle staleness bugs (the bearing-scroll one being the
  reported example).
- Phase 3 — async I/O. Independent of 2.
- Phase 4 — cancellation. Independent of 2 and 3.
- Phase 5 — view↔model decoupling. Reads cleaner after Phase 2 lands
  (LocalizationVM mutation loops are simpler against observable detections).
- Phase 6 — cleanup. Anytime.

## Tracking

Each finding from the audits maps to a phase:

| Finding         | Phase | Status |
|-----------------|-------|--------|
| MVVM-H1         | 1     | DONE   |
| MVVM-H2         | 1     | DONE   |
| MVVM-H3         | 3     | TODO   |
| MVVM-H4         | 3     | TODO   |
| MVVM-H5         | 3     | TODO   |
| MVVM-H6         | 3     | TODO   |
| MVVM-M1         | 5     | TODO   |
| MVVM-M2         | 5     | TODO   |
| MVVM-M3         | 5     | TODO   |
| MVVM-M4         | 2     | TODO   |
| MVVM-L1         | 6     | TODO   |
| MVVM-L2         | 6     | TODO   |
| Threading-C1    | 1     | DONE   |
| Threading-H1    | 3     | TODO   |
| Threading-H2    | 3     | TODO   |
| Threading-H3    | 4     | TODO   |
| Threading-M1    | 4     | TODO   |
| Threading-M2    | 3     | TODO   |
