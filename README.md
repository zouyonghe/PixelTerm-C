# PixelTerm-C - High Performance Terminal Image Viewer

*English | [中文](README_zh.md)*

🖼️ A high-performance terminal image browser written in C, based on the [Chafa](https://github.com/hpjansson/chafa) library.

## Overview

PixelTerm-C is a C implementation of the original PixelTerm application, designed to provide significantly better performance than the Python version while maintaining all the same functionality. By leveraging the Chafa library directly instead of using subprocess calls, we eliminate the overhead of Python interpretation and external process creation.

Release notes: see [CHANGELOG.md](CHANGELOG.md).

## 🌟 Features

- 🖼️ **Multi-format Support** - Supports JPG, PNG, GIF, BMP, WebP, TIFF and other mainstream image formats
- 🎬 **Animated GIF Support** - Play animated GIFs directly in the terminal with proper timing and high-quality rendering
- 🎨 **TrueColor Rendering** - Full 24-bit color support with automatic detection and optimization
- 📁 **Smart Browsing** - Automatically detects image files in directories with directory navigation support
- ⌨️ **Keyboard Navigation** - Switch between images with arrow keys, supporting various terminal environments
- 📏 **Adaptive Display** - Automatically adapts to terminal size changes
- 🎨️ **Minimal Interface** - No redundant information, focused on image browsing experience
- ⚡️ **High Performance** - 5-10x faster than Python version with significantly lower memory usage
- 🔄 **Circular Navigation** - Seamless browsing with wrap-around between first and last images
- 🏗️ **Multi-architecture Support** - Native support for both amd64 and aarch64 (ARM64) architectures
- 🖱️ **Mouse Support** - Intuitive mouse navigation, selection, and scrolling across all modes
- 📦 **Preloading** - Image preloading for faster navigation (enabled by default).
- 🎨 **Dithering** - Improves visual quality in color-limited terminals (disabled by default).

## Performance Improvements

| Metric | Python Version | C Version | Improvement |
|--------|---------------|-----------|-------------|
| Startup Time | ~1-2s | ~0.1-0.3s | Several times faster |
| Image Switching | ~200-500ms | ~50-150ms | 2-5x faster |
| Memory Usage | ~50-100MB | ~15-35MB | 2-3x reduction |
| CPU Usage | High (Python + subprocess) | Medium (pure C) | Noticeable reduction |

## 🚀 Quick Start

### Install Dependencies

```bash
# Ubuntu/Debian  
sudo apt-get install libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev pkg-config build-essential

# Arch Linux
sudo pacman -S chafa glib2 gdk-pixbuf2 pkgconf base-devel
```

### Quick Install

```bash
# Install from package manager (recommended)
# Arch Linux: pacman -S pixelterm-c

# Or download binary for your architecture and platform
# Linux AMD64:
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-linux
chmod +x pixelterm-amd64-linux && sudo mv pixelterm-amd64-linux /usr/local/bin/pixelterm

# Linux ARM64:
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-linux
chmod +x pixelterm-arm64-linux && sudo mv pixelterm-arm64-linux /usr/local/bin/pixelterm

# macOS AMD64:
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-macos
chmod +x pixelterm-amd64-macos && sudo mv pixelterm-amd64-macos /usr/local/bin/pixelterm

# macOS ARM64 (Apple Silicon):
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-macos
chmod +x pixelterm-arm64-macos && sudo mv pixelterm-arm64-macos /usr/local/bin/pixelterm

# Note for macOS users: If the binary fails to start due to security restrictions, run:
# xattr -dr com.apple.quarantine pixelterm-arm64-macos
```

### Building from Source

```bash
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make

# For cross-compilation to aarch64
make CC=aarch64-linux-gnu-gcc ARCH=aarch64
# Note: Cross-compilation is experimental and requires corresponding architecture-specific dependency libraries to be correctly installed on the host system.
```

### Usage

```bash
# View single image (opens image viewer directly)
./pixelterm /path/to/image.jpg

# Browse directory (launches file manager mode)
./pixelterm /path/to/directory

# Run in current directory (launches file manager mode)
./pixelterm

# Show version
./pixelterm --version

# Show help
./pixelterm --help

# Disable preloading
./pixelterm --no-preload /path/to/images

# Disable alternate screen buffer
./pixelterm --no-alt-screen /path/to/images

# Improve UI appearance on some terminals (may reduce performance)
./pixelterm --clear-workaround /path/to/images

# Enable dithering
./pixelterm -D /path/to/image.jpg
# Or
./pixelterm --dither /path/to/image.jpg

```

## 🎮 Controls



### Global Controls

| Key | Function |
|-----|----------|
| ESC | Exit application |
| Ctrl+C | Force exit |

### Mouse Controls

Mouse interaction significantly enhances navigation and selection across different modes.

| Control | Function | Applicable Modes | Notes |
|---------|----------|------------------|-------|
| Left Click | Advance to next image | Image View (Single Image Mode) | |
| Double Left Click | Switch to Grid Preview | Image View (Single Image Mode) | |
| Left Click | Select image | Grid Preview | Selects the image under the cursor. |
| Double Left Click | Open selected image in Image View | Grid Preview | Opens the image at the cursor position. |
| Left Click | Select entry | File Manager Mode | Selects the file or directory under the cursor. |
| Double Left Click | Open selected entry (directory/file) | File Manager Mode | Navigates into a directory or opens an image. |
| Mouse Scroll Up/Down | Previous/Next image | Image View (Single Image Mode) | Smoothly navigate through images. |
| Mouse Scroll Up/Down | Page Up/Down | Grid Preview | Scroll through pages of images. |
| Mouse Scroll Up/Down | Navigate entries up/down | File Manager Mode | Scroll through the list of files and directories. |

### Image View (Single Image Mode)

This is the default mode when viewing a single image.

| Key | Function |
|-----|----------|
| ←/↑ | Previous image |
| →/↓ | Next image |
| h/k | Vim-style navigation (previous image) |
| l/j | Vim-style navigation (next image) |
| Enter | Toggle into Grid Preview mode |
| TAB | Toggle into File Manager mode; returns to this view on subsequent TAB |
| i | Toggle image information display |
| `~` / `` ` `` | Toggle Zen mode (hide/show all UI text) |
| r | Delete current image |
| D | Toggle dithering on/off |

### Grid Preview (Thumbnail Mode)

This mode displays multiple image thumbnails in a grid.

| Key | Function |
|-----|----------|
| ←/→ | Move selection left/right |
| ↑/↓ | Move selection up/down |
| h/j/k/l | Vim-style navigation (left/down/up/right) |
| PgUp/PgDn | Page up/down through the grid |
| Enter | Open selected image in Image View |
| TAB | Toggle into File Manager mode; returns to this view on subsequent TAB |
| `~` / `` ` `` | Toggle Zen mode (hide/show all UI text) |
| r | Delete selected image |
| D | Toggle dithering on/off |

### File Manager Mode

This mode allows browsing through directories and files. Note that Vim-style navigation (h/j/k/l) is not supported here, as letter keys are reserved for quickly jumping to file entries.

| Key | Function |
|-----|----------|
| ←/→ | Go to parent directory / Open selected directory/file |
| ↑/↓ | Navigate entries up/down |
| Enter | Open selected directory or file |
| Any Letter (a-z/A-Z) | Jump to next entry starting with that letter |


## 📄 License

LGPL-3.0 or later - See LICENSE file for details

This project is licensed under the same license as [Chafa](https://github.com/hpjansson/chafa) (LGPLv3+).

---

**PixelTerm-C** - Making terminals excellent image viewers with lightning speed! 🖼️
