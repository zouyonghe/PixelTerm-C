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
