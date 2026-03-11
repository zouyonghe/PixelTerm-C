# PixelTerm-C

![Version](https://img.shields.io/badge/Version-v1.7.8-blue)
![License](https://img.shields.io/badge/License-LGPL--3.0-orange)

*English | [中文](README_zh.md) | [日本語](README_ja.md)*

PixelTerm-C is a terminal-native browser for images, video, and books. It helps you open local media, move through folders, and stay in a keyboard-and-mouse workflow without leaving the terminal.

## Why PixelTerm-C

- Browse images, animated GIFs, videos, and books in one tool.
- Switch between single-view, grid preview, book reader, and file manager modes.
- Use keyboard or mouse navigation across image, video, and book workflows.
- Adjust rendering with preload, dithering, gamma, and config-based terminal overrides.
- Build from source on Linux and macOS, or install a release binary for amd64 and arm64.

## Screenshot

This is a real PixelTerm-C session:

<img src="screenshots/2.png" alt="PixelTerm-C screenshot">

Screenshot from a real PixelTerm-C session.

## Install

Choose the install path that fits your setup:

```bash
# Arch Linux (AUR)
paru -S pixelterm-c
# or
yay -S pixelterm-c

# Linux amd64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-linux
chmod +x pixelterm-amd64-linux && sudo mv pixelterm-amd64-linux /usr/local/bin/pixelterm

# Linux arm64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-linux
chmod +x pixelterm-arm64-linux && sudo mv pixelterm-arm64-linux /usr/local/bin/pixelterm

# macOS amd64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-macos
chmod +x pixelterm-amd64-macos && sudo mv pixelterm-amd64-macos /usr/local/bin/pixelterm

# macOS arm64 (Apple Silicon)
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-macos
chmod +x pixelterm-arm64-macos && sudo mv pixelterm-arm64-macos /usr/local/bin/pixelterm

# If macOS blocks the installed binary, remove quarantine and try again.
# xattr -dr com.apple.quarantine /usr/local/bin/pixelterm
```

## Quick Start

Use `pixelterm` with a file or directory path:

```bash
# Open a single image
pixelterm /path/to/image.jpg

# Play a video (video only; no audio)
pixelterm /path/to/video.mp4

# Read a book
pixelterm /path/to/book.pdf

# Browse a folder
pixelterm /path/to/directory

# Show CLI help
pixelterm --help
```

For more usage examples and options, see [USAGE.md](USAGE.md).

## Formats and Compatibility

- Images: JPG, PNG, GIF, BMP, WebP, TIFF, and other common image formats.
- Video: MP4, MKV, AVI, MOV, WebM, MPEG/MPG, and M4V (video only; no audio).
- Books: PDF, EPUB, and CBZ when built with MuPDF support.
- PixelTerm-C can auto-detect an output protocol, and you can override it with `--protocol` when needed.
- Terminal and protocol notes: [docs/TERMINAL_PROTOCOL_SUPPORT.md](docs/TERMINAL_PROTOCOL_SUPPORT.md).

## Configuration

PixelTerm-C reads `$XDG_CONFIG_HOME/pixelterm/config.ini` when it is present, and you can pass a custom file with `--config`. Start from [`config.example.ini`](config.example.ini), use `[default]` for shared settings, then add terminal-specific sections that match `TERM_PROGRAM`, `LC_TERMINAL`, `TERMINAL_NAME`, or `TERM`.

Quick setup:

```bash
mkdir -p ~/.config/pixelterm
cp config.example.ini ~/.config/pixelterm/config.ini
```

## Documentation

- Usage and CLI options: [USAGE.md](USAGE.md)
- Keyboard and mouse controls: [CONTROLS.md](CONTROLS.md)
- Release history: [CHANGELOG.md](CHANGELOG.md)
- Terminal and protocol notes: [docs/TERMINAL_PROTOCOL_SUPPORT.md](docs/TERMINAL_PROTOCOL_SUPPORT.md)

## Build from Source

Install the build dependencies first:

```bash
# Ubuntu/Debian
sudo apt-get install libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev libavformat-dev libavcodec-dev libswscale-dev libavutil-dev pkg-config build-essential
# Optional: book support
sudo apt-get install libmupdf-dev

# Arch Linux
sudo pacman -S chafa glib2 gdk-pixbuf2 ffmpeg pkgconf base-devel
# Optional: book support
sudo pacman -S mupdf

# macOS (Homebrew)
brew install chafa glib gdk-pixbuf ffmpeg pkg-config
# Optional: book support
brew install mupdf
```

Then build:

```bash
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make

# Binary output
bin/pixelterm

# Optional system install
sudo make install

# Cross-compilation to aarch64
make CC=aarch64-linux-gnu-gcc ARCH=aarch64
```

If MuPDF is available, book support is built in automatically. Cross-compilation is experimental and depends on matching target libraries on the host system.

## License

LGPL-3.0-or-later. See [`LICENSE`](LICENSE) for details.

PixelTerm-C uses the same license family as [Chafa](https://github.com/hpjansson/chafa) (LGPLv3+).
