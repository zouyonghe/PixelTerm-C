# VideoPlayer Threading and Locking Contract

This document defines ownership and synchronization rules for `VideoPlayer`.
It applies to the public API in `include/video_player.h` and the implementation
in `src/video_player*.c`.

## Execution Contexts

### Main / GLib context

The application main thread owns user-visible control flow:

- load, play, pause, stop, and seek requests;
- GLib timeout creation/removal and tick callbacks;
- terminal presentation and render-area updates;
- renderer replacement and protocol/configuration changes.

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
flags and worker stop state use GLib atomics. The one test that intentionally
enters Chafa's uninstrumented internal worker pool is skipped only in the TSan
build; it remains covered by normal, ASan, and UBSan runs.

## Review Checklist for Concurrency Changes

- Identify the owning execution context for every new field.
- Assign every shared mutable field to exactly one mutex or atomic domain.
- Preserve the lock order above; avoid introducing a third nested order.
- Do not invoke terminal, FFmpeg, Chafa, GLib source removal, or thread join
  operations while holding a queue/state lock unless an existing documented
  path requires it.
- Add a deterministic regression test and run normal, ASan, TSan, and UBSan
  targets before merging.
