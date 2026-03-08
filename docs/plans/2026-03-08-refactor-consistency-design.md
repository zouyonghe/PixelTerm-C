# 2026-03-08 Refactor Consistency Pass Design

## Goal

Bring repository documentation, comments, and module descriptions back in sync with the current codebase while continuing the existing low-risk refactor. This pass should reduce responsibility overlap in the next two hotspots without changing user-visible behavior.

## Current Repository Observations

- The previous refactor already split the historical monoliths, but several docs still describe an older `v1.4.0` state while the current build reports `1.7.2`.
- `README.md` still mixes current and stale build/runtime descriptions, including an outdated binary output note.
- `docs/PROJECT_STATUS.md` describes a repository shape that no longer matches the tree and omits newer modules.
- `src/input_dispatch_core.c` still carries three distinct concerns: event routing, delete prompt/delete flow orchestration, and delayed single-click processing.
- `src/app_preview_grid.c` still combines preview layout/state logic with preview-only terminal rendering helpers and status-line output.

## Approaches Considered

### 1. Documentation-Only Cleanup

Update `README.md` and the `docs/` set without changing code structure.

- Pros: Lowest risk, fast to land.
- Cons: Leaves the next maintainability hotspots untouched and misses the user goal of starting the refactor immediately.

### 2. Low-Risk Internal Extraction Plus Documentation Sync (Chosen)

Keep public APIs and behavior stable, but move internal responsibilities into smaller `.c` files with internal headers, then update docs to describe the new boundaries accurately.

- Pros: Improves maintainability now, keeps rollback simple, aligns docs with code in the same pass.
- Cons: Requires careful sequencing so docs describe the final code layout rather than an intermediate state.

### 3. Aggressive State/Public API Redesign

Continue by redesigning `PixelTermApp` access patterns, renaming public headers, and pushing more logic into new interfaces.

- Pros: Could reduce long-term coupling faster.
- Cons: Too risky for a maintenance-focused pass; likely to mix behavior changes with structural work.

## Chosen Design

### Scope

This pass covers two tracks in one branch:

1. Documentation and comment consistency
2. Low-risk internal refactoring in the next two large files

The pass explicitly avoids UI redesign, shortcut changes, feature additions, or public API churn.

### Code Refactor Boundaries

#### Input Dispatch Core

Keep `include/input_dispatch_core.h` and the exported entry points unchanged:

- `input_dispatch_core_handle_event()`
- `input_dispatch_core_process_pending()`
- `input_dispatch_core_process_animations()`
- `input_dispatch_core_pause_video_for_resize()`

Internally split `src/input_dispatch_core.c` into:

- `src/input_dispatch_delete.c`
  - delete prompt row/width calculation
  - show/clear prompt rendering
  - delete request confirmation flow
  - delete-current-image orchestration after confirmation
- `src/input_dispatch_pending_clicks.c`
  - delayed single-click processing for single/book mode
  - delayed click processing for preview/book-preview mode
  - delayed click processing for file manager mode

Add internal-only headers for those modules so `input_dispatch_core.c` becomes a coordinator instead of a mixed routing/UI file.

#### Preview Grid

Keep `include/app_preview.h` stable and leave state/navigation entry points in `src/app_preview_grid.c`, including:

- layout calculation and scroll adjustment
- preview selection cache helpers
- selection movement
- page movement
- zoom change
- preview hit-test / mouse click handling

Move preview-only render helpers into `src/app_preview_render.c`:

- cell rendering callback and preload/cache handoff
- border draw/clear helpers for selection repaint
- selected filename/status-line rendering
- preview info block output

Expose those helpers only through a new internal header used by `src/app_preview_grid.c`.

### Documentation Updates

After the code move, update these files to match the final structure:

- `README.md`
  - correct version badge and build output wording
  - keep installation/usage guidance aligned with the Makefile and release artifacts
- `docs/PROJECT_STATUS.md`
  - update version, repository layout, dependency summary, and testing notes
- `docs/DEVELOPMENT.md`
  - remove stale version/status references
  - describe the input-dispatch and preview module boundaries accurately
  - remove orphaned placeholder wording
- `docs/ARCHITECTURE.md`
  - record the new internal splits and current hotspot rationale
- `docs/REFACTORING_PLAN.md`
- `docs/refactor-plan.md`
  - add this pass to the progress snapshot and next steps

Comment updates should stay minimal: remove misleading comments, retain comments that explain non-obvious terminal/layout behavior, and avoid comment-only duplication of obvious code.

## Data and Control Flow Impact

No external flow changes are planned.

- Input events still enter through `input_dispatch_core_handle_event()`.
- Mode routing still happens in `src/input_dispatch_core.c`.
- Delete confirmation still requires the same repeated `r` flow.
- Preview rendering still uses the same renderer/preloader behavior and selection repaint paths.

The only change is where these responsibilities live internally.

## Error Handling and Compatibility

- Keep current return codes and mode guards unchanged.
- Keep public headers stable unless a small signature cleanup is proven internal-only.
- Avoid changing terminal escape output semantics during extraction.
- Treat behavior parity as higher priority than file size reduction.

## Validation Plan

Baseline and final verification for the pass:

- `make`
- `make test`
- `make EXTRA_CFLAGS=-Werror test`

Targeted smoke expectations after refactor:

- delete confirmation still works in single and preview flows
- pending single-click behavior remains unchanged in single/book/preview/file-manager flows
- preview selection repaint and full-grid redraw behavior remain unchanged when scrolling or zooming

## Risks and Guardrails

- The delete prompt and pending-click logic both touch terminal repaint timing; extraction must preserve call order.
- Preview rendering mixes cache ownership and UI drawing, so extracted helpers must keep memory ownership rules unchanged.
- Documentation should be updated after structural changes settle, so it describes the landed shape rather than a halfway state.

## Out of Scope

- No `PixelTermApp` redesign
- No shortcut remapping
- No renderer/video/book feature changes
- No release packaging changes beyond documentation accuracy
