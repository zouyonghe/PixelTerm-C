# PixelTerm-C Project Status

## Overview
- **Current Version**: v1.4.0
- **Status**: Production ready
- **Core Dependencies**: chafa, glib-2.0, gdk-pixbuf, gio-2.0, pthread

## Repository Structure
```
PixelTerm-C/
├── README.md
├── README_zh.md
├── CHANGELOG.md
├── Makefile
├── src/
├── include/
├── docs/
├── completions/
├── screenshots/
└── stage/
```

## Feature Summary
- Image viewing for common formats (PNG, JPEG, GIF, WebP, TIFF, BMP)
- Animated GIF playback
- File browser with preview grid and paging
- Background preloading and LRU cache
- Mouse and keyboard navigation
- Dithering toggle and work-factor quality control

## Testing
- Manual verification on supported terminals
- Debug build targets for sanitizer-assisted runs

## Notes
- Documentation updates should reflect behavior in `src/` and `include/`.
