# PixelTerm-C Refactoring Plan

## Goals
- Reduce file size and responsibility overload in `src/app.c` and `src/main.c`.
- Keep behavior identical while isolating UI modes and state.
- Make performance work easier by separating hot paths from UI glue.

## Current Hotspots
- `src/app.c` (~5.5k LOC) mixes lifecycle, rendering, file manager, preview grid, and book preview logic.
- `src/main.c` (~2.1k LOC) mixes event loop, input dispatch, UI overlays, and media decisions.
- `include/app.h` has a large monolithic `PixelTermApp` with unrelated state groups.
- Click handling repeats the same pattern for single view, preview grid, and file manager.
- Grid layout/paging logic is duplicated between preview and book preview.

## Proposed Module Split (Low-Risk)

### 1. App Core
- `src/app_core.c` + `include/app_core.h`
- Lifecycle (`app_create`, `app_destroy`, `app_initialize`) and directory loading.
- Navigation APIs (`app_next_image`, `app_previous_image`, `app_goto_image`).

### 2. UI Modes
- `src/file_manager.c` + `include/file_manager.h`
  - Selection, paging, layout, and render for file manager.
- `src/preview_grid.c` + `include/preview_grid.h`
  - Grid layout, selection, render for preview mode.
- `src/book_preview.c` + `include/book_preview.h`
  - Book grid layout, selection, render, and jump logic.

### 3. Input Dispatch
- `src/input_actions.c` + `include/input_actions.h`
  - Translate `InputEvent` -> App action.
  - Move key/mouse mode handling out of `main.c`.

### 4. UI Overlays
- `src/ui_overlays.c` + `include/ui_overlays.h`
  - Delete prompt, FPS overlay, info text, and status lines.

## Suggested State Decomposition
Create sub-structs and embed them into `PixelTermApp`:
- `AppRenderState`: `canvas`, `canvas_config`, `term_info`, `render_work_factor`, `gamma`, `dither_enabled`, `last_render_*`.
- `AppMediaState`: `gif_player`, `video_player`, `show_fps`, `video_scale`, `image_zoom`, `image_pan_*`.
- `AppUiState`: `info_visible`, `ui_text_hidden`, `needs_redraw`, `needs_screen_clear`, `suppress_full_clear`.
- `FileManagerState`: `file_manager_directory`, `directory_entries`, `selected_entry`, `scroll_offset`, `show_hidden_files`.
- `PreviewState`: `preview_mode`, `preview_selected`, `preview_scroll`, `preview_zoom`.
- `BookState`: `book_doc`, `book_path`, `book_page`, `book_page_count`, `book_preview_*`, `book_jump_*`.
- `InputState`: mouse and click tracking (see below).

## Click Handling Refactor
Replace the triplicated pending-click fields with a shared struct:

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

This removes repeated logic in `main.c` and makes timeouts consistent.

## Refactor Sequence (Incremental)
0. **Mode transition guardrail**: introduce `app_transition_mode()` and migrate call sites off raw `app_set_mode()`.
1. **State grouping only**: introduce sub-structs and move initialization to helper functions.
2. **Extract file manager**: move `app_file_manager_*` to `file_manager.c`.
3. **Extract preview grid**: move grid layout and render logic to `preview_grid.c`.
4. **Extract book preview**: move book grid logic to `book_preview.c`.
5. **Input dispatch split**: move key/mouse handling from `main.c` into `input_actions.c`.
6. **UI overlays**: isolate prompt rendering to `ui_overlays.c`.

Each step should compile and keep tests passing.

## Risk and Guardrails
- Avoid renaming public `app_*` APIs until all call sites are migrated.
- Keep `PixelTermApp` layout stable until all modules compile.
- Use `make test` and manual smoke tests after each extraction.
- Preserve behavior in book mode and file manager, which are sensitive to layout calculations.

## Documentation Updates
- Update `docs/DEVELOPMENT.md` to link this plan.
- After each extraction, update `docs/ARCHITECTURE.md` with new module boundaries.

## Progress Snapshot (2026-02-16)
- Completed:
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
- Verification baseline:
  - `make`
  - `make test`
  - `make clean && make EXTRA_CFLAGS=-Werror && make EXTRA_CFLAGS=-Werror test`
