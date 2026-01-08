# PixelTerm-C Development Guide

## Project Overview

PixelTerm-C is a C implementation of the Python-based PixelTerm terminal image browser. This document outlines the development approach, architecture decisions, and implementation roadmap.

**Current Status**: ✅ **PRODUCTION READY** - v1.4.0 with mouse support, animated GIF playback, dithering control, input refactor, and preview paging polish.

## Technical Architecture

### Core Components

#### 1. Main Application (src/main.c)
- Application entry point
- Command line argument parsing
- Main event loop

#### 2. Core Application (src/app.h, src/app.c)
```c
typedef struct {
    ChafaCanvas *canvas;
    ChafaCanvasConfig *canvas_config;
    ChafaTermInfo *term_info;

    GList *image_files;
    gchar *current_directory;
    gint current_index;
    gint total_images;

    ImagePreloader *preloader;
    GifPlayer *gif_player;

    gboolean preview_mode;
    gboolean file_manager_mode;
    gboolean preload_enabled;
    gboolean dither_enabled;
    gint render_work_factor;
    gboolean force_sixel;

    gint term_width;
    gint term_height;
} PixelTermApp;
```
See `include/app.h` for the full state definition.

#### 3. File Browser (src/browser.h, src/browser.c)
- Image directory scanning
- Image file filtering/validation
- File list management for the viewer

#### 4. File Manager (src/app.c)
- Directory listing for mixed files/folders
- Hidden file toggling and AaBb sorting
- Selection, paging, and navigation logic

#### 5. Image Renderer (src/renderer.h, src/renderer.c)
- Direct Chafa canvas integration
- Image processing and display
- Caching system

#### 6. GIF Player (src/gif_player.h, src/gif_player.c)
- Animated GIF decoding and playback
- Frame timing and render window management

#### 7. Input Handler (src/input.h, src/input.c)
- Keyboard input processing
- Terminal mode management
- Key mapping

#### 8. Preloading System (src/preloader.h, src/preloader.c)
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
- [x] Cross-platform compatibility
- [x] Advanced configuration options
- [x] Comprehensive testing

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
- GDK-Pixbuf development libraries
- GIO development libraries (glib)
- Make or compatible build system

### Build System
The project ships a Makefile using pkg-config. Typical commands:
```bash
make
# Outputs: bin/pixelterm

make debug
make ARCH=aarch64
```

## Testing Strategy

### Unit Tests
- Manual spot checks for core workflows
- Targeted testing when adding new rendering logic

### Integration Tests
- Manual end-to-end testing in supported terminals

### Performance Tests
- Startup time measurement
- Image switching latency
- Memory usage profiling

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
- Static binary compilation
- Dependency checking
- Installation scripts

## Future Enhancements

### Short Term (Next 6 months)
- GUI configuration interface
- Plugin system for custom renderers
- Advanced image processing filters

### Long Term (Next year)
- Network image browsing
- Cloud storage integration
- Mobile terminal support

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

This project is designed to be maintainable and extensible. The modular architecture allows for easy addition of new features and optimization opportunities. The performance gains over the Python version are significant, but we should continue to optimize and improve the user experience.

Remember: Performance is not just about speed, but also about responsiveness, resource usage, and user experience. Keep these principles in mind when making architectural decisions.
