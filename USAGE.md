# Usage

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

# Disable preloading
pixelterm --no-preload /path/to/images

# Disable alternate screen buffer
pixelterm --no-alt-screen /path/to/images
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
# Note: defaults to 0.5 in kitty, 1.0 otherwise
pixelterm --gamma 0.8 /path/to/image.jpg
```
