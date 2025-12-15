# PixelTerm-C - High Performance Terminal Image Viewer

*English | [中文](README_zh.md)*

🖼️ A high-performance terminal image browser written in C, based on the Chafa library.

## Overview

PixelTerm-C is a C implementation of the original PixelTerm application, designed to provide significantly better performance than the Python version while maintaining all the same functionality. By leveraging the Chafa library directly instead of using subprocess calls, we eliminate the overhead of Python interpretation and external process creation.

Release notes: see `CHANGELOG.md`.

## 🌟 Features

- 🖼️ **Multi-format Support** - Supports JPG, PNG, GIF, BMP, WebP, TIFF and other mainstream image formats
- 📁 **Smart Browsing** - Automatically detects image files in directories with directory navigation support
- ⌨️ **Keyboard Navigation** - Switch between images with arrow keys, supporting various terminal environments
- 📏 **Adaptive Display** - Automatically adapts to terminal size changes
- 🎨️ **Minimal Interface** - No redundant information, focused on image browsing experience
- ⚡️ **High Performance** - 5-10x faster than Python version with significantly lower memory usage
- 🔄 **Circular Navigation** - Seamless browsing with wrap-around between first and last images
- 📊 **Detailed Information** - Toggle comprehensive image metadata display
- 🎯 **Blue Filenames** - Color-coded filename display for better visibility
- 🏗️ **Multi-architecture Support** - Native support for both amd64 and aarch64 (ARM64) architectures
- 📦 **Preloading** - Optional image preloading for faster navigation
- 📋 **Smart Help** - Automatically shows version and help information when no images are found

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
```

### Usage

```bash
# Browse images in directory (launches directly into preview grid if images exist)
./pixelterm /path/to/images

# View single image (opens image viewer)
./pixelterm /path/to/image.jpg

# Run in current directory
./pixelterm

# Show version
./pixelterm --version

# Show help
./pixelterm --help

# Start with image information visible
./pixelterm --info /path/to/images

# Disable preloading
./pixelterm --no-preload /path/to/images

# Start directly in preview grid (default when opening a directory with images)
./pixelterm /path/to/images   # then use Enter or p to leave/return to grid
```

Preview grid basics:
- When opening a directory with images, the app starts in the preview grid by default; from single-image view press `Enter` or `p` to enter the grid.
- Use arrows/PgUp/PgDn to move, `+`/`-` to change thumbnail size (at least 2 columns), and `Enter` to open the selected image.

## 🎮 Controls

| Key | Function |
|-----|----------|
| ←/→ | Previous/Next image |
| a/d | Alternative left/right keys |
| ↑/↓ | Move selection (preview/file manager) |
| PgUp/PgDn | Page up/down in preview grid |
| p/Enter | Enter preview grid mode (move with arrows/PgUp/PgDn, Enter to open selected) |
| +/- | Increase/decrease preview thumbnail size |
| TAB | Toggle file manager (or exit when no images are loaded) |
| i | Toggle image information |
| r | Delete current image |
| q | Return to previous view (image view exits app) |
| ESC | Exit program (always) |
| Ctrl+C | Force exit |

File Manager:
- ↑/↓ to navigate, Enter/→ to open, ← to go to parent.
- Any letter key (a–z/A–Z) jumps to the next entry starting with that letter.
- q returns to previous view (image view exits app); TAB toggles file manager; ESC quits the program.

## 📄 License

LGPL-3.0 or later - See LICENSE file for details

This project is licensed under the same license as Chafa (LGPLv3+).

---

**PixelTerm-C** - Making terminals excellent image viewers with lightning speed! 🖼️
