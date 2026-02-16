# PixelTerm-C Refactor Plan

> Goal: document the refactor direction first, then implement in incremental phases with behavior parity, easy rollback, and testability at every step.

## Background and Current State
- Complexity is concentrated in `src/app.c` (historically 6k+ LOC), mixing UI rendering, file management, book preview, text utilities, and preload scheduling.
- `PixelTermApp` is still large and state boundaries are not fully isolated.
- Repetition still exists across some rendering and mode flows.

## Objectives
1. Reduce file size and responsibility overlap, with clear module boundaries.
2. Consolidate duplicated logic (text/path handling, media checks, preloader control, grid rendering helpers).
3. Keep every step behavior-preserving and independently verifiable.

## Constraints (Must Follow)
- Ship in minimal, rollback-friendly steps; each step must pass `make` and `make test`.
- No user-visible behavior changes unless explicitly scoped and tested.
- Keep mode switching centralized via `app_transition_mode()`; avoid scattered raw mode assignments.

## Non-goals
- No UI redesign.
- No shortcut semantics changes.
- No new third-party dependencies for this refactor.

## Module Split Plan
1. Text/path utilities
- `src/text_utils.c`, `include/text_utils.h`
- Centralized UTF-8 width/truncation and terminal-safe text helpers.

2. Media classification unification
- `src/media_utils.c`, `include/media_utils.h`
- Unified media kind checks and mode-dependent behavior guards.

3. Preloader lifecycle encapsulation
- `src/preload_control.c`, `include/preload_control.h`
- Consolidated start/stop/reset/queue logic.

4. Grid render skeleton extraction
- `src/grid_render.c`, `include/grid_render.h`
- Shared grid traversal with callback-based cell renderers.

5. Input dispatch decomposition
- Wrapper in `src/input_dispatch.c`
- Core routing in `src/input_dispatch_core.c`
- Mode helpers split by concern:
  - `src/input_dispatch_media.c`
  - `src/input_dispatch_book.c`
  - `src/input_dispatch_key_modes.c`
  - `src/input_dispatch_key_single.c`
  - `src/input_dispatch_key_file_manager.c`
  - `src/input_dispatch_key_book.c`
  - `src/input_dispatch_mouse_modes.c`

6. App mode/state decomposition (ongoing)
- Keep behavior stable while progressively isolating mode-specific logic and shared helpers.

## Phased Execution
### Phase 0: Mode transition guardrail
- Introduce and enforce `app_transition_mode()`.
- Centralize mode enter/exit side effects.

### Phase 1: Utility convergence
- Move text/media shared logic into dedicated modules.

### Phase 2: Preloader convergence
- Replace scattered preloader lifecycle logic with centralized helpers.

### Phase 3: Rendering decomposition
- Split rendering by mode and extract shared render helper layers.

### Phase 4: Input decomposition
- Replace monolithic mode branching with routed handlers.

### Phase 5: State boundary cleanup
- Continue reducing state coupling and narrow mutable surfaces.

## Risk Controls
- Preserve public behavior and external app APIs.
- Keep changes incremental and buildable.
- Validate after each extraction with tests and smoke checks.

## Validation Strategy
- Baseline for each step:
  - `make -j4`
  - `make test`
  - `make EXTRA_CFLAGS=-Werror test`
- Targeted smoke checks across mode transitions:
  - single <-> preview
  - preview <-> file manager
  - single/book <-> book preview/TOC

## Progress Snapshot (2026-02-16)
Completed:
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
- Continue tightening `PixelTermApp` state boundaries (internal grouping/access patterns).
- Add targeted regression tests for complex mode round-trips and book-preview edge paths.
