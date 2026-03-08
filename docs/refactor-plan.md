# PixelTerm-C Refactor Plan

> Goal: document the refactor direction first, then implement in incremental phases with behavior parity, easy rollback, and testability at every step.

## Background and Current State
- Complexity is now concentrated in `src/input_dispatch_core.c` (~408 LOC) and `src/app_preview_grid.c` (~716 LOC); the historical `src/app.c` monolith has already been split.
- `PixelTermApp` is still a large state container, but module headers and mode-specific helpers are now mostly in place.
- This pass has already removed repetition around delete prompt flow, pending-click processing, and preview render-only helpers; the remaining work is smaller follow-up cleanup and regression hardening.

## Objectives
1. Reduce the remaining hotspot files with small internal extractions.
2. Keep every step behavior-preserving and independently verifiable.
3. Keep docs aligned with the shipped release (`v1.7.2`) and Makefile output (`bin/pixelterm`).

## Constraints (Must Follow)
- Ship in minimal, rollback-friendly steps; each step must pass `make` and `make test`.
- No user-visible behavior changes unless explicitly scoped and tested.
- Keep mode switching centralized via `app_transition_mode()`; avoid scattered raw mode assignments.

## Non-goals
- No UI redesign.
- No shortcut semantics changes.
- No new third-party dependencies for this refactor.

## Module Split Plan
1. Delete prompt/delete confirmation extraction
- `src/input_dispatch_delete.c`, `include/input_dispatch_delete_internal.h`
- Move delete prompt layout/clear helpers and preview-delete flow out of `src/input_dispatch_core.c`.

2. Pending click extraction
- `src/input_dispatch_pending_clicks.c`, `include/input_dispatch_pending_clicks_internal.h`
- Move delayed click timeout processing out of `src/input_dispatch_core.c`.

3. Preview render extraction
- `src/app_preview_render.c`, `include/app_preview_render_internal.h`
- Move render-only preview helpers out of `src/app_preview_grid.c` while keeping layout/navigation local.

4. Documentation follow-through
- Refresh README/project/development/architecture docs after the landed module boundaries are in place.

## Phased Execution
### Phase 0: Doc consistency pass
- Update version/build/layout references to the current shipped state.

### Phase 1: Delete flow extraction
- Keep the public input-dispatch API stable while moving delete helpers into an internal module.

### Phase 2: Pending click extraction
- Move timeout processing without changing click behavior.

### Phase 3: Preview render extraction
- Split render-only preview helpers from grid layout/navigation.

### Phase 4: Final doc sync and verification
- Refresh module-boundary docs and re-run `make`, `make test`, and `make EXTRA_CFLAGS=-Werror test`.

## Risk Controls
- Preserve public behavior and external app APIs.
- Keep changes incremental and buildable.
- Validate after each extraction with tests and smoke checks.

## Validation Strategy
- Baseline for each step:
  - `make`
  - `make test`
  - `make EXTRA_CFLAGS=-Werror test`
- Targeted smoke checks across mode transitions:
  - single <-> preview
  - preview <-> file manager
  - single/book <-> book preview/TOC

## Progress Snapshot (2026-03-08)
Completed:
- Historical monolith splitting is complete; the current pass focuses on `src/input_dispatch_core.c` and `src/app_preview_grid.c`.
- Docs are synced to release `v1.7.2`, the current repository layout, and the `bin/pixelterm` source-build output.
- Delete prompt/delete confirmation flow is now isolated in:
  - `src/input_dispatch_delete.c`
  - `include/input_dispatch_delete_internal.h`
- Delayed click timeout processing is now isolated in:
  - `src/input_dispatch_pending_clicks.c`
  - `include/input_dispatch_pending_clicks_internal.h`
- Preview render-only helpers are now isolated in:
  - `src/app_preview_render.c`
  - `include/app_preview_render_internal.h`
- Unified mode transitions via `app_transition_mode()` with table-driven transition guards and per-mode hooks.
- Makefile cleanup with shared test object rules and auto-dependencies.
- CI hardening (`cppcheck` non-optional, `-Werror` + tests).
- `src/app.c` split into:
  - `src/app_core.c`
  - `src/app_single_render.c`
  - `src/app_file_manager.c`
  - `src/app_preview_book.c` (subsequently further split)
- File manager further split:
  - `src/app_file_manager.c` (state/navigation/refresh)
  - `src/app_file_manager_render.c` (viewport/hit-test/render + mouse enter)
- Preview/book-preview further split:
  - `src/app_preview_grid.c` (preview interaction/render)
  - `src/app_preview_book.c` (book-preview interaction/render)
  - `src/app_preview_shared.c` + `include/app_preview_shared_internal.h` (shared grid helpers)
- Book flow decomposition:
  - `src/app_book_toc.c`
  - `src/app_book_page_render.c`
- Shared render/util consolidation:
  - `src/ui_render_utils.c` + `include/ui_render_utils.h`
  - `src/pixbuf_utils.c` + `include/pixbuf_utils.h`
- Input click tracking cleanup:
  - Introduced `ClickTracker` and migrated `InputState` to:
    - `single_click`
    - `preview_click`
    - `file_manager_click`
  - Removed repeated pending-click fields and duplicated timeout bookkeeping.
- List hot-path optimization with pointer/index hints:
  - `PreviewState.selected_link + selected_link_index`
  - `FileManagerState.selected_link + selected_link_index`
  - `FileBrowser.current_index`
- Remaining `g_list_nth*` / `g_list_length()` hotspots removed from target hot files.
- Error semantics cleanup:
  - Mode mismatch paths in file-manager / preview / book-preview handlers now return `ERROR_INVALID_ARGS`.
  - Null-pointer guards still return `ERROR_MEMORY_ALLOC`.
- Header boundary cleanup:
  - Introduced `include/app_state.h` for app state/types and mode helpers.
  - Split API declarations into:
    - `include/app_core.h`
    - `include/app_file_manager.h`
    - `include/app_preview.h`
    - `include/app_book_mode.h`
    - `include/app_render.h`
    - `include/app_runtime.h`
  - Kept `include/app.h` as an umbrella include for compatibility.

Current next steps:
- Continue tightening internal boundaries only where they reduce coupling without changing behavior.
- Add targeted regression coverage for input-dispatch and preview-mode round trips if future changes touch those paths.
