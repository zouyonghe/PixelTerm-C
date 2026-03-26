# PixelTerm-C Project Status

## Overview
- **Current Version**: v1.7.9
- **Status**: Production ready
- **Core Dependencies**: chafa, glib-2.0, gdk-pixbuf-2.0, gio-2.0, FFmpeg libs, pthread; MuPDF optional for book support

## Repository Structure
```
PixelTerm-C/
├── README.md
├── CHANGELOG.md
├── CONTRIBUTING.md
├── Makefile
├── config.example.ini
├── src/
├── include/
├── docs/
│   ├── README.md
│   ├── development/
│   ├── guides/
│   ├── i18n/
│   └── project/
├── tests/
├── screenshots/
└── .github/
```

## Feature Summary
- Image viewing for common formats (PNG, JPEG, GIF, WebP, TIFF, BMP)
- Animated GIF playback
- Video playback via FFmpeg-backed decoding
- PDF/EPUB/CBZ reading with preview and reader modes
- File browser with preview grid and paging
- Background preloading and LRU cache
- Mouse and keyboard navigation
- Dithering toggle and work-factor quality control

## Testing
- `make test` builds and runs `bin/pixelterm-tests`, `bin/pixelterm-file-manager-tests`, and `bin/pixelterm-preview-grid-tests`
- `bin/pixelterm-tests` directly covers browser, renderer, GIF/text/common utilities, terminal protocol helpers, CLI/startup behavior, and book core helpers
- Dedicated file-manager and preview-grid binaries keep those mode-specific suites isolated instead of overloading the main test binary
- Manual verification is still required for terminal-specific rendering and protocol behavior
- Debug build targets remain part of the verification baseline

## CI Baseline
- Linux CI installs MuPDF, validates its `pkg-config` metadata, runs warning-clean build/test checks, and exercises `make debug`
- Pull request macOS CI runs the warning-clean build/test path and `make debug`

## Notes
- Default source build output is `bin/pixelterm`.
- `make install` installs `/usr/local/bin/pixelterm` by default (override with `PREFIX`/`DESTDIR`).
- Current automated protocol coverage validates existing helper/startup behavior; broader protocol-detection redesign remains later work.
