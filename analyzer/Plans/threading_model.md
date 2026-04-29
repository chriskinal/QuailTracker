# QuailTracker Analyzer — Threading model

Adapted from the AgValoniaGPS pattern (`AgValoniaGPS3/Plans/threading_model.svg`).
Same principles, applied to an event-driven desktop tool instead of a 10 Hz GPS cycle.

## Three lanes

### UI thread (Avalonia dispatcher)

- Bindings, rendering, user input. Single-threaded by Avalonia contract.
- **Reads** from observable state: `AppStateService`, `ObservableCollection<AudioFile>`, `ObservableCollection<Detection>`, the per-VM bound properties.
- **Writes** to observable state happen here and **only** through a small set of well-named "apply" methods that take an immutable result snapshot:
  - `ApplyAudioFilesLoaded(IReadOnlyList<AudioFile>)`
  - `ApplyAnalysisResult(IReadOnlyList<Detection>)`
  - `ApplyExportResult(...)`
  - etc.
- User input (button clicks, hotkeys) routes through VM commands which **enqueue intents** on a service, then return immediately. The command does not do work; it asks for work.

### Job worker (Task.Run per job)

- Spawned by services (`BirdNetService`, `AudioFileService`, `DetectionExporter`, …) when the UI requests a job.
- **Owns all heavy work:** ONNX inference, audio decode, FLAC parsing, file I/O for export, batch loops.
- **Operates on POCO inputs and a private working set.** Never touches `ObservableObject` or `ObservableCollection` directly.
- Returns an **immutable result** (record / `IReadOnlyList<T>` / value type). The result is the only thing that crosses back to the UI thread, via:
  - `Task` completion (`await jobTask`) — the natural Avalonia pattern, since `await` resumes on the captured `SynchronizationContext`. The continuation runs on the UI thread.
  - `Dispatcher.UIThread.Post(...)` for fire-and-forget progress reports or completions whose context wasn't captured.
- **Single in-flight per job kind** where it matters (e.g., one batch analysis at a time). Use `CancellationTokenSource` + a `Task?` field on the service. New requests cancel or queue, never overlap silently.

### I/O threads (file system, ONNX runtime internals)

- File scanning, FLAC frame decode, ONNX session inference: these may use their own threads internally.
- Treat them as opaque — we don't reach in. The job worker `await`s their results.
- No domain logic on these threads. No `ObservableCollection` writes.

## Non-negotiables

1. **One writer per observable.** Each piece of observable state has exactly one place that writes it. Examples:
   - `AppStateService.IsModelLoaded` / `ModelPath`: written by an `ApplyModelLoaded(...)` method on the UI thread, called from `BirdNetService.LoadModelAsync`'s continuation. No VM writes these directly.
   - `ObservableCollection<AudioFile> AudioFiles`: written by `ApplyAudioFilesLoaded(...)` on the UI thread. No background `Add`.
   - `ObservableCollection<Detection> Detections`: written by `ApplyAnalysisResult(...)` on the UI thread.
2. **No domain work on the UI thread.** Long-running loops, file reads/writes, ML inference, audio decode all go through `Task.Run` (or are already async-all-the-way to a real I/O API).
3. **No `ObservableCollection.Add` / `Clear` from a background thread.** Either the worker returns an immutable list and the UI thread bulk-applies it, or the worker uses `Dispatcher.UIThread.Post` for incremental progress updates.
4. **No `.Result` / `.Wait()` on `Task`s in command handlers.** `await` everywhere, or it's a deadlock waiting to happen.
5. **No fire-and-forget without a token.** Every long task takes `CancellationToken`. The VM owns a `CancellationTokenSource`; cancelling a screen cancels its in-flight work.
6. **VM commands are intents, not implementations.** A command method's body should be: validate → call service → await → apply result. No file I/O, no inference, no spectrogram math inside the command.
7. **Services don't reach back into VMs.** Progress flows up via `IProgress<T>` (which Avalonia marshals correctly) or via observable state on `AppStateService`. Services never hold a reference to a VM.

## What this rules out (and why)

- A VM property setter that synchronously decodes audio. (Blocks UI; user sees a frozen window.)
- `_detections.Add(d)` inside `Parallel.ForEachAsync`. (Race; `ObservableCollection` is not thread-safe; UI bindings get torn reads.)
- `await foreach` over a service result that mutates a model in-place. (More than one writer; ordering bugs.)
- Code-behind `.axaml.cs` calling `File.ReadAllText` and then setting VM properties. (Sync I/O on UI thread; bypasses the apply funnel.)
- Two VMs each owning a copy of `IsModelLoaded`. (Today's bug. Two writers, drift, manual reload corrupts shared service state.)

## Audit hooks (what to grep / scan for)

- `Task.Run` — every site should return a snapshot, not mutate observable state from inside.
- `Parallel.ForEachAsync` / `Parallel.For*` — body must not touch observables.
- `Dispatcher.UIThread.Post` / `InvokeAsync` — used? in the right places? missing where needed?
- `ObservableCollection<...>.Add` / `Clear` / `Insert` / `Remove` — find every call site, confirm it's on UI thread.
- `.Result` / `.Wait()` / `.GetAwaiter().GetResult()` — should be zero hits in non-test code.
- `async void` — should only be event handlers in `.axaml.cs`. Anywhere else is fire-and-forget.
- `File.Read*` / `File.Write*` (sync) — every hit should be inside `Task.Run` or be the async overload.
- VM `[ObservableProperty]` fields that duplicate a service-owned value — see today's `IsModelLoaded` / `ModelPath`.

## Reference diagram

The AgValoniaGPS SVG (`AgValoniaGPS3/Plans/threading_model.svg`) captures the same idea visually.
This document is the prose form, adapted: substitute "cycle worker (10 Hz)" → "job worker (per request)",
and "GpsCycleResult snapshot" → "AnalysisResult / FileLoadResult / ExportResult snapshot".
