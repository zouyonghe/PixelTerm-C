# PixelTerm-C - High Performance Terminal Image Viewer

*English | [中文](README_zh.md)*

🖼️ A high-performance terminal image browser written in C, based on the Chafa library.

## Overview

PixelTerm-C is a C implementation of the original PixelTerm application, designed to provide significantly better performance than the Python version while maintaining all the same functionality. By leveraging the Chafa library directly instead of using subprocess calls, we eliminate the overhead of Python interpretation and external process creation.

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

### Quick Install (Linux amd64)

```bash
# Download and install the latest binary
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm
chmod +x pixelterm
sudo mv pixelterm /usr/local/bin/

# Or just download to current directory
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm
chmod +x pixelterm
./pixelterm /path/to/images

# Or download the tarball
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm.tar.gz
tar -xzf pixelterm.tar.gz
./pixelterm /path/to/images
```

### Building from Source

```bash
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make
```

### Usage

```bash
# Browse images
./pixelterm /path/to/images
```

## 🎮 Controls

| Key | Function |
|-----|----------|
| ←/→ | Previous/Next image |
| a/d | Alternative left/right keys |
| i | Toggle image information |
| r | Delete current image |
| q or ESC | Exit program |
| Ctrl+C | Force exit |

## 📄 License

LGPL-3.0 or later - See LICENSE file for details

This project is licensed under the same license as Chafa (LGPLv3+).

---

**PixelTerm-C** - Making terminals excellent image viewers with lightning speed! 🖼️