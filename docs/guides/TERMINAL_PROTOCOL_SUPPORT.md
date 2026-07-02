# Terminal Protocol Support

*English | [中文](TERMINAL_PROTOCOL_SUPPORT_zh.md) | [日本語](TERMINAL_PROTOCOL_SUPPORT_ja.md)*

This page summarizes the terminal and graphics-protocol support notes currently documented for PixelTerm-C. It is intended as a practical user reference for `auto` detection and manual overrides, not a ranking of terminal quality.

## How to read this page

- `Documented` means you have a clear starting point here: try the listed protocol or example first.
- `Partially documented` means PixelTerm-C has a likely protocol hint, but if `auto` falls back to text you may still need to test `--protocol` yourself.
- `Recognized` means PixelTerm-C can identify the terminal family, but you should not assume graphics output there unless you have already verified a manual override locally.
- Any explicit non-`auto` `--protocol` value, or a non-`auto` `protocol = ...` entry in `config.ini`, bypasses `auto` and uses that protocol directly.
- In `auto`, PixelTerm-C first checks the matched terminal hint from `TERM`, `TERM_PROGRAM`, or terminal-specific env vars, and only tries plausible hinted protocols in `sixel` -> `iterm2` -> `kitty` priority.
- If a hinted probe returns an affirmative result, `auto` uses it. Outside direct SSH, otherwise `auto` continues with a generic probe pass in the same `sixel` -> `iterm2` -> `kitty` order.
- If no local probe confirms a graphics protocol, `auto` falls back to text.
- In a direct SSH session (`SSH_CONNECTION`, `SSH_CLIENT`, or `SSH_TTY` set, without `TMUX` or `STY`), `auto` does not continue to the generic probe pass. Without an affirmative hinted signal it falls back to text.
- Use `--protocol` or a terminal-specific `config.ini` override when you already know the right protocol, or when remote/passthrough setup keeps `auto` in text.

## Terminals with protocol notes

| Terminal | Protocol notes | Status | Notes |
|----------|----------------|--------|-------|
| WezTerm | kitty, sixel, optional iTerm2 override | Documented | `config.example.ini` includes a `[WezTerm] protocol = iterm2` example. |
| kitty | kitty | Documented | You can force it with `--protocol kitty`. Native kitty may show little difference between direct and shared-memory transfer at modest terminal sizes. |
| iTerm2 | iTerm2, sixel | Documented | You can force it with `--protocol iterm2`. |
| Ghostty | kitty | Partially documented | If `auto` stays in text, try `--protocol kitty`. |
| Rio | sixel | Partially documented | If `auto` stays in text, try `--protocol sixel`. |
| Warp | kitty | Partially documented | `config.example.ini` includes a `[WarpTerminal]` example with extra compatibility settings. Prefer `kitty_transfer = direct` if shared-memory video makes the Warp UI sluggish. |
| Contour | sixel | Partially documented | If `auto` stays in text, try `--protocol sixel`. |
| Konsole | kitty | Partially documented | If `auto` stays in text, try `--protocol kitty`. |
| EAT | sixel | Partially documented | If `auto` stays in text, try `--protocol sixel`. |
| foot | sixel | Partially documented | If `auto` stays in text, try `--protocol sixel`. |
| mintty | iTerm2, sixel | Partially documented | If `auto` stays in text, test `--protocol iterm2` and `--protocol sixel`. |
| mlterm | iTerm2, sixel | Partially documented | If `auto` stays in text, test `--protocol iterm2` and `--protocol sixel`. |
| yaft | sixel | Partially documented | If `auto` stays in text, try `--protocol sixel`. |

## Recognized terminals without a documented graphics recommendation

| Terminal family | Status | Notes |
|-----------------|--------|-------|
| Alacritty | Recognized | No graphics recommendation is documented here; expect text unless you have verified an override locally. |
| Apple Terminal | Recognized | No graphics recommendation is documented here; expect text unless you have verified an override locally. |
| ctx | Recognized | No graphics recommendation is documented here; expect text unless you have verified an override locally. |
| fbterm | Recognized | No graphics recommendation is documented here; expect text unless you have verified an override locally. |
| hurd / linux console / vt220 | Recognized | Treat these as text-only unless you have confirmed something different in your own environment. |
| rxvt / st / xterm | Recognized | No graphics recommendation is documented here; expect text unless you have verified an override locally. |
| VTE / Windows console | Recognized | No graphics recommendation is documented here; expect text unless you have verified an override locally. |

## Manual override paths

```bash
# Force a protocol for the current run
pixelterm --protocol kitty /path/to/image.jpg

# Keep text mode but bias it toward quadrant detail
pixelterm --protocol text --text-symbols quarter /path/to/image.jpg

# Available values
pixelterm --protocol auto|text|sixel|kitty|iterm2 /path/to/image.jpg

# Compare kitty video transfer paths
pixelterm --protocol kitty --kitty-transfer direct /path/to/video.mp4
pixelterm --protocol kitty --kitty-transfer shm /path/to/video.mp4
```

You can also set `protocol = auto|text|sixel|kitty|iterm2` in `config.ini`. For kitty video rendering, `kitty_transfer = auto|direct|shm` controls whether PixelTerm-C uses the regular inline kitty path or the kitty shared-memory fast path. For text rendering, `text_symbols = auto|half|quarter` controls whether PixelTerm-C keeps the terminal-safe default symbol set or switches to stronger half-block-heavy / quad-heavy manual symbol sets. Terminal-specific sections follow the first matching value from `TERM_PROGRAM`, `LC_TERMINAL`, `TERMINAL_NAME`, or `TERM`. See [config.example.ini](../../config.example.ini) and [USAGE.md](USAGE.md) for the current CLI and config syntax.

## Kitty transfer modes

- `auto`: default. PixelTerm-C uses conservative shared-memory detection for kitty video and otherwise keeps the direct inline path.
- `direct`: always use Chafa's inline kitty output. This is useful for comparing behavior or avoiding terminal-specific shared-memory issues.
- `shm`: force the kitty shared-memory path for video frames. If shared-memory setup fails for a frame, PixelTerm-C falls back to direct rendering.
- `PIXELTERM_KITTY_SHM=1` remains available as a debug override for `auto`, but `config.ini` or `--kitty-transfer` is preferred for normal use.
- If a kitty-compatible terminal becomes sluggish outside PixelTerm-C itself, for example mouse cursor changes to a loading state or tabs become hard to switch, use `kitty_transfer = direct`. That usually means the terminal's own shared-memory graphics consumer is overloaded.

## Scope notes

- Use this page as a practical guide, not as a complete certification matrix.
- Rendering behavior can still vary by terminal version, local settings, remote session setup, and whether protocol probing succeeds at runtime.
- Direct SSH fallback currently stays conservative on purpose. If a remote, tmux, or screen setup hides the affirmative probe response you expect, prefer an explicit override.
- If your terminal works better with an explicit protocol than with `auto`, prefer a local config override for that terminal.
- Shared-memory kitty transfer is local-only by design. Avoid forcing it through SSH, tmux, or screen unless you are deliberately testing a setup that supports it.
