# PixelTerm-C Roadmap

## Current Status
PixelTerm-C is production-ready as of v1.7.14. The current short-term baseline
now includes non-overlapping preview/book pagination behavior, paused video
seek target restoration, stable EOF drain-to-stop playback without tail-frame
replay, matching regression coverage, and refreshed resolver/override docs.
Near-term follow-up is focused on release polish, terminal-specific presets,
CI/release upkeep, and broader performance/packaging follow-up after that
verification pass.

## Near-Term Ideas
- Keep Linux and pull request macOS CI aligned with `make EXTRA_CFLAGS=-Werror`, four-binary `make EXTRA_CFLAGS=-Werror test`, and `make EXTRA_CFLAGS=-Werror debug` validation
- Add safer terminal-specific presets and user-facing override guidance for terminals that still need manual configuration
- Keep release automation and generated notes aligned with shipped artifacts and current version metadata
- Follow up on performance diagnostics and packaging polish now that the short-term regression and documentation pass has landed

## Longer-Term Ideas
- Additional terminal-specific presets and safer default overrides
- Deeper performance diagnostics and render-path profiling
- Broader packaging and distribution polish for supported platforms
- Revisit tmux/screen passthrough and broader remote-session heuristics after the direct-SSH model settles
