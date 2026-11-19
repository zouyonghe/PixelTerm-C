# Terminal Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace scattered terminal recovery with one output owner and explicit
session cleanup contracts without changing #47 media behavior.

**Architecture:** Extract the existing main-context output queue from Kitty
encoding. Use one checked submission/recovery implementation and distinguish
destructive-once commands from idempotent restoration. Existing input/probe
lifecycle owns termios; queued restoration outlives its callers.

**Tech Stack:** C11, GLib, stdio/POSIX, existing FFmpeg/Chafa/MuPDF integrations.

**Spec:** `docs/plans/2026-11-19-terminal-lifecycle-design.md`

## Global Constraints

- Preserve CLI/config, RGB24 geometry and fallback behavior; no dependency additions.
- Main-context terminal I/O only; workers retain current queue/generation contracts.
- One recovery queue; 100 ms background retry interval; no destructive replay.
- Preserve local uncommitted policy work; do not overwrite unrelated files.
- Do not suppress sanitizers, remove failing tests or merge an unverified head.

## Review Focus

- Buffered deletes whose delivery is ambiguous must never replay (Task 1).
- Unsent cleanup controls must survive handler/player destruction (Task 2).
- Recovery created inside async/event work must block later output (Task 3).
- User-original terminal settings must override pending app-raw snapshots (Task 2).
- Standalone test links and non-GNU platform purge declarations must remain valid (Tasks 1 and 4).

---

### Task 1: Extract the terminal output owner

**Files:** Create `include/terminal_output.h`, `src/terminal_output.c`,
`tests/test_terminal_output.c`; modify `kitty_graphics.c/.h`, `Makefile`,
`tests/test_common.c`.

**Interfaces:**
- `TerminalOutputPolicy`: `TERMINAL_OUTPUT_ONCE`, `TERMINAL_OUTPUT_RESTORE`.
- `gboolean terminal_output_ready(gint64 now_us)` — background readiness.
- `gboolean terminal_output_flush(void)` — immediate drain.
- `gboolean terminal_output_write(const char *bytes, gsize length)` — checked
  non-replayed payload; first drain pending output; recover on failed submission.
- `void terminal_output_defer(const char *bytes, TerminalOutputPolicy policy)` —
  copy owned command, prefix ST, coalesce identical restore controls.
- `gboolean terminal_output_recover(void)` — purge, queue ST, drain and set deadline.

- [x] Add tests for once/restore policies, persistent failure coalescing, and
  99,999/100,000 us retry boundary. Assert actual sink bytes, not helper counts alone.
- [x] Run targeted tests and observe missing or incorrect behavior fail.
- [x] Move the existing queue/purge/deadline implementation rather than duplicate it;
  keep Kitty allocation and command encoders in their current module.
- [x] Link the lightweight output module wherever UI/input references it; include
  standalone preview/file-manager/book binaries. Keep any transitional adapters
  temporary and remove them in Task 4.
- [x] Run `make -j4 EXTRA_CFLAGS=-Werror test`; inspect all test binaries.
- [x] Commit only output extraction and its directly dependent migrations.

### Task 2: Make session teardown complete and replay-safe

**Files:** Modify `input.c/.h`, `terminal_probe.c/.h`, `ui_render_utils.c`,
`main.c`, `tests/test_main_lifecycle.c`, `tests/test_kitty_graphics.c`.

**Consumes:** Task 1 output interfaces. **Produces:** Existing input lifecycle
APIs with documented transfer of restore controls; no extra generic session layer.

- [x] Add failure/recovery tests requiring mouse-disable, cursor-show, alternate-
  screen exit and sync-end after ST, including after handler destruction.
- [x] Retain outer original termios on non-TTY/restore failure; test raw snapshot
  replacement and final restoration with two failures versus three.
- [x] Queue idempotent controls separately from raw-mode ownership and use checked
  final drain. Make sync-end defer through its existing UI helper.
- [x] Preserve primary errors and emit one secondary cleanup diagnostic; test both.
- [x] Run `/main`, input/probe and output tests, then the full normal suite.
- [x] Commit the independently verified lifecycle slice.

### Task 3: Route presentation through the shared output contract

**Files:** Modify `gif_player.c`, `video_player.c`, `app_single_render.c`,
`ui_render_utils.c`, `main.c`, related GIF/video/UI lifecycle tests.

**Consumes:** `terminal_output_write/ready/flush/defer`. **Produces:** Existing
public media API behavior; no new codec, rendering backend or CLI parameter.

- [x] Add tests for blocked-but-unsent GIF delete surviving reset, actual Kitty
  APC absence after hide failure, and sync-end after failed initial play.
- [x] Strengthen stage-injection tests to record every unsafe dispatch/render
  while recovery remains pending, including async-to-input fallthrough.
- [x] Route GIF root/subsequent/software output and video presentation failures
  through the same checked submission boundary. Retain renderer output format.
- [x] Use one stage readiness contract for startup, event, animation, async and
  cleanup; ensure no unbounded shutdown wait or busy-spin is introduced.
- [x] Remove repeated local purge/retry sequences; keep generation invalidation,
  worker joins, paused target restoration and fallback policies unchanged.
- [x] Run full normal/UBSan/debug suites and intentional missing-gate/wrong-delete
  mutations; restore production code before committing.
- [x] Commit presentation migration separately from optional cleanup.

### Task 4: Remove drift and validate the complete branch

**Files:** Output/Kitty headers, `Makefile`, threading/performance/protocol docs,
and only directly affected tests.

- [ ] Remove old `kitty_graphics_*` recovery adapters and stale state flags once
  all callers migrate. Search remaining direct terminal writes for bypasses.
- [ ] Verify minimal standalone dependencies; no protocol module imports a media
  player merely for error recovery. Correct the currently failing preview link.
- [ ] Update contracts with exact failure semantics, retry timing and limits;
  record deferred real-terminal validation honestly.
- [ ] Run `make -j4 EXTRA_CFLAGS=-Werror test`, `ubsan-test`, `debug-test`,
  `tsan-test`, plus `git diff --check`; record results for the exact head.
- [ ] Review diff for removed validations, changed geometry, widened inputs and
  lifecycle regressions. Push only after local validation; inspect Linux/macOS CI.
- [ ] Report remaining risks; do not auto-merge.

## Execution notes

- Phase 1: extracted terminal_output and migrated all old recovery callers (no duplicate adapters).
- Preserved and incorporated the pre-existing uncommitted policy work; fixed the standalone preview link by linking only the lightweight output module.
- GIF root submission now returns the original presentation result; successful recovery cannot erase the failed-submit result.
- Warning-clean full normal suite passed: 515 C tests and 74 Python tests. Tasks 2/3 continue with lifecycle ownership/fault tests and checked presentation.

- Phase 2/3: session controls and unsent GIF deletion remain owned after handler/player destruction. GIF/video presentation is assembled then submitted via terminal_output_write; local payload fwrite/flush branches were removed.
- Gate mutations after dispatch, animation and async rendering each fail at premature_dispatches==0; production code restored. Full normal suite passed 518 C and 74 Python tests. Final sanitizer/platform checks remain in Task 4.
