# PixelTerm-C Technical Architecture

## Performance Analysis

### Why C Implementation?

The Python version of PixelTerm suffers from several performance bottlenecks:

1. **Subprocess Overhead**: Each image rendering requires spawning a chafa process
2. **Python Interpretation**: High-level language overhead for tight loops
3. **Memory Copying**: Multiple data transfers between processes
4. **GIL Limitations**: Global Interpreter Lock limits true parallelism

### Expected Performance Gains

| Operation | Python (ms) | C (ms) | Improvement |
|-----------|-------------|---------|-------------|
| Startup | 1000-2000 | 200 | 5-10x |
| Image Switch | 200-500 | 50-100 | 3-5x |
| Memory Usage | 50-100MB | 20-30MB | 2-3x |

## Core Data Structures

### Mode Transition Control

- `src/app_mode.c` provides `app_transition_mode(PixelTermApp*, AppMode)` as the unified mode switch entry.
- Current guardrail stage uses a table-driven transition mask (currently all valid transitions allowed) and per-mode enter/exit hooks for side effects (transient input state cleanup and media stop when entering non-single modes).
- `app_set_mode()` remains as a low-level compatibility API and should not be used for new code paths.
- Core app state/navigation APIs have been split into `src/app_core.c` (`app_load_*`, `app_*image` navigation, current-file accessors, book open/close, and delete flow).
- File manager mode implementation has been split into:
  - `src/app_file_manager.c` for directory refresh/navigation and selection state logic.
  - `src/app_file_manager_render.c` for viewport/hit-test and terminal rendering logic.
- Single-image render/refresh flow has been split into `src/app_single_render.c`.
- Preview grid interaction/render logic has been split into `src/app_preview_grid.c`.
- Shared preview/book-preview grid helpers now live in `src/app_preview_shared.c`.
- Book-preview interaction/render logic now lives in `src/app_preview_book.c`.
- Book TOC interaction/render logic has been split into `src/app_book_toc.c`.
- Book page render logic has been split into `src/app_book_page_render.c`.
- Shared terminal UI helper logic has been centralized in `src/ui_render_utils.c` (sync-update wrappers, centered help rendering, clear-area helpers, and kitty image cleanup), reducing duplicated rendering glue across modes.
- Input dispatch now uses split helpers:
  - `src/input_dispatch_media.c` for media checks (single-mode video/animated detection),
  - `src/input_dispatch_key_modes.c` for preview-mode key flows,
  - `src/input_dispatch_key_book.c` for book/book-preview key flows and book jump state handling,
  - `src/input_dispatch_key_single.c` for single-mode key flows and video key actions,
  - `src/input_dispatch_key_file_manager.c` for file-manager key flows,
  - `src/input_dispatch_mouse_modes.c` for mode mouse handlers and single-image zoom interactions.
  This keeps `src/input_dispatch_core.c` focused on routing and shared flow control.
- List-navigation hot paths now use index+pointer hints instead of repeated head-based lookup:
  - `FileManagerState.selected_link` / `selected_link_index`
  - `PreviewState.selected_link` / `selected_link_index`
  - `FileBrowser.current_index`
  This reduces repeated linear random-access calls in high-frequency render/input paths.

### Main Application Structure

State and mode definitions are isolated in `include/app_state.h`.
Public app APIs are split into `include/app_core.h`, `include/app_file_manager.h`,
`include/app_preview.h`, `include/app_book_mode.h`, `include/app_render.h`, and
`include/app_runtime.h`; `include/app.h` remains the umbrella compatibility header.

```c
typedef struct {
    // Rendering + terminal integration
    ChafaCanvas *canvas;
    ChafaCanvasConfig *canvas_config;
    ChafaTermInfo *term_info;

    // Media list + navigation
    GList *image_files;
    gchar *current_directory;
    gint current_index;
    gint total_images;

    // Mode/state buckets (see include/app.h for full details)
    FileManagerState file_manager;
    PreviewState preview;
    BookState book;
    InputState input;
    AsyncState async;
} PixelTermApp;
```

`InputState` uses three `ClickTracker` instances (`single_click`, `preview_click`, `file_manager_click`) to keep click timeout handling consistent across modes.

### Memory Management Strategy

#### Cache Implementation
- **LRU Cache**: Keep most recently used rendered images
- **Memory Pressure**: Automatic cleanup when memory is low
- **Thread Safety**: Mutex protection for shared cache



## Threading Architecture

### Main Thread Responsibilities
- User input handling
- UI updates and display
- File system operations

### Preload Thread Responsibilities
- Background image rendering
- Cache management
- Memory optimization

### Thread Communication
- **Mutex-protected queues** for task handoff
- **Condition variables** for pause/resume and work notification

## Rendering Pipeline

### Image Processing Flow
1. **Load Image**: Read file into memory
2. **Decode**: Convert to raw pixel data
3. **Render**: Apply Chafa transformation
4. **Cache**: Store result for future use
5. **Display**: Output to terminal

### Optimization Techniques
- **Predictive Caching**: Preload adjacent images
- **Selective Redraws**: Avoid re-rendering when selection doesn't change

## Terminal Integration

### Terminal Capability Detection
- Use Chafa's terminal database to select the best canvas and pixel mode.
- Query terminal size (cells and pixels) to derive cell geometry and aspect ratio.

### Adaptive Rendering
- **Fallback Modes**: Graceful degradation for limited terminals
- **Optimal Formats**: Choose best output format per terminal
- **Dynamic Sizing**: Adjust output based on terminal size

## File System Integration

### Supported Formats
- **Static**: PNG, JPEG, GIF, WebP, TIFF, BMP
- **Optional**: SVG (via librsvg), AVIF (via libavif)
- **Animated**: GIF, WebP animation

### Directory Scanning
- **Single-Threaded Scan**: Directory traversal happens on the main thread.
- **Filtering**: Image lists rely on `is_valid_image_file` (magic header checks).
- **Sorting**: File manager groups directories before files and sorts entries in AaBb order; image lists reuse the same AaBb ordering for consistency.

## Error Handling Strategy

### Error Categories
1. **System Errors**: File I/O, memory allocation
2. **Format Errors**: Unsupported or corrupted images
3. **Terminal Errors**: Display limitations
4. **User Errors**: Invalid inputs

### Recovery Mechanisms
- **Graceful Degradation**: Fallback to simpler modes
- **Error Reporting**: Clear, actionable error messages
- **State Preservation**: Maintain application stability

## Performance Monitoring

### Metrics Collection
- **Timing**: Frame rendering time, input latency
- **Memory**: Peak usage, allocation patterns
- **Cache**: Hit rates, eviction statistics
- **I/O**: File access times, throughput

### Optimization Targets
- **Startup Time**: < 500ms for typical directories
- **Image Switch**: < 100ms for cached images
- **Memory Usage**: < 50MB for normal usage
- **CPU Usage**: < 50% on modern hardware

## Security Considerations

### Input Validation
- **Path Traversal**: Prevent directory escape attacks
- **File Size**: Limit maximum file size
- **Format Validation**: Check file headers

### Resource Limits
- **Memory**: Prevent excessive allocation
- **File Handles**: Limit concurrent open files
- **Threads**: Cap background processing

## Cross-Platform Compatibility

### Platform Support
- **Linux**: Primary target, full feature support
- **macOS**: Secondary target, limited terminal support
- **Windows**: Experimental, requires WSL or similar

### Build System
- **Makefile**: Uses pkg-config for Chafa/GLib/GDK-Pixbuf/GIO and outputs `bin/pixelterm`.
- **Cross-Compile**: `ARCH=aarch64` switches pkg-config paths for ARM64 builds.

## Future Optimization Opportunities

### Advanced Rendering
- **GPU Acceleration**: OpenCL/CUDA for image processing
- **Neural Upscaling**: AI-based image enhancement
- **Custom Symbol Sets**: Optimized character mappings

### Advanced Features
- **Network Support**: Remote image browsing
- **Cloud Integration**: Direct cloud storage access
- **Plugin System**: Extensible architecture

## Implementation Notes

### Critical Paths
1. **Image Loading**: Must be fast and memory-efficient
2. **Rendering**: Direct Chafa integration is key
3. **Input Handling**: Low-latency keyboard processing
4. **Cache Management**: Intelligent preloading strategy

### Performance Targets
- **Startup**: < 200ms cold start
- **Navigation**: < 50ms image switch
- **Memory**: < 30MB steady state
- **CPU**: < 30% average usage

### Testing Strategy
- **Unit Tests**: Component isolation
- **Integration Tests**: End-to-end scenarios
- **Performance Tests**: Benchmarking suite
- **Compatibility Tests**: Multiple terminals/platforms

This architecture provides a solid foundation for achieving the performance goals while maintaining code quality and maintainability.
