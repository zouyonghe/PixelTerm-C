# Troubleshooting

*English | [中文](TROUBLESHOOTING_zh.md) | [日本語](TROUBLESHOOTING_ja.md)*

## No image or video appears

- Try an explicit protocol override:

```bash
pixelterm --protocol kitty /path/to/media
pixelterm --protocol iterm2 /path/to/media
pixelterm --protocol sixel /path/to/media
pixelterm --protocol text /path/to/media
```

- `auto` mode currently probes in this order: `sixel`, `iterm2`, then `kitty`.
- If a video is already open, press `p` or `P` to switch video output modes in this order: `text -> sixel -> iterm2 -> kitty -> text`.
- If you are in a remote shell or SSH session, protocol probing may behave differently from a local terminal.
- See [TERMINAL_PROTOCOL_SUPPORT.md](TERMINAL_PROTOCOL_SUPPORT.md) for terminal-specific notes.

## Output looks wrong in Warp or another terminal

- Try `--alt-screen false` first.
- If that is not enough, try `--clear-workaround`.
- If one setting consistently works better, move it into `config.ini` under a terminal-specific section.
- `config.example.ini` includes examples for `WezTerm` and `WarpTerminal`.

## The downloaded macOS binary will not start

macOS may attach quarantine metadata to downloaded binaries. Remove it and try again:

```bash
xattr -dr com.apple.quarantine /usr/local/bin/pixelterm
```

## Books do not open

- PDF, EPUB, and CBZ support only work in builds that include MuPDF support.
- When building from source, install MuPDF before running `make`.
- If book support is missing in your current build, images and videos can still work normally.

## A config file seems to be ignored

- Default config path: `$XDG_CONFIG_HOME/pixelterm/config.ini`
- Custom config path: `pixelterm --config /path/to/config.ini ...`
- A missing default config file is ignored, but a missing file passed with `--config` is treated as an error.
- Config loading order is:
  - `[default]`
  - first matching terminal-specific group from `TERM_PROGRAM`, `LC_TERMINAL`, `TERMINAL_NAME`, or `TERM`
- CLI flags override config values because argument parsing happens after config loading.
- For CLI boolean flags such as `--preload` and `--alt-screen`, accepted values include `true/false`, `yes/no`, `on/off`, and `1/0`.

## There is no audio during video playback

Video playback is video-only. Audio output is not part of the current feature set.

## The path behavior is not what I expected

- If the path does not exist or is inaccessible, PixelTerm-C exits with an error.
- If you pass a directory, PixelTerm-C loads that directory and starts in file manager mode.
- If you pass a regular file that is not a supported media file, PixelTerm-C falls back to the file's parent directory and opens file manager mode there.
- If you run `pixelterm` with no path, it starts in file manager mode for the current directory.
