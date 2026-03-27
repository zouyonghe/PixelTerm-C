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

- `auto` first checks the matched terminal hint and only tries that hint's plausible protocols in `sixel -> iterm2 -> kitty` priority.
- On a local session, if no hinted probe confirms a protocol, `auto` runs the same `sixel -> iterm2 -> kitty` order as a generic probe pass. If nothing confirms, it falls back to text.
- In a direct SSH session (`SSH_CONNECTION`, `SSH_CLIENT`, or `SSH_TTY` set, without `TMUX` or `STY`), `auto` stays conservative: it skips the generic probe pass and falls back to text unless the hinted family returns an affirmative signal.
- If you already know the correct protocol, or if a remote/passthrough setup keeps `auto` in text, use an explicit `--protocol` value or a terminal-specific `config.ini` override.
- If a video is already open, press `p` or `P` to switch video output modes in this order: `text -> sixel -> iterm2 -> kitty -> text`.
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
- A directory path is not a valid book file. Directories open in file manager mode, and attempts to open a directory as a book are rejected like a missing or invalid book path.

## A config file seems to be ignored

- Default config path: `$XDG_CONFIG_HOME/pixelterm/config.ini`, or `$HOME/.config/pixelterm/config.ini` when `XDG_CONFIG_HOME` is unset or empty.
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
- If the path itself starts with `-`, use `--` to stop option parsing first, for example: `pixelterm -- --config=gallery.txt`
- If you pass a directory, PixelTerm-C loads that directory and starts in file manager mode.
- If the startup path is not a directory, not a valid book file, and not a valid media file, PixelTerm-C falls back to the file's canonical parent directory and opens file manager mode there.
- If you run `pixelterm` with no path, it starts in file manager mode for the current directory.

## File manager or preview grid selection seems off

- If the wrong item stays selected after toggling hidden files or changing preview-grid zoom, quit and reopen PixelTerm-C in the same directory to reset the view.
- If it happens again after reopening, note the directory, whether hidden files were shown, and the last few keys you pressed before reporting it.
