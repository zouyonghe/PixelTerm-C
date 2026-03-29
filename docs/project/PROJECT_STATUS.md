# PixelTerm-C Project Status

## Overview
- **Current Version**: v1.7.17
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
- `make test` builds and runs `bin/pixelterm-tests`, `bin/pixelterm-file-manager-tests`, `bin/pixelterm-preview-grid-tests`, and `bin/pixelterm-book-preview-tests`, then runs `scripts/test_install_script.py` to verify the installer/docs path
- `bin/pixelterm-tests` covers browser, renderer, GIF/text/common utilities, terminal probe/protocol resolver helpers, CLI/startup behavior, book core helpers, and video playback/seek regressions, including the paused-seek target-restore path
- `bin/pixelterm-file-manager-tests` isolates file-manager navigation and selection-state regressions
- `bin/pixelterm-preview-grid-tests` isolates preview-grid zoom, pagination, and short-last-page selection normalization regressions
- `bin/pixelterm-book-preview-tests` isolates book-preview pagination and page-move normalization regressions
- Manual verification is still required for terminal-specific rendering and protocol behavior
- Debug build targets remain part of the verification baseline

## CI Baseline
- Linux CI validates MuPDF `pkg-config` metadata, then runs `make EXTRA_CFLAGS=-Werror`, `make EXTRA_CFLAGS=-Werror test`, and `make EXTRA_CFLAGS=-Werror debug`
- Pull request macOS CI runs the same `make EXTRA_CFLAGS=-Werror`, `make EXTRA_CFLAGS=-Werror test`, and `make EXTRA_CFLAGS=-Werror debug` path without the Linux-specific MuPDF metadata check

## Notes
- Default source build output is `bin/pixelterm`.
- `make install` installs `/usr/local/bin/pixelterm` by default (override with `PREFIX`/`DESTDIR`).
- Shipped behavior and automated coverage now include the layered auto-protocol resolver, bounded probe transport, direct-SSH conservative fallback behavior, non-overlapping short-last-page preview/book pagination, and paused video seek target restoration after seek-preview redraw so repeated paused seeks stay anchored to the latest requested position.
