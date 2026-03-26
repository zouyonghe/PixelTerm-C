# PixelTerm-C Refactoring Plan

## Goals
- Reduce file size and responsibility overload in the remaining coordinator-heavy files without changing behavior.
- Keep new maintainability seams around video playback internals and startup/runtime config application explicit and documented.
- Keep broader `PixelTermApp` state-structure redesign and terminal protocol redesign out of this pass and defer them until the current seams settle.

## Current Hotspots
- `src/input_dispatch_core.c` (~408 LOC) is now a narrower router, but it still coordinates mode dispatch and shared event flow.
- `src/app_preview_grid.c` (~716 LOC) now focuses on preview interaction/state, but it remains one of the larger mode-specific files.
- `src/video_player.c` remains the main playback coordinator even after the clock, seek-preview, and debug/test helper splits.
- `src/app.c` and `src/main.c` are now thin coordination layers compared with the earlier monoliths; `src/main.c` now delegates runtime config application to `src/app_config_runtime.c` instead of copying fields inline.
- Shared click tracking already lives in `InputState`, so the next steps are selective follow-up cleanup and regression coverage rather than another large state redesign.

## Landed Maintainability Seams

### 1. Runtime Config Application
- `src/app_config_runtime.c` + `include/app_config_runtime.h`
- Centralizes `AppConfig` -> `PixelTermApp` runtime field application before `app_initialize()`.

### 2. Video Player Clock Helpers
- `src/video_player_clock.c` + `include/video_player_clock_internal.h`
- Fallback PTS tracking and current-position clock helpers used by playback/seek flows.

### 3. Video Player Seek Helpers
- `src/video_player_seek.c` + `include/video_player_seek_internal.h`
- Seek target clamping plus seek-preview decode/render helpers and preview-test controls.

### 4. Video Player Debug/Test Helpers
- `src/video_player_debug.c` + `include/video_player_debug_internal.h`
- Debug logging lifecycle/filtering plus queue-wait and seek test hooks.

### 5. Documentation Sync
- Keep `docs/development/ARCHITECTURE.md`, `docs/development/DEVELOPMENT.md`, and this plan aligned with the landed seams.

## Suggested State Decomposition
State grouping is partially complete today:
- `FileManagerState`: file-manager directory/list/selection state.
- `PreviewState`: preview selection, scroll, zoom, and cached link hints.
- `BookState`: book reader, preview, jump, and TOC state.
- `InputState`: mouse and click tracking (see below).
- `AsyncState`: deferred render/image request state.

Render/media/UI flags still live directly on `PixelTermApp`, and the broader state-layout redesign is intentionally deferred. This pass focuses on helper extraction rather than another state-layout change.

## Click Handling Refactor
Shared click tracking is already in place:

```
typedef struct {
    gboolean pending;
    gint64 pending_time;
    gint x;
    gint y;
} ClickTracker;
```

Embed one tracker per mode:
- `InputState.single_click`
- `InputState.preview_click`
- `InputState.file_manager_click`

This keeps timeouts consistent across modes. With the pending-click extraction now landed, the next low-risk step is adding targeted regression coverage around those mode transitions.

## Refactor Sequence (Incremental)
0. **Doc sync**: update version/build/layout text to the current repository state.
1. **Extract delete flow**: move delete prompt/delete confirmation helpers out of `src/input_dispatch_core.c`.
2. **Extract pending clicks**: move pending-click processing out of `src/input_dispatch_core.c`.
3. **Extract preview render helpers**: move render-only preview helpers out of `src/app_preview_grid.c`.
4. **Extract runtime config application**: move the `AppConfig` -> `PixelTermApp` field copy out of `src/main.c` into `src/app_config_runtime.c`.
5. **Extract video player helper seams**: move clock, seek-preview, and debug/test helpers out of `src/video_player.c` into focused internal modules.
6. **Later work**: revisit broader `PixelTermApp` state decomposition and terminal protocol redesign only after the current seams and coverage baseline are stable.

Each step should compile and keep tests passing.

## Risk and Guardrails
- Avoid renaming public `app_*` APIs until all call sites are migrated.
- Keep `PixelTermApp` layout stable until all modules compile.
- Do not fold larger state-bucket or protocol redesign work into these helper extractions.
- Use `make test` and manual smoke tests after each extraction.
- Preserve behavior in book mode and file manager, which are sensitive to layout calculations.

## Documentation Updates
- Keep build/install text aligned with `Makefile` (`bin/pixelterm`, `make install` -> `$(PREFIX)/bin/pixelterm`).
- After each extraction, update `docs/development/ARCHITECTURE.md` and the companion refactor notes with the landed module boundaries.
- Keep doc language explicit that the current pass is about helper seams, not a full state-layout or protocol redesign.

## Progress Snapshot (2026-03-11)
- Completed:
  - Current pass targets the remaining large helper files: `src/input_dispatch_core.c` and `src/app_preview_grid.c`.
  - Synced README/project/development/architecture docs to shipped release `v1.7.8`, current repository layout, and `bin/pixelterm` build output.
  - Extracted delete prompt/delete confirmation helpers from `src/input_dispatch_core.c` into:
    - `src/input_dispatch_delete.c`
    - `include/input_dispatch_delete_internal.h`
  - Extracted delayed click timeout handling from `src/input_dispatch_core.c` into:
    - `src/input_dispatch_pending_clicks.c`
    - `include/input_dispatch_pending_clicks_internal.h`
  - Extracted preview render-only helpers from `src/app_preview_grid.c` into:
    - `src/app_preview_render.c`
    - `include/app_preview_render_internal.h`
  - Unified mode transition entry via `app_transition_mode()` in `src/app_mode.c`, now backed by a table-driven transition mask plus per-mode enter/exit hooks.
  - Extracted file manager mode code from `src/app.c` into `src/app_file_manager.c`.
  - Further split file manager responsibilities into `src/app_file_manager.c` (state/navigation/refresh) and `src/app_file_manager_render.c` (viewport/hit-test/render + mouse entry flow).
  - Extracted preview/book mode code from `src/app.c` into `src/app_preview_book.c`.
  - Further split preview/book-preview responsibilities:
    - `src/app_preview_grid.c` for preview-grid interaction/render flow.
    - `src/app_preview_book.c` reduced to book-preview interaction/render flow.
    - `src/app_preview_shared.c` for shared grid helpers (layout offsets, grid borders, grid renderer creation, rendered-line drawing).
  - Extracted core app state/navigation APIs from `src/app.c` into `src/app_core.c` (directory/file loading, image navigation, current-file accessors, book open/close, delete flow).
  - Introduced `src/ui_render_utils.c` to centralize duplicated terminal UI helper logic used by single-view and preview/book rendering paths.
  - Introduced mode-based input handler routing in `src/input_dispatch_core.c` (`ModeInputHandlers` table) while keeping `src/input_dispatch.c` as a stable API wrapper.
  - Started decoupling `src/input_dispatch_core.c` by extracting media-state checks into `src/input_dispatch_media.c`.
  - Extracted book page-change helper into `src/input_dispatch_book.c`.
  - Extracted mode key handlers into `src/input_dispatch_key_modes.c` (including single-mode video key actions).
  - Further split key handlers by responsibility: `src/input_dispatch_key_single.c` (single/video keys) and `src/input_dispatch_key_file_manager.c` (file-manager keys).
  - Further split book-related key handlers into `src/input_dispatch_key_book.c` (book/book-preview + jump/TOC).
  - Extracted mode mouse handlers and single-view zoom helpers into `src/input_dispatch_mouse_modes.c`.
  - Consolidated duplicated stream pixbuf loading helper into `src/pixbuf_utils.c`.
  - Extracted single-view render/refresh flow from `src/app.c` into `src/app_single_render.c`.
  - Further split book-mode modules by responsibility:
    - `src/app_book_toc.c` for TOC layout/hit-test/selection/render flow.
    - `src/app_book_page_render.c` for single/double-page book rendering flow.
  - Reduced duplication by moving kitty image cleanup helper into `src/ui_render_utils.c`.
  - Reduced preview/book-preview duplication by unifying shared cell-origin and border draw/clear helpers in `src/app_preview_book.c`.
  - Extracted runtime config application from `src/main.c` into:
    - `src/app_config_runtime.c`
    - `include/app_config_runtime.h`
  - Split video playback internals by responsibility into:
    - `src/video_player_clock.c` + `include/video_player_clock_internal.h`
    - `src/video_player_seek.c` + `include/video_player_seek_internal.h`
    - `src/video_player_debug.c` + `include/video_player_debug_internal.h`
  - Replaced repeated pending-click fields with a shared `ClickTracker` model in `InputState`:
    - `single_click`
    - `preview_click`
    - `file_manager_click`
  - Split state/type definitions into `include/app_state.h`; `include/app.h` now centers on API declarations.
  - Split API declarations into module headers:
    - `include/app_core.h`
    - `include/app_file_manager.h`
    - `include/app_preview.h`
    - `include/app_book_mode.h`
    - `include/app_render.h`
    - `include/app_runtime.h`
    while keeping `include/app.h` as an umbrella header for compatibility.
  - Added list-selection caches (`selected_link + selected_link_index`) to `PreviewState` and `FileManagerState` and migrated preview/file-manager lookups to hint-based traversal.
  - Added `FileBrowser.current_index` and switched browser index navigation to cached/hint traversal.
  - Removed remaining `g_list_nth*` / `g_list_length()` hotspots from `src/app.c`, `src/app_preview_book.c`, `src/app_file_manager.c`, `src/preloader.c`, `src/browser.c`, and `src/input_dispatch_key_file_manager.c`.
  - Normalized mode guard error semantics: mode mismatch now returns `ERROR_INVALID_ARGS` in mode-scoped file-manager/preview/book-preview handlers.
  - Hardened CI/build gates (`cppcheck` non-optional, `-Werror` + tests).
  - Explicitly deferred broader `PixelTermApp` state-structure redesign and terminal capability/protocol redesign to later follow-up work.
- Verification baseline:
  - `make`
  - `make test`
  - `make clean && make EXTRA_CFLAGS=-Werror && make EXTRA_CFLAGS=-Werror test`
