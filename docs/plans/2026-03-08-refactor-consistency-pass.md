# Refactor Consistency Pass Implementation Plan

> Work through this plan task-by-task and verify each step before moving on.

**Goal:** Bring the repository docs and comments back in sync with the current codebase while shrinking `src/input_dispatch_core.c` and `src/app_preview_grid.c` through low-risk internal extractions that preserve behavior.

**Architecture:** Keep all public entry points stable and move only internal responsibilities into new internal modules. Land documentation updates after the code moves so the final docs describe the code that actually ships, not an intermediate state.

**Tech Stack:** C11, GLib, Chafa, GDK-Pixbuf, FFmpeg, MuPDF, GNU Make

---

### Task 1: Capture Baseline and Stale References

**Files:**
- Create: `docs/plans/2026-03-08-refactor-consistency-design.md`
- Modify: `README.md`
- Modify: `README_zh.md`
- Modify: `docs/PROJECT_STATUS.md`
- Modify: `docs/DEVELOPMENT.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/REFACTORING_PLAN.md`
- Modify: `docs/refactor-plan.md`

**Step 1: Record the current verification baseline**

Run: `make test`
Expected: PASS with the current baseline test count.

**Step 2: Identify stale version/build/layout text**

Run: `rg "v1\.4\.0|v1\.7\.0|pixelterm$|stage/|Current Status" README.md README_zh.md docs`
Expected: Hits in the current docs that need updating.

**Step 3: Update only factual inconsistencies first**

Make these edits:

```md
- Update version references to the current shipped state.
- Change build output wording from `pixelterm` to `bin/pixelterm` where the docs are describing `make` output.
- Replace stale repository layout bullets with the directories that actually exist.
- Keep release/install text aligned with the current README and Makefile behavior.
```

**Step 4: Re-run the stale-text search**

Run: `rg "v1\.4\.0|v1\.7\.0|stage/" README.md README_zh.md docs`
Expected: No matches.

**Step 5: Commit**

```bash
git add README.md README_zh.md docs/PROJECT_STATUS.md docs/DEVELOPMENT.md docs/ARCHITECTURE.md docs/REFACTORING_PLAN.md docs/refactor-plan.md docs/plans/2026-03-08-refactor-consistency-design.md docs/plans/2026-03-08-refactor-consistency-pass.md
git commit -m "docs: sync repository docs with current codebase"
```

### Task 2: Extract Delete Prompt and Delete Flow

**Files:**
- Create: `include/input_dispatch_delete_internal.h`
- Create: `src/input_dispatch_delete.c`
- Modify: `src/input_dispatch_core.c`
- Test: `tests/test_app_mode.c`

**Step 1: Introduce the internal function declarations**

Add an internal header with these declarations:

```c
gboolean input_dispatch_handle_delete_request(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_delete_current_image(PixelTermApp *app);
```

**Step 2: Switch `src/input_dispatch_core.c` to call the new helpers before they exist**

Replace the local static calls with the new internal API calls.

Run: `make test`
Expected: FAIL at compile or link time because the new helper implementation is not present yet.

**Step 3: Implement the extracted module with behavior-preserving logic**

Move these responsibilities into `src/input_dispatch_delete.c`:

```c
static gint delete_prompt_display_width(const char *text);
static gint delete_prompt_row(const PixelTermApp *app);
static void app_show_delete_prompt(PixelTermApp *app);
static void app_clear_delete_prompt(PixelTermApp *app);
static void handle_delete_current_in_preview(PixelTermApp *app);
gboolean input_dispatch_handle_delete_request(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_delete_current_image(PixelTermApp *app);
```

Keep the control flow and return codes identical.

**Step 4: Run the focused and full verification**

Run: `make test`
Expected: PASS with the same test count as baseline.

**Step 5: Commit**

```bash
git add include/input_dispatch_delete_internal.h src/input_dispatch_delete.c src/input_dispatch_core.c
git commit -m "refactor: extract input delete flow helpers"
```

### Task 3: Extract Pending Click Processing

**Files:**
- Create: `include/input_dispatch_pending_clicks_internal.h`
- Create: `src/input_dispatch_pending_clicks.c`
- Modify: `src/input_dispatch_core.c`

**Step 1: Add the new internal API**

Create a header exposing the pending-click entry point:

```c
void input_dispatch_process_pending_clicks(PixelTermApp *app);
```

**Step 2: Wire `src/input_dispatch_core.c` to the extracted helper first**

Replace the local `process_pending_clicks()` call with `input_dispatch_process_pending_clicks(app)`.

Run: `make test`
Expected: FAIL until the new implementation is added.

**Step 3: Implement the extracted module**

Move the delayed click behavior into `src/input_dispatch_pending_clicks.c`, preserving:

```c
static const gint64 k_click_threshold_us = 400000;
```

and the existing single/book, preview/book-preview, and file-manager behavior.

**Step 4: Re-run verification**

Run: `make test`
Expected: PASS.

**Step 5: Commit**

```bash
git add include/input_dispatch_pending_clicks_internal.h src/input_dispatch_pending_clicks.c src/input_dispatch_core.c
git commit -m "refactor: extract pending click dispatch logic"
```

### Task 4: Extract Preview Render-Only Helpers

**Files:**
- Create: `include/app_preview_render_internal.h`
- Create: `src/app_preview_render.c`
- Modify: `src/app_preview_grid.c`

**Step 1: Declare the internal preview-render helpers**

Expose only the render-specific pieces needed by `src/app_preview_grid.c`:

```c
ErrorCode app_preview_print_info(PixelTermApp *app);
void app_preview_render_selected_filename(PixelTermApp *app);
void app_preview_draw_cell_border(const PixelTermApp *app,
                                  const PreviewLayout *layout,
                                  gint index,
                                  gint start_row,
                                  gint vertical_offset);
void app_preview_clear_cell_border(const PixelTermApp *app,
                                   const PreviewLayout *layout,
                                   gint index,
                                   gint start_row,
                                   gint vertical_offset);
GridRenderResult app_preview_grid_render_cell(const GridRenderContext *context,
                                              const GridRenderCell *cell,
                                              void *userdata);
```

**Step 2: Move call sites in `src/app_preview_grid.c` to the new internal header**

Run: `make test`
Expected: FAIL until the implementation exists.

**Step 3: Implement `src/app_preview_render.c`**

Move the render-only logic out of `src/app_preview_grid.c`, including:

```c
typedef struct {
    PixelTermApp *app;
    ImageRenderer *renderer;
    GList *cursor;
} PreviewGridRenderContext;
```

and the preview info/status-line rendering logic, while leaving layout/navigation/cache selection helpers in `src/app_preview_grid.c`.

**Step 4: Run verification**

Run: `make test`
Expected: PASS.

**Step 5: Commit**

```bash
git add include/app_preview_render_internal.h src/app_preview_render.c src/app_preview_grid.c
git commit -m "refactor: extract preview render helpers"
```

### Task 5: Final Documentation Sync for the Landed Structure

**Files:**
- Modify: `README.md`
- Modify: `README_zh.md`
- Modify: `docs/PROJECT_STATUS.md`
- Modify: `docs/DEVELOPMENT.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/REFACTORING_PLAN.md`
- Modify: `docs/refactor-plan.md`

**Step 1: Update module-boundary docs to describe the landed file layout**

Apply wording like:

```md
- `src/input_dispatch_core.c` now coordinates event routing and shared flow control.
- Delete prompt/delete confirmation lives in `src/input_dispatch_delete.c`.
- Pending-click processing lives in `src/input_dispatch_pending_clicks.c`.
- Preview render-only helpers live in `src/app_preview_render.c`.
```

**Step 2: Remove comment/documentation drift**

Replace stale or misleading comments with concise explanations of non-obvious behavior, especially around terminal row placement and preview selection redraw behavior.

**Step 3: Run doc consistency checks**

Run: `rg "v1\.4\.0|v1\.7\.0|stage/|src/app\.c \(~5\.5k LOC\)" README.md README_zh.md docs`
Expected: No stale matches that contradict the current codebase.

**Step 4: Commit**

```bash
git add README.md README_zh.md docs/PROJECT_STATUS.md docs/DEVELOPMENT.md docs/ARCHITECTURE.md docs/REFACTORING_PLAN.md docs/refactor-plan.md
git commit -m "docs: update architecture and refactor notes"
```

### Task 6: Full Verification

**Files:**
- Modify: `Makefile` (only if verification reveals a real issue)
- Test: `tests/test_app_mode.c`
- Test: `tests/test_browser.c`
- Test: `tests/test_common.c`
- Test: `tests/test_gif_player.c`
- Test: `tests/test_renderer.c`
- Test: `tests/test_text_utils.c`

**Step 1: Run the normal verification pass**

Run: `make`
Expected: PASS, producing `bin/pixelterm`.

**Step 2: Run the test suite**

Run: `make test`
Expected: PASS with no regressions.

**Step 3: Run the warning-tight pass**

Run: `make EXTRA_CFLAGS=-Werror test`
Expected: PASS.

**Step 4: Review the final diff**

Run: `git diff --stat`
Expected: Changes limited to the planned modules and docs.

**Step 5: Commit (if any verification fix was needed here)**

```bash
git add -A
git commit -m "test: verify refactor consistency pass"
```
