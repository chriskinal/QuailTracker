# Analyzer — Avalonia 11.2 → 12 upgrade + WebView/Map activation

## Why this is more than a maintenance bump

The map tab has been a placeholder all along. `MapService.cs:30` is
documented as a stub. `MapView.axaml.cs:31–33` says "WebView initialization
would happen here ... when platform-specific WebView support is added."
`Shared/WebContent/cesium.html` is authored but never loads into anything
real. The `Avalonia.Controls.WebView` package — newly first-class in 12 with
the `NativeWebView` control — is the missing piece.

So the upgrade is two threads, run together:

1. **Migrate analyzer to Avalonia 12** (platform bump).
2. **Wake up the Map feature** that's been blocked on this.

## What we know

From the Avalonia 12 docs (queried via the Avalonia MCP):

- **.NET requirement:** Avalonia 12 needs .NET 8+; .NET 10 recommended. Our
  `net10.0` is already fine — **no .NET bump needed**.
- **WebView API:** `Avalonia.Controls.WebView` package, `NativeWebView`
  control. Maps cleanly to current MapService surface:
  - `webView.InvokeScript(string)` → replaces `MapService.ExecuteJsAsync`.
  - `webView.WebMessageReceived` event → JS calls `invokeCSharpAction(body)`
    in the page; we parse the body and raise the appropriate
    `StationClicked` / `DetectionClicked` / `LocalizationClicked` event.
  - `webView.NavigateToString(html)` for the embedded `cesium.html`.
- **Cross-platform support:** WebView2 on Windows (preinstalled on Win11;
  runtime needed on Win10), WKWebView on macOS (preinstalled), WPE WebKit
  on Linux (`libwpewebkit-2.0-1` runtime needed). For our deployment story
  this is a non-issue on macOS/Win11.
- **Breaking changes that affect us:**
  - `DragEventArgs.Data` → `DragEventArgs.DataTransfer`. Hits both
    `ImportView.axaml.cs` (existing) and `SingleAnalysisView.axaml.cs`
    (just added).
  - `DragDrop.DoDragDrop()` → `DoDragDropAsync()`. We don't *initiate* drag
    operations, only receive — so this one doesn't bite us.
  - `OpenFileDialog` / `SaveFileDialog` / `OpenFolderDialog` removed. We
    already use `IStorageProvider.{Open,Save,OpenFolder}PickerAsync` —
    nothing to change.
  - Compiled bindings now default-on. We already have
    `<AvaloniaUseCompiledBindingsByDefault>true</AvaloniaUseCompiledBindingsByDefault>` — nothing to change.
  - Data-annotations binding plugin (which previously conflicted with
    `CommunityToolkit.Mvvm`) is now disabled by default. Net win for us.
  - `Avalonia.Diagnostics` 11.x → `AvaloniaUI.DiagnosticsSupport` 2.2.0+.
    We already use `AvaloniaUI.DiagnosticsSupport` 2.1.1 — bump to 2.2.x.
- **Packages that survive the bump unchanged (just version-bump):**
  `Avalonia`, `Avalonia.Desktop`, `Avalonia.Themes.Fluent`,
  `Avalonia.Fonts.Inter`, `Avalonia.Svg.Skia`,
  `Avalonia.Controls.DataGrid`.
- **Dead weight to drop:** `Avalonia.ReactiveUI` (zero code usage —
  confirmed via grep).

## Phase A — Pre-flight cleanup (still on 11.2)

**Goal:** isolate the Avalonia bump in Phase B from incidental cleanup.

1. Remove `Avalonia.ReactiveUI` from `Shared.csproj`.
2. Run the `migrate_diagnostics` MCP tool to confirm
   `AvaloniaUI.DiagnosticsSupport` is wired correctly (we already have it
   at 2.1.1; the tool will validate and patch if needed).
3. Build.
4. Smoke-test all 7 tabs (Single Analysis, Import, Process, Localization,
   Population, Training Data, Map).

**Acceptance:** clean build, all tabs functional, no behavior change.

**Risk:** trivial.

## Phase B — Bump Avalonia 11.2 → 12.x

**Goal:** the codebase compiles and runs on Avalonia 12 with no new
features, just maintenance breaking-change fixes.

1. Edit `Shared/QuailTracker.Analyzer.Shared.csproj` and
   `Desktop/QuailTracker.Analyzer.Desktop.csproj`:
   - All `Avalonia.*` packages: `11.2.*` → `12.0.*`.
   - `AvaloniaUI.DiagnosticsSupport`: `2.1.1` → `2.2.*`.
   - Verify third-party `Avalonia.Svg.Skia` has a 12.x release; if not,
     find a maintained alternative or pin to last 11.2-compatible version
     (this is the most likely bump-blocker).
2. Build. Fix breaking changes:
   - **Drag-drop arg rename.** Replace `e.Data.Contains(...)` /
     `e.Data.GetFiles()` with `e.DataTransfer.Contains(...)` /
     `e.DataTransfer.GetFiles()` in both `ImportView.axaml.cs` and
     `SingleAnalysisView.axaml.cs`. Build until clean.
   - Address any other compiler errors as they surface — the docs warn of
     ~40 breaking changes; most likely won't hit our surface area.
3. Smoke-test all 7 tabs.

**Acceptance:** clean build on Avalonia 12, all tabs still functional, no
visible regressions.

**Risk:** medium. The unknowns are (a) whether `Avalonia.Svg.Skia` has a
12.x release, (b) any breaking changes that don't show up in
compile-time but bite at runtime (XAML semantics, control lifecycle).
Smoke testing every tab is non-negotiable here.

## Phase C — Wire NativeWebView into MapView

**Goal:** an actual web rendering surface in the Map tab. The map can
still be blank — we're just establishing the host.

1. Add package: `dotnet add package Avalonia.Controls.WebView` to
   `Shared.csproj`. Match Avalonia version (`12.0.*`).
2. In `MapView.axaml`:
   - Add `xmlns:web="using:Avalonia.Controls"` (or whatever namespace
     `NativeWebView` ships in — verify via the doc example).
   - Replace `<ContentControl x:Name="WebViewContainer">` placeholder with
     `<NativeWebView x:Name="MapWebView" />`.
   - Keep the "WebView will be initialized when available" placeholder
     `IsVisible` bound to `!IsMapReady` for the loading state.
3. In `MapView.axaml.cs`:
   - On `OnDataContextChanged` (or `Loaded`), pass the `MapWebView`
     reference to `vm.InitializeMapAsync(MapWebView)`.
   - Subscribe `MapWebView.WebMessageReceived` and forward the body to
     `MapService` (via a method we add in Phase D).
4. Don't change `MapService` yet — it still has the stubbed
   `InitializeAsync(object webView)` signature, just receives a real
   control instead of `null`.
5. Build, run, open the Map tab. The WebView should render a blank /
   `about:blank` page without crashing.

**Acceptance:** Map tab loads with a visible blank WebView surface; no
crash; existing tabs unaffected.

**Risk:** low-medium. Linux WPE runtime is the obvious gotcha but we're
deploying to macOS / Windows.

## Phase D — Real MapService implementation

**Goal:** Cesium globe loads, stations / detections / localizations render,
clicks fire the existing events.

1. **Load the page.** Read `Shared/WebContent/cesium.html` from embedded
   resources at startup; call `webView.NavigateToString(htmlContent)`.
   (Verify whether `cesium.html` references external CDN resources or
   local assets — if local, we need either inlining or a
   `WebResourceRequested` interception with a custom URI scheme.)
2. **Replace `MapService.ExecuteJsAsync` stubs** with real
   `webView.InvokeScript(script)` calls. Re-read the file once and replace
   every `// Stub - actual implementation would execute JS in WebView` /
   `await Task.Delay(...)` placeholder.
3. **Wire `MapReady` to navigation completion.** On
   `webView.NavigationCompleted` after the cesium.html load, set
   `_isInitialized = true` and raise `MapReady`. Drop the
   `Task.Delay(100)` simulation.
4. **JS → .NET click events.** In `cesium.html`, attach Cesium's
   `screenSpaceEventHandler` for `LEFT_CLICK` and call
   `invokeCSharpAction(JSON.stringify({type: 'station', id: 'abc'}))`
   (or whichever entity was clicked). In `MapService`:
   - On `WebMessageReceived`, parse the JSON.
   - Raise `StationClicked` / `DetectionClicked` / `LocalizationClicked`
     based on the type. (These events have been declared but never raised
     since the audit started — closes those compiler warnings.)
5. **Verify each `Set*Async` round-trip.** With real JS execution working,
   walk through:
   - `SetStationsAsync` → stations appear as pins.
   - `SetDetectionsAsync` → detections appear with bearing rays if stereo.
   - `SetLocalizationsAsync` → localized positions with error ellipses.
   - `SetLayerVisibilityAsync` → checkboxes hide/show layers.
   - `SetTimeFilterAsync` → time-window filter narrows visible items.
   - `FlyToAsync` / `FlyToAllAsync` → camera animations work.

**Acceptance:** import a recording or two with stations + detections, open
Map tab, see them on the globe. Click a station — `SelectedStation`
property updates. Toggle filter checkboxes — items hide/show. No console
errors in the WebView devtools.

**Risk:** medium. `cesium.html` was authored without a real WebView host,
so there may be hidden assumptions (asset paths, CSP, iframe allowances).
Plan a half-day buffer for "Cesium tries to load X, fails because Y."

## Phase E — Cleanup and commit cadence

- Phase A: 1 commit, "Drop Avalonia.ReactiveUI; verify diagnostics setup".
- Phase B: 1 commit, "Bump Avalonia 11.2 → 12.0".
- Phase C: 1 commit, "Add NativeWebView host to Map tab".
- Phase D: 1 commit (or 2 if the JS message-channel work is large), "Real
  MapService implementation backed by NativeWebView".

After Phase D, the three `MapService` event-not-used warnings finally go
away, the Plans/ folder gets this doc updated with DONE markers per phase,
and the analyzer is on a maintained Avalonia line with its map feature
finally turned on.

## Open questions to resolve before starting

1. Does `Avalonia.Svg.Skia` have a 12.x release? (Look on NuGet before
   Phase B.)
2. Does `cesium.html` reference local assets (`./js/...`, `./assets/...`)
   that need a custom URI scheme, or is everything CDN/inline? (Read the
   file before Phase D.)
3. Deployment platform — macOS only, or Windows + Linux too? Affects how
   strict we need to be about Linux WPE prerequisites.

## Tracking

| Phase | Focus | Status |
|-------|-------|--------|
| A | Drop ReactiveUI, validate diagnostics | TODO |
| B | Avalonia 11.2 → 12.x | TODO |
| C | NativeWebView host | TODO |
| D | Real MapService + JS interop | TODO |
