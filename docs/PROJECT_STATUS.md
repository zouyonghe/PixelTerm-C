# PixelTerm-C Project Status

## Overview
- **Current Version**: v1.3.8
- **Status**: Production ready
- **Core Dependencies**: chafa, glib-2.0, gdk-pixbuf, pthread

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
- File browser with preview grid
- Background preloading and LRU cache
- Mouse and keyboard navigation

## Testing
- Manual verification on supported terminals
- Debug build targets for sanitizer-assisted runs

## Notes
- Documentation updates should reflect behavior in `src/` and `include/`.
