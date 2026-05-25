# PixelTerm-C Code Review Report

Date: 2026-05-25
Scope: current code in `/Users/buding/Code/PixelTerm-C`

## Summary

Reviewed the current C codebase with emphasis on hidden races, media decode/render paths, preloading, document rendering, and file deletion behavior. The highest-risk area is video playback: the player mixes GLib timeout callbacks, decode/render worker threads, and public controls while many shared fields are read or written without a single synchronization rule.

Existing unit coverage is broad, but most concurrency-sensitive cases are deterministic hook tests rather than sanitizer-backed race tests. The findings below are based on static source inspection.

## Findings

### High: VideoPlayer shared state has unsynchronized cross-thread access

Files:
- `include/video_player.h`
- `src/video_player.c`

Evidence:
- `VideoPlayer` stores playback state, EOF state, timer id, FFmpeg pointers, layout fields, and worker flags in one struct: `include/video_player.h:33`.
- Some helpers use `state_mutex`, for example `video_player_is_eof_pending()` and `video_player_is_eof_ended()` in `src/video_player.c:128` and `src/video_player.c:139`.
- Other paths read or write the same state without the mutex:
  - `video_player_schedule_tick()` reads `player->is_playing` and mutates `player->timer_id` at `src/video_player.c:287`.
  - `video_player_tick()` reads/writes `player->is_playing` and `player->timer_id` before taking `state_mutex` at `src/video_player.c:1450`.
  - `video_player_play()`, `video_player_pause()`, and `video_player_stop()` update `is_playing`, `timer_id`, frame layout state, and timing state directly at `src/video_player.c:2105`, `src/video_player.c:2146`, and `src/video_player.c:2162`.
  - The decode worker reads and writes `player->draining` without a lock at `src/video_player.c:1509` and `src/video_player.c:1512`; EOF handlers also write it directly at `src/video_player.c:383` and `src/video_player.c:395`.
  - `video_player_render_frame()` reads and writes render layout fields directly at `src/video_player.c:1783` through `src/video_player.c:1904`, while `video_player_set_render_area()` writes those fields under `state_mutex` at `src/video_player.c:1162`.

Impact:
The code has real C data races if playback controls, GLib timeout callbacks, terminal resize handling, EOF handling, or worker activity overlap. Symptoms can include duplicate or lost timers, playback stopping unexpectedly, stuck EOF handling, stale frames after seek/resize, or undefined behavior under ThreadSanitizer.

Recommendation:
Define one ownership rule for every `VideoPlayer` field. A practical first step is to keep FFmpeg decode state worker-owned after startup, keep timer operations main-context-only, and move all shared playback/layout/EOF fields behind `state_mutex` or atomics. Add a ThreadSanitizer job or targeted TSAN test binary for play/pause/seek/resize/EOF overlap.

### High: Renderer pointer replacement is unsafe while video workers can run

Files:
- `src/video_player.c`

Evidence:
- `video_player_set_renderer()` can destroy the owned renderer and replace `player->renderer` under `render_mutex` at `src/video_player.c:1137`.
- The decode worker checks `player->renderer` without `render_mutex` at `src/video_player.c:1501`.
- `video_player_start_worker()` uses `!player->renderer` without `render_mutex` at `src/video_player.c:1271`.
- Render worker config reads the renderer under `render_mutex` at `src/video_player.c:1287`, but that does not protect the unchecked reads elsewhere.

Impact:
If a renderer is swapped while playback is active or workers are starting/stopping, workers may observe stale or inconsistent renderer state. The common setup path may not hit this, but the public API does not enforce "only before playback".

Recommendation:
Either make renderer replacement illegal while workers are running, or stop/join workers before replacing the renderer. Document the API contract and assert it in debug builds.

### Medium: Preloader configuration fields are not consistently protected

Files:
- `include/preloader.h`
- `src/preloader.c`

Evidence:
- `ImagePreloader` stores config fields such as terminal size, dither, protocol flags, work factor, gamma, and color enhancement in `include/preloader.h:31`.
- `preloader_initialize()` writes these config fields without `preloader->mutex` at `src/preloader.c:217`.
- `preloader_update_terminal_size()` writes terminal dimensions under the mutex at `src/preloader.c:695`.
- The worker snapshots terminal size under the mutex at `src/preloader.c:719`, but then reads dither/protocol/gamma/color config without the mutex while building `RendererConfig` at `src/preloader.c:725`.
- While processing a task, the worker calls `preloader_normalize_dims()` at `src/preloader.c:793`; that helper can read `term_width` and `term_height` without taking the mutex at `src/preloader.c:45`.

Impact:
Most current call sites appear to initialize after stopping the worker, but the API itself allows concurrent configuration and worker access. This is another TSAN-visible race and can render cached entries at inconsistent dimensions or with mixed config if future code calls initialization/update while running.

Recommendation:
Make `preloader_initialize()` require an idle preloader or take the mutex and signal/restart safely. Avoid reading any `ImagePreloader` mutable fields outside the mutex; pass normalized dimensions/config snapshots into worker-local variables.

### Medium: Public preloader cache cleanup mutates shared structures without locking

Files:
- `include/preloader.h`
- `src/preloader.c`

Evidence:
- `preloader_cache_cleanup()` is declared as a public function in `include/preloader.h:262`.
- It reads and mutates `preload_cache` and `lru_queue` without locking `preloader->mutex` at `src/preloader.c:631`.
- It is currently called internally by `preloader_cache_add()` while the mutex is already held at `src/preloader.c:563`, which is safe only because the helper assumes its caller owns the lock.

Impact:
Any external caller can corrupt the hash table/LRU queue if cleanup overlaps with worker cache insertions, cache reads, or cache clears. The public declaration makes that misuse easy.

Recommendation:
Split this into a private `_locked` helper for `preloader_cache_add()` and a public wrapper that takes `preloader->mutex`, or remove the public declaration if no external caller should use it.

### Medium: MuPDF page target pixel dimensions can overflow before clamping

Files:
- `src/book.c`

Evidence:
- `book_render_page()` calculates pixel dimensions with `gint target_px_w = target_cols * cell_w;` and `gint target_px_h = target_rows * cell_h;` at `src/book.c:262`.
- The later `max_dim = 4096.0` clamp only runs after these signed integer multiplications.

Impact:
Very large or corrupted terminal geometry values can overflow `gint` before the renderer clamps scale. That can produce negative or very small dimensions, incorrect scaling, or unexpected MuPDF allocation behavior.

Recommendation:
Use checked multiplication (`g_size_checked_mul` or explicit `gint64`) before converting to a render scale. Clamp target pixel dimensions before calling MuPDF.

### Low: `preloader_stop()` does not match its documented queue-clearing contract

Files:
- `include/preloader.h`
- `src/preloader.c`

Evidence:
- The header says `preloader_stop()` "clears any remaining tasks in the queue" at `include/preloader.h:108`.
- The implementation signals stop, joins the thread, and sets status idle, but does not clear `task_queue` at `src/preloader.c:265`.

Impact:
Pending tasks can survive a stop/start cycle. In current flows this may be harmless or even intentional, but it is surprising when toggling render settings or reusing the preloader after a stop.

Recommendation:
Either clear `task_queue` in `preloader_stop()` or update the comment and ensure callers explicitly call `preloader_clear_queue()` when they need old work discarded.

### Low: Media buffer rejection log limit is off by one

Files:
- `src/media_buffer.c`

Evidence:
- `MEDIA_BUFFER_REJECTION_LOG_LIMIT` is `32` at `src/media_buffer.c:5`.
- `media_buffer_debug_reject()` suppresses only when `previous_count > MEDIA_BUFFER_REJECTION_LOG_LIMIT` at `src/media_buffer.c:11` and prints the suppression notice when `previous_count == MEDIA_BUFFER_REJECTION_LOG_LIMIT` at `src/media_buffer.c:14`.

Impact:
The function allows 32 detailed logs plus one suppression notice. If the intended limit is exactly 32 total log emissions, this is off by one. If the intended behavior is 32 detailed messages plus one notice, the constant/comment should say so.

Recommendation:
Clarify the limit semantics or change the comparison to match the intended count.

## Suggested Follow-Up Tests

- Add a ThreadSanitizer build target that exercises video play/pause/stop/seek while a worker is decoding and while terminal layout changes.
- Add a preloader concurrency test that starts the worker, repeatedly updates terminal size/config, and queues tasks under TSAN.
- Add a MuPDF render unit test or internal helper test for oversized target column/row values to prove checked clamping occurs before multiplication.
- Add a regression test for `preloader_stop()` semantics once the intended queue behavior is chosen.

## Notes

- This report does not claim all findings are user-triggerable through the current UI. The concurrency items are still worth fixing because the public APIs and worker topology currently rely on implicit call ordering that is not encoded in the code.
- No code fixes are included in this report; it is intended as the requested bug/defect/race assessment.
