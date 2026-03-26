# PixelTerm-C Roadmap

## Current Status
PixelTerm-C is production-ready as of v1.7.9. Current work is focused on
compatibility hardening, keeping the validated regression baseline documented,
release polish, and clearer user-facing documentation for terminal-specific
behavior without reopening protocol-detection redesign during this phase.

## Near-Term Ideas
- Expand regression coverage for video seek and remaining preview/book navigation flows
- Keep troubleshooting and compatibility documentation aligned with the current protocol behavior and override paths
- Keep Linux and pull request macOS CI aligned with warning-clean build/test and debug validation
- Strengthen troubleshooting and compatibility documentation for end users
- Keep release automation and generated notes aligned with shipped artifacts

## Longer-Term Ideas
- Redesign terminal capability/protocol detection and remote-session fallbacks after the current baseline is stable
- Additional terminal-specific presets and safer default overrides
- More performance diagnostics and render-path profiling
- Packaging and distribution polish for supported platforms
