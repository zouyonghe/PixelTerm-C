# 🎮 Controls

*English | [中文](CONTROLS_zh.md) | [日本語](CONTROLS_ja.md)*

### Global Controls

| Key | Function |
|-----|----------|
| ESC | Exit application |
| Ctrl+C | Force exit |

### Mouse Controls

Mouse interaction significantly enhances navigation and selection across different modes.

| Control | Function | Applicable Modes | Notes |
|---------|----------|------------------|-------|
| Left Click | Advance to next image | Image View (Single Image Mode) | In Video View, toggles play/pause. |
| Double Left Click | Switch to Grid Preview | Image View (Single Image Mode) | |
| Mouse Scroll Up/Down | Zoom in/out (cursor over image) | Image View (Single Image Mode) | Not for video/animated; ignored outside image. |
| Left Click | Select image | Grid Preview | Selects the image under the cursor. |
| Double Left Click | Open selected image in Image View | Grid Preview | Opens the image at the cursor position. |
| Mouse Scroll Up/Down | Page Up/Down | Grid Preview | Scroll through pages of images. |
| Left Click | Select page | Book Preview | Selects the page under the cursor. |
| Double Left Click | Open selected page in Book Reader | Book Preview | Opens the page at the cursor position. |
| Mouse Scroll Up/Down | Page Up/Down | Book Preview | Scroll through pages. |
| Left Click | Advance page | Book Reader | Advances by 1 or 2 pages depending on layout. |
| Double Left Click | Switch to Book Preview | Book Reader | |
| Mouse Scroll Up/Down | Previous/Next page | Book Reader | |
| Left Click | Select entry | File Manager Mode | Selects the file or directory under the cursor. |
| Double Left Click | Open selected entry (directory/file) | File Manager Mode | Navigates into a directory or opens an image. |
| Mouse Scroll Up/Down | Navigate entries up/down | File Manager Mode | Scroll through the list of files and directories. |

### Image View (Single Image Mode)

This is the default mode when viewing a single image.

| Key | Function |
|-----|----------|
| ←/→ | Previous/next image |
| ↑/↓ | Previous/next image |
| h/k | Vim-style navigation (previous image) |
| l/j | Vim-style navigation (next image) |
| Enter | Toggle into Grid Preview mode |
| TAB | Cycle between Image View / Grid Preview / File Manager |
| i | Toggle image information display |
| `~` / `` ` `` | Toggle Zen mode (hide/show all UI text) |
| r | Delete current image |
| d/D | Toggle dithering on/off |

### Video Playback

These shortcuts apply when the current item in Image View is a video.

| Key | Function |
|-----|----------|
| Left Click | Toggle play/pause |
| Space | Toggle play/pause |
| ←/→ | Seek backward/forward 5 seconds |
| ↑/↓ | Switch to previous/next media item |
| F | Toggle FPS overlay |
| + / - | Adjust video scale (`+` to increase, `-` to decrease) |
| p / P | Switch video protocol: `text -> sixel -> iterm2 -> kitty -> text` |

### Grid Preview (Thumbnail Mode)

This mode displays multiple image thumbnails in a grid.

| Key | Function |
|-----|----------|
| ←/→ | Move selection left/right |
| ↑/↓ | Move selection up/down |
| h/j/k/l | Vim-style navigation (left/down/up/right) |
| PgUp/PgDn | Page up/down through the grid |
| Enter | Open selected image in Image View |
| TAB | Cycle between Image View / Grid Preview / File Manager |
| `~` / `` ` `` | Toggle Zen mode (hide/show all UI text) |
| r | Delete selected image |
| d/D | Toggle dithering on/off |
| +/= | Zoom in |
| - | Zoom out |

### Book Reader (Reader Mode)

| Key | Function |
|-----|----------|
| ←/→ | Previous/Next page |
| ↑/↓ | Page up/down (single page: 1; double page: 2) |
| h/j/k/l | Vim-style navigation (left/down/up/right) |
| PgUp/PgDn | Page up/down |
| P | Jump to page (type digits, Enter to jump, P/ESC to cancel) |
| Enter | Switch to Book Preview |
| TAB | Return to File Manager |
| `~` / `` ` `` | Toggle Zen mode (hide/show all UI text) |

Note: Book view automatically switches between single-page and double-page layout based on terminal aspect ratio.

### Book Preview (Page Grid)

| Key | Function |
|-----|----------|
| ←/→ | Move selection left/right |
| ↑/↓ | Move selection up/down |
| h/j/k/l | Vim-style navigation (left/down/up/right) |
| PgUp/PgDn | Page up/down through the grid |
| P | Jump to page (type digits, Enter to jump, P/ESC to cancel) |
| Enter | Open selected page in Book Reader |
| TAB | Return to File Manager |
| +/= | Zoom in |
| - | Zoom out |

### File Manager Mode

This mode allows browsing through directories and files. Note that Vim-style navigation (h/j/k/l) is not supported here, as letter keys are reserved for quickly jumping to file entries.

| Key | Function |
|-----|----------|
| ←/→ | Go to parent directory / Open selected directory/file |
| ↑/↓ | Navigate entries up/down |
| Enter | Open selected directory or file |
| TAB | Open image preview/book preview or return |
| Backspace | Toggle hidden files |
| Any Letter (a-z/A-Z) | Jump to next entry starting with that letter |
