# PixelTerm-C

🖼️ A high-performance terminal image browser written in C, based on the Chafa library.

## Overview

PixelTerm-C is a C implementation of the original PixelTerm application, designed to provide significantly better performance while maintaining all the same functionality. By leveraging the Chafa library directly instead of using subprocess calls, we eliminate the overhead of Python interpretation and external process creation.

## Performance Improvements

| Metric | Python Version | C Version | Improvement |
|--------|---------------|-----------|-------------|
| Startup Time | ~1-2s | ~0.2s | 5-10x faster |
| Image Switching | ~200-500ms | ~50-100ms | 3-5x faster |
| Memory Usage | ~50-100MB | ~20-30MB | 2-3x reduction |
| CPU Usage | High (Python + subprocess) | Medium (pure C) | 2-4x reduction |

## Features

- 🚀 **Fast image browsing** in terminal with minimal latency
- 🔄 **Preloading system** for smooth navigation between images
- 📁 **Multiple format support** (PNG, JPEG, GIF, WebP, TIFF, BMP, etc.)
- ⌨️ **Intuitive keyboard navigation** with customizable key bindings
- 🗂️ **File management** (delete, info display)
- 🎨 **Beautiful image rendering** with Chafa library optimization
- 🔄 **Circular navigation** - seamless browsing through image collections
- 📊 **Detailed image information** display with toggle functionality
- 🎯 **Memory-efficient caching** system for optimal performance

## Installation

### Dependencies

- GCC or Clang compiler
- Chafa library development files
- GLib 2.0 development files
- GDK-Pixbuf 2.0 development files
- Make

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install build-essential libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev pkg-config
```

### Fedora/CentOS/RHEL

```bash
sudo dnf install gcc gcc-c++ make chafa-devel glib2-devel gdk-pixbuf2-devel pkgconfig
```

### Arch Linux

```bash
sudo pacman -S base-devel chafa glib2 gdk-pixbuf2 pkgconf
```

### Building from Source

```bash
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make
```

### Installation

```bash
sudo make install
```

## Usage

### Basic Usage

```bash
# Browse images in a directory
./pixelterm-c /path/to/images/

# View a single image
./pixelterm-c /path/to/image.jpg

# Use with current directory
./pixelterm-c .
```

### Command Line Options

```bash
Usage: pixelterm-c [OPTIONS] [PATH]

Arguments:
  PATH    Path to an image file or directory containing images

Options:
  -h, --help     Show this help message
  -v, --version  Show version information
  -p, --preload  Enable image preloading (default: enabled)
  --no-preload   Disable image preloading
```

## Key Bindings

| Key | Action |
|-----|--------|
| `←/→` or `a/d` | Previous/Next image (with circular navigation) |
| `i` | Toggle detailed image information display |
| `r` | Delete current image |
| `q` or `ESC` | Quit application |
| `Ctrl+C` | Force exit |

## Image Information Display

Press `i` to toggle detailed image information:

```
============================================================
📸 Image Details
============================================================
📁 Filename: example.jpg
📂 Path: /home/user/Pictures
📄 Index: 1/25
💾 File size: 2.3 MB
📐 Dimensions: 1920 x 1080 pixels
🎨 Format: JPEG
🎭 Color mode: RGB
📏 Aspect ratio: 1.78
============================================================
```

## Architecture

### Core Components

- **Core Application**: Main application logic and state management
- **File Browser**: Directory traversal and image file management
- **Image Renderer**: Direct Chafa canvas integration with optimization
- **Input Handler**: Keyboard input processing and terminal management
- **Preloader System**: Multi-threaded image preloading for smooth navigation

### Performance Features

- **Direct Chafa Integration**: Eliminates subprocess overhead
- **Multi-threaded Preloading**: Background image preparation
- **Intelligent Caching**: LRU cache with automatic cleanup
- **Memory Management**: Optimized allocation and cleanup patterns
- **Terminal Adaptation**: Automatic capability detection and optimization

## Supported Formats

- **Static Images**: PNG, JPEG, GIF, WebP, TIFF, BMP, etc.
- **Animated Formats**: GIF, WebP animation (experimental)
- **Terminal Graphics**: Sixel, Kitty protocol support (when available)

## Examples

### Basic Image Browsing

```bash
# Browse all images in a directory
pixelterm-c ~/Pictures/

# View a single image
pixelterm-c wallpaper.jpg

# Browse with preloading enabled (default)
pixelterm-c --preload /path/to/photos/
```

### Advanced Usage

```bash
# Disable preloading for low-memory systems
pixelterm-c --no-preload /path/to/large/collection/

# Quick image preview
pixelterm-c screenshot.png
```

## Development

### Building for Debugging

```bash
make debug
```

### Running Tests

```bash
make test
```

### Checking Dependencies

```bash
make check-deps
```

### Clean Build

```bash
make clean
```

## Technical Details

### Memory Management

The application uses a sophisticated memory management strategy:

- **Smart Caching**: LRU cache with configurable size limits
- **Preloader Queue**: Priority-based task scheduling
- **Thread Safety**: Mutex protection for shared resources
- **Resource Cleanup**: Automatic cleanup on exit

### Terminal Compatibility

PixelTerm-C automatically detects and adapts to different terminal capabilities:

- **Color Support**: Truecolor, 256-color, and ANSI color modes
- **Graphics Protocols**: Sixel, Kitty protocol detection
- **Size Adaptation**: Dynamic geometry calculation
- **Symbol Optimization**: Terminal-specific symbol mapping

## Performance Tuning

### Environment Variables

```bash
# Adjust cache size
export PIXELTERM_CACHE_SIZE=100

# Enable/disable preloading
export PIXELTERM_PRELOAD=true

# Set debug mode
export PIXELTERM_DEBUG=1
```

### Performance Tips

1. **Use SSD storage** for faster image loading
2. **Enable preloading** for better navigation experience
3. **Limit directory size** for optimal performance
4. **Use appropriate image formats** (PNG for quality, JPEG for size)

## Troubleshooting

### Common Issues

**Problem**: Images not displaying correctly
- **Solution**: Check terminal color support and try different image formats

**Problem**: Slow image loading
- **Solution**: Enable preloading or reduce directory size

**Problem**: Memory usage too high
- **Solution**: Reduce cache size or disable preloading

### Debug Mode

```bash
# Run with debug output
PIXELTERM_DEBUG=1 ./pixelterm-c /path/to/images/
```

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Guidelines

- Follow C11 standards
- Use consistent coding style
- Add appropriate error handling
- Include memory management
- Write tests for new features

## License

This project follows the same licensing as the original PixelTerm project.

## Acknowledgments

- **Chafa Library**: For excellent terminal graphics rendering
- **Original PixelTerm**: For the inspiration and feature design
- **GLib and GDK-Pixbuf**: For robust image handling and utilities

## Roadmap

- [ ] Advanced image filters and adjustments
- [ ] Plugin system for custom extensions
- [ ] Network image browsing support
- [ ] Configuration file support
- [ ] Batch processing capabilities
- [ ] GUI mode integration

---

**PixelTerm-C** - Fast, efficient, and beautiful terminal image browsing.