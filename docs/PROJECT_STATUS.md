# PixelTerm-C Project Status

## Overview
- **Current Version**: v1.7.8
- **Status**: Production ready
- **Core Dependencies**: chafa, glib-2.0, gdk-pixbuf-2.0, gio-2.0, FFmpeg libs, pthread; MuPDF optional for book support

## Repository Structure
```
PixelTerm-C/
├── README.md
├── README_zh.md
├── CHANGELOG.md
├── Makefile
├── config.example.ini
├── src/
├── include/
├── docs/
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
- `make test` automated coverage for core app, browser, renderer, GIF, and text utilities
- Manual verification on supported terminals
- Debug build targets for sanitizer-assisted runs

## Notes
- Default source build output is `bin/pixelterm`.
- `make install` installs `/usr/local/bin/pixelterm` by default (override with `PREFIX`/`DESTDIR`).
