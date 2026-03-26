# Usage

*English | [中文](USAGE_zh.md) | [日本語](USAGE_ja.md)*

```bash
# View single image (opens image viewer directly)
pixelterm /path/to/image.jpg

# Play a video (video-only; no audio)
pixelterm /path/to/video.mp4

# Read a book (PDF/EPUB/CBZ)
pixelterm /path/to/book.pdf

# Browse directory (launches file manager mode)
pixelterm /path/to/directory

# Run in current directory (launches file manager mode)
pixelterm

# Show version
pixelterm --version

# Show help
pixelterm --help

# Control preloading
pixelterm --preload false /path/to/images

# Control alternate screen buffer
pixelterm --alt-screen false /path/to/images
# Note: Mainly for Warp terminal; usually unnecessary.

# Improve UI appearance on some terminals (may reduce performance)
pixelterm --clear-workaround /path/to/images
# Note: Mainly for Warp terminal; usually unnecessary.

# Enable dithering
pixelterm -D /path/to/image.jpg
# Or
pixelterm --dither /path/to/image.jpg

# Adjust rendering work factor (1-9, higher is slower but higher quality)
pixelterm --work-factor 7 /path/to/image.jpg

# Force output protocol (auto, text, sixel, kitty, iterm2)
pixelterm --protocol kitty /path/to/image.jpg

# Gamma correction for image rendering
# Note: defaults to 1.0
pixelterm --gamma 0.8 /path/to/image.jpg

# Load configuration file (default: $XDG_CONFIG_HOME/pixelterm/config.ini)
pixelterm --config ~/.config/pixelterm/config.ini /path/to/image.jpg

# Treat a config-like filename, or any path starting with '-', as a path
pixelterm -- --config=gallery.txt

# Config file format: [default] for baseline settings, optional terminal-named
# sections matching TERM_PROGRAM/LC_TERMINAL/TERMINAL_NAME/TERM for overrides.
# See config.example.ini
```

## Notes

- `pixelterm` with no `PATH` starts in file manager mode for the current directory.
- `--` stops option parsing, so anything after it is treated as `PATH`.
- CLI flags override config file values because config loading happens before argument parsing.
- `--preload` and `--alt-screen` accept `true/false`, `yes/no`, `on/off`, and `1/0`.
- A missing default config file is ignored, but a missing file passed with `--config` is treated as an error.
- Config groups are applied in this order: `[default]`, then the first matching terminal-specific group from `TERM_PROGRAM`, `LC_TERMINAL`, `TERMINAL_NAME`, or `TERM`.
- If `PATH` is an unsupported regular file, PixelTerm-C falls back to that file's canonical parent directory and opens file manager mode there.
- A directory path is never treated as a valid book file; directories open in file manager mode instead.
- If rendering looks wrong, try an explicit `--protocol` value or see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
