# Terminal Protocol Support

*English | [中文](TERMINAL_PROTOCOL_SUPPORT_zh.md) | [日本語](TERMINAL_PROTOCOL_SUPPORT_ja.md)*

This page summarizes the terminal and graphics-protocol support notes currently documented for PixelTerm-C. It is intended as a practical user reference for `auto` detection and manual overrides, not a ranking of terminal quality.

## How to read this page

- `Documented` means the repository includes direct support notes or examples intended to guide users.
- `Partially documented` means PixelTerm-C has protocol hints or terminal-specific notes, but behavior may still depend on your local setup.
- `Recognized` means the terminal family is known to the project, but this page does not document a stronger protocol guarantee.
- In `auto` mode, PixelTerm-C probes protocols in this order: Sixel, iTerm2, then kitty. If that does not match your setup, use `--protocol` or a terminal-specific `config.ini` override.

## Terminals with protocol notes

| Terminal | Protocol notes | Status | Notes |
|----------|----------------|--------|-------|
| WezTerm | kitty, sixel, optional iTerm2 override | Documented | `config.example.ini` includes a `[WezTerm] protocol = iterm2` example. |
| kitty | kitty | Documented | You can force it with `--protocol kitty`. |
| iTerm2 | iTerm2, sixel | Documented | You can force it with `--protocol iterm2`. |
| Ghostty | kitty | Partially documented | PixelTerm-C includes a kitty-protocol hint for Ghostty environments. |
| Rio | sixel | Partially documented | Current docs point to Sixel-based detection. |
| Warp | kitty | Partially documented | `config.example.ini` includes a `[WarpTerminal]` example with extra compatibility settings. |
| Contour | sixel | Partially documented | Current docs point to Sixel-based detection. |
| Konsole | kitty | Partially documented | The codebase also contains Konsole-specific rendering adjustments. |
| EAT | sixel | Partially documented | Current docs point to Sixel-based detection. |
| foot | sixel | Partially documented | Current docs point to Sixel-based detection. |
| mintty | iTerm2, sixel | Partially documented | Current docs point to protocol hints rather than a broader guarantee. |
| mlterm | iTerm2, sixel | Partially documented | Current docs point to protocol hints rather than a broader guarantee. |
| yaft | sixel | Partially documented | Current docs point to Sixel-based detection. |

## Recognized terminals without stronger protocol notes

| Terminal family | Status | Notes |
|-----------------|--------|-------|
| Alacritty | Recognized | The project recognizes the terminal name, but this page does not document a stronger kitty, iTerm2, or Sixel expectation. |
| Apple Terminal | Recognized | No stronger protocol note is documented here. |
| ctx | Recognized | No stronger protocol note is documented here. |
| fbterm | Recognized | No stronger protocol note is documented here. |
| hurd / linux console / vt220 | Recognized | These entries are recognized by the project, not documented here as graphics-capable terminals. |
| rxvt / st / xterm | Recognized | No stronger protocol note is documented here. |
| VTE / Windows console | Recognized | No stronger protocol note is documented here. |

## Manual override paths

```bash
# Force a protocol for the current run
pixelterm --protocol kitty /path/to/image.jpg

# Available values
pixelterm --protocol auto|text|sixel|kitty|iterm2 /path/to/image.jpg
```

You can also set `protocol = auto|text|sixel|kitty|iterm2` in `config.ini`. Terminal-specific sections follow the first matching value from `TERM_PROGRAM`, `LC_TERMINAL`, `TERMINAL_NAME`, or `TERM`. See [config.example.ini](../../config.example.ini) and [USAGE.md](USAGE.md) for the current CLI and config syntax.

## Scope notes

- This page reflects the support notes currently documented by the project; it is not a complete certification matrix.
- Rendering behavior can still vary by terminal version, local settings, remote session setup, and whether protocol probing succeeds at runtime.
- If your terminal works better with an explicit protocol than with `auto`, prefer a local config override and note the result when updating docs.
