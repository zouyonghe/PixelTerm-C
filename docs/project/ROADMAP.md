# PixelTerm-C Roadmap

## Current Status
PixelTerm-C is production-ready as of v1.7.11. Current work is focused on
regression coverage expansion, release polish, terminal-specific presets and
documentation refinement, and broader performance/packaging follow-up after the
protocol-detection hardening pass.

## Near-Term Ideas
- Expand regression coverage for video seek and remaining preview/book navigation flows
- Keep troubleshooting and compatibility documentation aligned with the current resolver and override paths
- Keep Linux and pull request macOS CI aligned with warning-clean build/test and debug validation
- Add safer terminal-specific presets and user-facing override guidance
- Keep release automation and generated notes aligned with shipped artifacts

## Longer-Term Ideas
- Additional terminal-specific presets and safer default overrides
- More performance diagnostics and render-path profiling
- Packaging and distribution polish for supported platforms
- Revisit tmux/screen passthrough and broader remote-session heuristics after the direct-SSH model settles
