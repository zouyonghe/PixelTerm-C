# PixelTerm-C Development Guide

## Project Overview

PixelTerm-C is a C implementation of the Python-based PixelTerm terminal image, video, and book browser. This document outlines the development approach, architecture decisions, and implementation roadmap.

**Current Status**: ✅ **PRODUCTION READY** - v1.7.19 with a warning-clean verification baseline that now covers terminal protocol helpers, CLI/startup paths, book core helpers, isolated file-manager/preview-grid/book-preview suites, the paused video-seek target-restore fix, stable EOF drain/replay handling in the video player, the newer video-player/app-config maintainability seams, and the layered auto-protocol resolver path.

## Technical Architecture

### Core Components

#### 1. Main Application (src/main.c, include/app_config_runtime.h, src/app_config_runtime.c)
- Application entry point
- Command line argument parsing and startup path classification
- Runtime config application via `app_config_apply_runtime()`
- Main event loop

#### 2. Core Application (include/app.h, include/app_state.h, src/app.c, src/app_core.c)
`PixelTermApp` now keeps shared render/media state at the top level and embeds mode-specific state buckets from `include/app_state.h`:
- `FileManagerState`
- `PreviewState`
- `BookState`
- `InputState`
- `AsyncState`

State and mode data types now live in `include/app_state.h`; `include/app.h` focuses on public app APIs.
Public API declarations are now split by module:
- `include/app_core.h`
- `include/app_file_manager.h`
- `include/app_preview.h`
- `include/app_book_mode.h`
- `include/app_render.h`
- `include/app_runtime.h`
`include/app.h` remains the compatibility umbrella include.
- `src/app_core.c` owns core state/navigation APIs (`app_load_*`, `app_*image`, `app_get_current_*`, `app_delete_current_image`, `app_open_book`/`app_close_book`).

#### 3. File Browser (include/browser.h, src/browser.c)
- Image directory scanning
- Image file filtering/validation
- File list management for the viewer
- Cached cursor/index navigation (`current` + `current_index`) for lower-cost index jumps

#### 4. File Manager Core (src/app_file_manager.c)
- Directory listing for mixed files/folders
- Hidden file toggling and AaBb sorting
- Directory refresh, selection, paging, and navigation logic
- Selection cache (`selected_link + selected_link_index`) to avoid repeated full-list lookups

#### 4.1 File Manager Render (src/app_file_manager_render.c)
- File manager viewport computation and hit-testing
- Terminal rendering of file manager header/list/footer
- Mouse selection and enter-at-position handling

#### 5. Preview Grid Mode (src/app_preview_grid.c)
- Preview grid layout, selection/navigation, zoom, paging, and mouse hit-testing
- Preview selection cache (`selected_link + selected_link_index`) for grid hot paths

#### 5.1 Preview Render Helpers (src/app_preview_render.c)
- Preview cell rendering and preloader/renderer handoff
- Selection border repaint helpers and selected filename redraw
- Preview info/status block output

#### 5.2 Preview Shared Helpers (src/app_preview_shared.c)
- Shared preview/book-preview grid helpers:
- Grid renderer creation and rendered-line drawing
- Grid cell border/origin helpers and vertical layout offsets

#### 5.3 Book-Preview Mode (src/app_preview_book.c)
- Book-preview rendering/navigation helpers and jump prompt UI

#### 5.4 Book TOC Mode (src/app_book_toc.c)
- Book TOC viewport/layout, hit-test, selection, and rendering helpers

#### 5.5 Book Page Render (src/app_book_page_render.c)
- Book single/double-page rendering pipeline and page image composition

#### 6. Image Renderer (include/renderer.h, src/renderer.c)
- Direct Chafa canvas integration
- Image processing and display
- Caching system

#### 7. Pixbuf Utilities (src/pixbuf_utils.c)
- Shared stream-based pixbuf loading helper used by both app and renderer paths

#### 7.5 UI Render Utilities (src/ui_render_utils.c)
- Shared terminal UI rendering helpers (sync update markers, centered help line, clear helpers, kitty image cleanup, filename width policy)
- Reused by single-image and preview/book rendering paths to avoid duplicate implementations

#### 8. GIF Player (include/gif_player.h, src/gif_player.c)
- Animated GIF decoding and playback
- Frame timing and render window management

#### 8.5 Video Player Core (include/video_player.h, src/video_player.c)
- FFmpeg-backed playback coordination, decode/render queues, and render scheduling
- Delegates clock, seek-preview, and debug/test helpers to focused internal modules

#### 8.6 Video Player Clock Helpers (include/video_player_clock_internal.h, src/video_player_clock.c)
- Fallback PTS tracking and current-position clock helpers shared by playback and seek flows

#### 8.7 Video Player Seek Helpers (include/video_player_seek_internal.h, src/video_player_seek.c)
- Seek target clamping plus seek-preview decode/render helpers
- Preview-test hooks and decode-attempt controls used by the video test suite

#### 8.8 Video Player Debug/Test Helpers (include/video_player_debug_internal.h, src/video_player_debug.c)
- Debug log gating/output, stream lifecycle, queue-wait hooks, and seek test hooks

#### 9. Input Handler (include/input.h, src/input.c)
- Keyboard input processing
- Terminal mode management
- Key mapping

#### 9.5 Input Dispatch (include/input_dispatch.h, src/input_dispatch.c, src/input_dispatch_core.c)
- `src/input_dispatch.c` keeps the public API (`include/input_dispatch.h`) stable.
- `src/input_dispatch_core.c` now focuses on event routing and shared guard logic.

#### 9.6 Input Dispatch Delete Flow (src/input_dispatch_delete.c)
- Delete prompt placement, prompt rendering/clearing, and delete confirmation orchestration

#### 9.7 Input Dispatch Pending Clicks (src/input_dispatch_pending_clicks.c)
- Delayed single-click timeout handling for single/book, preview/book-preview, and file-manager modes

#### 9.8 Input Dispatch Media Helpers (src/input_dispatch_media.c)
- Shared media-type checks used by input dispatch paths (`single` video/animated-image detection).

#### 9.9 Input Dispatch Key Modes
- `src/input_dispatch_key_modes.c`: preview-mode key handlers.
- `src/input_dispatch_key_book.c`: book/book-preview key handlers and book jump/TOC routing.
- `src/input_dispatch_key_single.c`: single-mode key handlers and video key-side helpers.
- `src/input_dispatch_key_file_manager.c`: file-manager key handlers.

#### 9.10 Input Dispatch Mouse Modes (src/input_dispatch_mouse_modes.c)
- Mode-specific mouse press/double-click/scroll handlers and single-view zoom anchor logic.

#### 11. App Mode Transition (src/app_mode.c)
- `app_transition_mode()` centralizes mode switching and validation, backed by a table-driven transition mask and per-mode enter/exit hooks.

#### 10. Preloading System (include/preloader.h, src/preloader.c)
- Multi-threaded image preloading
- Memory management
- Cache coordination

## Implementation Strategy

### Phase 1: Minimal Viable Product ✅ COMPLETED
- [x] Basic image display using Chafa
- [x] Simple keyboard navigation (left/right arrows)
- [x] Directory scanning
- [x] Basic error handling

### Phase 2: Core Features ✅ COMPLETED
- [x] Complete keyboard support (arrows, hjkl in image/preview, i, r, Tab)
- [x] Preloading system implementation
- [x] File management (delete, info display)
- [x] Memory optimization

### Phase 3: Advanced Features ✅ COMPLETED
- [x] Performance optimization
- [x] Linux/macOS compatibility work
- [x] Advanced configuration options
- [x] Comprehensive testing

## Refactoring Roadmap

The codebase is now split into mode-focused modules and routed input handlers, while keeping `app_*` APIs stable.
For a structured refactor plan with safe, incremental steps, see:

- `docs/project/REFACTORING_PLAN.md`
- `docs/project/archive/refactor-plan.md` (archived execution-focused companion plan)

The current pass intentionally limits itself to maintainability seams around existing behavior; broader `PixelTermApp` state-bucket redesign and terminal protocol/probe redesign remain later work.

Performance changes should follow the workflow notes in this section and keep benchmarks consistent across runs.

## Key Design Decisions

### 1. Direct Chafa Integration
Instead of using subprocess calls like the Python version, we use Chafa as a library:

```c
// Direct library call (fast)
ChafaCanvas *canvas = chafa_canvas_new(config);
chafa_canvas_draw_all_pixels(canvas, pixel_type, pixels, width, height, rowstride);
GString *output = chafa_canvas_print(canvas, term_info);
```

### 2. Memory Management Strategy
- Use GLib memory management (g_malloc, g_free) and GObject reference counting
- Free render results deterministically and cache only needed content

### 3. Threading Model
- Main thread: UI and input handling
- Preload thread: Background image processing
- Mutex protection for shared data structures

### 4. Error Handling
- Use GLib error handling (GError)
- Graceful degradation for unsupported features
- Clear, user-facing error messages where needed

## Performance Optimizations

### 1. Zero-Copy Rendering
- Avoid unnecessary data copying where possible
- Minimize allocations in hot rendering paths

### 2. Smart Caching
- LRU cache for rendered images
- Predictive preloading based on navigation patterns
- Memory pressure handling

### 3. Efficient I/O
- Stream-based image loading for long paths and robust error handling

## Development Environment Setup

### Dependencies
- GCC or Clang with C11 support
- Chafa development libraries
- GLib 2.0 development libraries
- Python 3.9+ for repository maintenance and script-backed test checks
- GDK-Pixbuf development libraries
- GIO development libraries (glib)
- FFmpeg development libraries (`libavformat`, `libavcodec`, `libswscale`, `libavutil`)
- MuPDF development libraries for book support (optional)
- `pkg-config`
- Make or compatible build system

### Build System
The project ships a Makefile using pkg-config. Typical commands:
```bash
make
# Outputs: bin/pixelterm

sudo make install
# Installs: /usr/local/bin/pixelterm by default

make debug
make ARCH=aarch64
```

## Testing Strategy

### Unit Tests
- `make test` builds and runs `bin/pixelterm-tests`, `bin/pixelterm-file-manager-tests`, `bin/pixelterm-preview-grid-tests`, and `bin/pixelterm-book-preview-tests`, then runs `scripts/test_install_script.py` to verify the installer/docs integration
- `bin/pixelterm-tests` directly covers common utilities plus browser, renderer, GIF player, terminal probe/protocol resolver helpers, CLI/startup paths, app-mode transitions, book core helpers, and the paused video-seek target-restore path
- `bin/pixelterm-file-manager-tests`, `bin/pixelterm-preview-grid-tests`, and `bin/pixelterm-book-preview-tests` keep those mode-specific suites isolated from the main test linker graph
- Targeted automated coverage should still be added when refactors touch routing, rendering, or state helpers outside the current baseline

### Integration Tests
- Manual end-to-end testing in supported terminals is still required for real protocol/render behavior

### CI Baseline
- Linux CI installs MuPDF, validates `pkg-config --modversion mupdf` and `pkg-config --libs mupdf`, then runs `make EXTRA_CFLAGS=-Werror`, `make EXTRA_CFLAGS=-Werror test`, and `make EXTRA_CFLAGS=-Werror debug`
- Pull request macOS CI runs `make EXTRA_CFLAGS=-Werror`, `make EXTRA_CFLAGS=-Werror test`, and `make EXTRA_CFLAGS=-Werror debug`

### Performance Tests
- Startup time measurement
- Image switching latency
- Memory usage profiling

### Verification Baseline
- The current baseline can switch between warning-clean and debug builds without a manual `clean` step.
- `make EXTRA_CFLAGS=-Werror`
- `make EXTRA_CFLAGS=-Werror test`
- `make EXTRA_CFLAGS=-Werror debug`

## Code Style Guidelines

### Naming Conventions
- Functions: snake_case (e.g., load_image_file)
- Variables: snake_case (e.g., current_index)
- Types: PascalCase (e.g., PixelTermApp)
- Constants: UPPER_SNAKE_CASE (e.g., MAX_CACHE_SIZE)

### Documentation
- All public functions must have Doxygen comments
- Complex algorithms need inline comments
- Architecture decisions documented in this file

### Error Handling
- Always check return values
- Use GError for propagating errors
- Provide meaningful error messages

## Debugging and Profiling

### Debug Builds
`make debug` enables AddressSanitizer with debug symbols.

### Profiling
- Use gprof for performance profiling
- Valgrind for memory leak detection
- Custom timing macros for critical sections

## Release Process

### Version Management
- Semantic versioning (MAJOR.MINOR.PATCH)
- Git tags for releases
- Change log maintenance

### Distribution
- GitHub release binaries for supported platform/architecture combinations
- Source builds that produce `bin/pixelterm`
- `make install` support for installing `$(PREFIX)/bin/pixelterm`

## Future Enhancements

### Short Term (Next 6 months)
- Keep Linux and pull request macOS CI aligned with `make EXTRA_CFLAGS=-Werror`, four-binary `make EXTRA_CFLAGS=-Werror test`, and `make EXTRA_CFLAGS=-Werror debug` validation
- Keep protocol behavior documentation and troubleshooting aligned with the current detection and override paths
- Add safer terminal-specific presets and default overrides for terminals that still need manual configuration

### Long Term (Next year)
- Revisit broader `PixelTermApp` state-bucket redesign only after the current helper seams and regression coverage are stable
- Add safer terminal-specific presets and default overrides
- Improve performance diagnostics and render-path profiling
- Continue packaging and distribution polish for supported platforms
- Revisit tmux/screen passthrough and broader remote-session heuristics after the new direct-SSH policy settles

## Contributing Guidelines

1. Fork the repository
2. Create a feature branch
3. Write tests for new functionality
4. Ensure all tests pass
5. Submit a pull request

## Resources

### Documentation
- [Chafa API Reference](https://hpjansson.org/chafa/ref/)
- [GLib Documentation](https://docs.gtk.org/glib/)
- [POSIX Terminal Programming](https://tldp.org/HOWTO/Text-Terminal-HOWTO.html)

### Tools
- Valgrind (memory debugging)
- GDB (debugging)
- Perf (performance analysis)
- Clang Static Analyzer

### Community
- GitHub Issues (bug reports)
- Chafa mailing list (technical discussions)
- Terminal emulator forums (compatibility issues)

## Notes for Future Developers

This project is designed to be maintainable and extensible. The modular architecture allows for incremental feature work, compatibility fixes, and profiling without returning to large monolithic source files.

Remember: Performance is not just about speed, but also about responsiveness, resource usage, and user experience. Keep these principles in mind when making architectural decisions.
