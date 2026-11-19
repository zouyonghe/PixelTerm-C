# Terminal lifecycle and media presentation design

## Goal and scope

Consolidate the terminal lifecycle affected by PR #47 and the recovery work in
PR #48. Preserve RGB24 video output, existing CLI/configuration, image geometry,
alpha/gamma fallback, and main-thread presentation. Do not rewrite decoding, add
dependencies, or perform unrelated formatting.

## Current evidence

- Protocol encoding and a process-wide recovery queue currently coexist in
  `kitty_graphics.c`; UI and input modules consequently depend on Kitty internals.
- Readiness checks are scattered across event dispatch, GIF, video and UI paths.
- Termios recovery and output-control restoration have different lifetimes but
  have historically shared flags such as `raw_mode_enabled`.
- The current uncommitted policy work adds replay-safe controls, but fails the
  independent preview-grid test link: UI helpers now require Kitty recovery
  symbols absent from that target. It is not a verified baseline.
- Preserve these local edits as migration input, not as finished production code.

## Boundaries

### Terminal output (`terminal_output.c/.h`)

Own readiness, checked submissions, failed-buffer disposal, ST framing, retry
deadlines and deferred output ownership. This is independent of image encoding.
Use the existing C/GLib style and process main-context ownership; do not add a
worker, generic framework, or second output queue. Every platform-specific purge
belongs here.

Queued operations have two explicit policies:

| Policy | Before submission | After failed/ambiguous submission |
| --- | --- | --- |
| Destructive once | Keep owned bytes until sent | Discard buffered bytes; retain only ST, never replay deletion |
| Idempotent restore | Keep owned bytes until sent | Retain ST-prefixed restore until success |

Use the same checked write/flush result for both. Ordinary frame failures are
not queued for replay: discard the frame, recover framing, and report failure.
Coalesce identical pending restore controls. Use a 100 ms background retry
interval after failure; explicit shutdown attempts may run immediately.

### Terminal session (`input.c` and `terminal_probe.c`)

Retain one authoritative user-original termios snapshot for outer restoration.
A probe may temporarily restore application raw settings, but must never
overwrite pending user-original cleanup. Termios restoration is independent of
stdout readiness. Mouse disable, alternate-screen exit, cursor show and DEC 2026
end are idempotent restore operations whose ownership survives the input handler.

Do not introduce a second session object merely to relocate InputHandler fields.
Use existing lifecycle entry points, remove redundant pending flags, and make
ownership transfer explicit in their contracts.

### Media and UI

Kitty module encodes commands and allocates confirmed image IDs. GIF/video
workers build frames; the main context submits them through terminal output.
UI renderers do not implement retry/purge policy. Event-loop checkpoints use one
output readiness API. A stage that queues recovery prevents subsequent stages
from writing in that iteration. Startup and teardown use that same contract.

## Error and ownership rules

1. Unsent work remains owned; no delete or restore is freed merely because output
   is blocked.
2. Delivery ambiguity is not success and is not proof of non-delivery. Never
   retry a destructive operation based solely on failed fwrite/fflush.
3. A safe restore remains pending until checked success; raw-mode flags describe
   termios ownership, not outstanding output controls.
4. Stop producers/join workers before releasing resources referenced by frames.
5. Keep primary application errors; return cleanup failure if no primary error
   exists and report an unresolved secondary cleanup failure once. EOF, temporary
   frame absence, blocked output and actual write failure remain distinct.
6. Permanent disconnect cannot guarantee terminal restoration. Return failure
   without claiming successful cleanup; do not spin indefinitely during shutdown.

## Verification matrix

| Scenario | Observable requirement |
| --- | --- |
| Partial write; failure with bytes still buffered | Purge before ST or later output |
| Delete reached terminal but returned failure | At most one destructive delete |
| Blocked, never-submitted delete | Sent once after recovery, even if player freed |
| Blocked mouse/cursor/alternate-screen/sync-end | Restore appears after ST when sink recovers |
| Event, animation or async render queues recovery | No later stage emits until readiness |
| Recover helper fails | Next background attempt waits at least 100 ms |
| Outer restore and probe restore both fail | Retry applies user-original termios |
| Restore fails twice then succeeds / fails three times | Correct final result and one diagnostic |
| Pause, seek, EOF replay, resize, protocol changes | Existing timing, generations and geometry preserved |
| Standalone preview/file-manager/book test links | No accidental dependency on media implementation |

Run full warning-clean normal, UBSan, ASan and TSan targets. Preserve independent
test binaries, platform CI, leak checks and negative/mutation tests. Do not infer
real terminal visual performance from PTY throughput or Linux-only verification.

## Migration and completion

Document contracts first; extract the one output owner; move deferred controls;
route media/UI writes; remove obsolete Kitty recovery APIs and duplicate guards.
Land each functional slice only when its tests pass. Do not merge #48 while a
refactor or required review finding is unverified. Completion requires build/link
checks for every test target, full suites, and Linux/macOS CI on the pushed head.
