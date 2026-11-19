# VideoPlayer Threading and Locking Contract

This document defines ownership and synchronization rules for `VideoPlayer`.
It applies to the public API in `include/video_player.h` and the implementation
in `src/video_player*.c`.

## Terminal output contract

`terminal_output.c/.h` is the single main-context owner of checked submissions,
failed stdio buffer disposal, APC framing, deferred command lifetime and the
100 ms background retry deadline. `kitty_graphics.c` encodes Kitty commands,
runs the image-ID allocation probe, and queues deferred deletes and ST recovery
through this owner. UI/input depend on the output owner's readiness contract,
but do not implement its recovery queue, buffer disposal or retry policy.
Legacy UI rows still assemble bytes with `printf`/`fwrite`; their checked frame
boundaries use `terminal_output_flush`. This module owns checking and recovery,
not every individual formatting call. Remaining legacy prompt flushes require
their own checked integration; the contract does not make raw stdio checked.

- `terminal_output_write` first drains pending work. If that fails, it returns
  false without attempting the payload. Otherwise it writes and flushes the
  payload and returns that submission result even if recovery later succeeds.
- `terminal_output_defer(..., TERMINAL_OUTPUT_ONCE)` owns unsent destructive
  commands. A failed/ambiguous attempt becomes ST-only; deletion is not replayed.
- `TERMINAL_OUTPUT_RESTORE` owns idempotent mouse/cursor/alternate-screen/sync-end
  controls until success or process exit, including after the handler/player has
  been destroyed. Exit makes one final attempt before discarding pending controls.
- `terminal_output_ready` returns false both after failure and during backoff.
  The event loop must not run later output stages while readiness is false.
- Termios recovery remains in input/probe lifecycle and never waits for stdout.
  Outer user-original settings override a pending probe app-raw snapshot.
- Media stop and render-area clearing return submission failures. Callers must
  stop before rendering, changing protocol or resuming playback on failure.
  Void input-dispatch paths record failures in `app->last_error` for the main loop.
  GIF timers wait during recovery backoff; failed frame submissions stop playback
  and report the error instead of scheduling another frame.
  Native GIF pause transfers its idempotent STOP to retained restoration so
  logical pause does not silently discard the terminal-side stop after failure.

Recovery assumes the same terminal session and image-ownership namespace. An
external terminal reset or reconnect is not detected by a local retry deadline;
cross-session queued-ID validation requires a separate continuity protocol.
The two-second readiness limit bounds repeated returned write failures; it does
not interrupt a kernel-blocked synchronous `fwrite`/`fflush`. Deadline-bounded
submission for a full pipe or flow-controlled terminal requires a separate
nonblocking-output change. Session restoration also assumes stdout still names
the original terminal; redirection during an active session is not reattached.
Shared-memory frames with failed/ambiguous submissions are discarded and their
local resources reclaimed. A terminal that received the command but has not yet
opened the shared-memory object by name may lose that failed frame; no delivery
success is reported.
Guaranteeing its display would require acknowledgement-based resource lifetime,
which this recovery contract does not provide.

GIF/video build a presentation payload and submit once through this owner.
Workers never write to stdout or mutate deferred restoration. This adds a
command-buffer copy compared with incremental stdout assembly; end-to-end CPU,
RSS and physical-terminal frame rate must be remeasured before performance claims.

## Execution Contexts

### Main / GLib context

The application main thread owns user-visible control flow:

- load, play, pause, stop, and seek requests;
- GLib timeout creation/removal and tick callbacks;
- terminal presentation and render-area updates;
- renderer replacement and protocol/configuration changes.

`kitty_direct_image_id` is reserved through a validated terminal allocation reply
before workers start and remains immutable while they run. Main-context probes
reuse terminal-probe key replay and bounded deadlines; workers never read stdin.
Pause retains the reservation; full release happens only after joining workers.
The play entry point joins stale worker handles before resetting playback state
or allocating an image ID. If query bytes were written but no ID was confirmed,
only the protocol boundary is recovered; invisible placeholders may remain until terminal
eviction/reset, since shared image numbers cannot establish ownership. Complete
validated replies from probes that failed terminal restoration are cleaned up by
their confirmed ID, not used for rendering. Pending user keys continue to be
replayed by the existing input owner.
`kitty_direct_image_present` is main-context-only: workers annotate queued frames
with the ID but never change terminal-image ownership or emit cleanup commands.

Public control functions are not a license to invoke the same player
concurrently from arbitrary application threads. Unless a function explicitly
states otherwise, call it from the player-owning main context.

### Decode worker

The decode worker owns sequential FFmpeg packet/frame decoding while running.
It publishes decoded frames through `decode_queue` and observes the atomic
`worker_stop` flag.

### Render workers

Render workers consume `decode_queue`, use worker-local renderers, and publish
`frame_queue` entries. They snapshot shared renderer configuration and layout
state under the locks described below.

## Synchronization Domains

### `render_mutex`

Protects:

- `renderer` and `owns_renderer`;
- reads/writes of shared renderer configuration;
- `color_enhance`, including the fallback configuration used when no shared
  renderer is attached;
- renderer replacement against worker configuration snapshots and seek-preview
  rendering.

Use `video_player_set_color_enhance()` instead of writing these fields from app
or input modules.

### `state_mutex`

Protects:

- playback state (`is_playing`, EOF/draining state, timing and PTS fields);
- `timer_id` bookkeeping (actual GLib source operations remain main-context
  operations and occur after releasing the mutex);
- terminal render-area/layout state and line cache;
- presentation statistics and `show_stats`;
- last rendered frame bounds.

Use `video_player_set_show_stats()`,
`video_player_get_last_frame_bounds()`, and
`video_player_dup_cached_line_at_row()` outside video-player internals.

### `queue_mutex`

Protects:

- `frame_queue` and `decode_queue`;
- queue capacity and `render_in_flight`;
- all queue condition-variable wait/broadcast operations.

`worker_stop` is an atomic integer. It is often changed while `queue_mutex` is
held so condition broadcasts and stop observation form one operation, but every
read/write of the flag itself must use `g_atomic_int_get/set`.

### Atomic fields

- `playback_generation`: invalidates stale decoded/rendered frames;
- `worker_stop`: terminates queue waits and worker loops.

Do not mix atomic and plain access to either field.

## Lock Ordering

Nested acquisition is intentionally limited to these orders:

1. `queue_mutex` -> `state_mutex`
2. `render_mutex` -> `state_mutex`

Never acquire `queue_mutex` or `render_mutex` while holding `state_mutex`.
Never hold `queue_mutex` while joining workers, performing terminal I/O,
running FFmpeg decode calls, or invoking renderer operations. Release locks
before `g_source_remove()`, thread joins, and potentially blocking external
library calls.

Helpers ending in `_locked` require the documented lock to be held and must not
be exposed as general public APIs.

## Object and Renderer Lifetime

- The caller owns the `VideoPlayer *` and must ensure all control calls and GLib
  callbacks have stopped before `video_player_destroy()` returns.
- `video_player_set_renderer()` stops/joins workers before replacing a live
  renderer. An attached external renderer is not owned by the player.
- Worker-local renderers are created and destroyed by their worker paths.
- Returned strings from `video_player_dup_cached_line_at_row()` are caller-owned
  snapshots and must be freed with `g_free()`.

## Sanitizer Notes

`make tsan-test` instruments project code. Distribution-provided GLib and Chafa
libraries are not necessarily built with ThreadSanitizer. Under
`PIXELTERM_TSAN`, `include/common.h` annotates project uses of `GMutex` and
`GCond` with TSan acquire/release edges, while project-owned synchronization
flags and worker stop state use GLib atomics. The TSan target runs a focused
suite of project-owned queue, worker-stop, renderer-replacement, and state API
concurrency tests. It deliberately does not enter Chafa's uninstrumented
persistent pixel-render worker pool; the complete rendering suite remains
covered by normal, ASan, and UBSan runs.

## Review Checklist for Concurrency Changes

- Identify the owning execution context for every new field.
- Assign every shared mutable field to exactly one mutex or atomic domain.
- Preserve the lock order above; avoid introducing a third nested order.
- Do not invoke terminal, FFmpeg, Chafa, GLib source removal, or thread join
  operations while holding a queue/state lock unless an existing documented
  path requires it.
- Add a deterministic regression test and run normal, ASan, TSan, and UBSan
  targets before merging.
