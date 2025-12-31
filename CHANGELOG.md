# Changelog

- v1.3.13: Fix GIF rendering in Single Image View.
    - **Single Image View**: Prevent GIF frames from overwriting UI text by rendering within the image area.
    - **Rendering**: Stabilize GIF frame placement and fix sixel frame output handling to avoid flicker/ghosting.
    - **UI**: Keep the filename on the second-to-last line and move the index indicator to row 2.

- v1.3.12: Sixel probing and CLI cleanup.
    - **Rendering**: Probe for sixel support when `TERM_PROGRAM` is missing and force sixel output when supported.
    - **CLI**: Remove `--term` option; help output no longer lists control keys.
    - **Completions/Docs**: Remove `--term` from shell completions and READMEs.

- v1.3.11: Adjust terminal override to avoid color regressions.
    - **CLI**: `--term` now overrides `TERM_PROGRAM` instead of `TERM`.
    - **Compatibility**: Default `TERM_PROGRAM=rio` when unset, keeping `TERM` intact for color detection.

- v1.3.10: Terminal override flag and safer defaults.
    - **CLI**: Add `--term TERM` to override terminal detection.
    - **Rendering**: Only default `TERM=rio` when `TERM_PROGRAM` is missing and `TERM` is empty or `xterm-256color`.

- v1.3.9: Codebase cleanup and documentation alignment.
    - **Rendering**: Clamp and normalize work factor using chafa's 0.0–1.0 scale.
    - **Code Cleanup**: Remove unused renderer config fields and unused input/preloader APIs.
    - **Docs**: Refresh architecture, development, status, and roadmap docs to match current behavior.

- v1.3.8: Improve SSH terminal detection defaults.
    - **Rendering**: When `TERM_PROGRAM` is missing and `TERM` is empty or `xterm-256color`, default `TERM` to `rio` to preserve image clarity in SSH sessions.

- v1.3.6: Rendering quality controls and sixel tuning.
    - **CLI**: Add `--work-factor N` (1-9, default: 9) to adjust quality vs speed.
    - **Rendering**: Align non-symbols pixel modes with TrueColor defaults, apply sixel noise dither, and set grain size to 1x1 for smoother color transitions.
    - **Docs/Completions**: Document the new flag and update shell completions.

- v1.3.5: Shell completion cleanup.
    - **Completions**: Translate completion descriptions to English.

- v1.3.4: Add shell completions and doc updates.
    - **Completions**: Add Bash/Fish/Zsh completion scripts.
    - **Docs**: Update development status notes and TAB cycling hints.
    - **Repo**: Ignore `.iflow` artifacts.

- v1.3.3: Rendering efficiency and README refresh.
    - **Rendering**: Avoid redundant redraws in preview and file manager.
    - **Docs**: Refresh README screenshots, usage notes, and add changelog link.

- v1.3.2: UI hints and clear workaround tweaks.
    - **UI**: Improve clear workaround behavior and single-view TAB hinting.

- v1.3.1: Fix preview selection on double-click; improve install portability.
    - **Preview Grid**: Double-click switching between Single Image View and Grid Preview now preserves the selected image and avoids unintended yellow (virtual) selection state.
    - **Build/Install**: Make `make install` portable across GNU/BSD `install` by removing GNU-only `-D` usage and supporting `PREFIX`/`DESTDIR`.

- v1.3.0: UI/UX improvements, Zen mode, and compatibility options.
    - **Zen Mode**: Add `~` / `` ` `` to hide/show all on-screen text in Image View and Grid Preview.
    - **Single Image View**: Add file-manager-style hints, show the current image index within the folder, and improve header layout.
    - **Grid Preview**: Add `r` to delete the selected image and reflow the grid; refine overlay layout and keep hints colorized and centered.
    - **File Manager**: Improve layout symmetry and default selection behavior when entering directories.
    - **Input**: Debounce mouse wheel paging to reduce accidental multi-page jumps.
    - **CLI**: Add `--no-alt-screen` and `--clear-workaround` for improved terminal compatibility.

- v1.2.8: File manager mouse interaction and navigation improvements.
    - **Mouse Handling**: Refactored file manager mouse handling with dedicated hit-test function for accurate position-to-entry mapping. Implemented deferred single-click handling (400ms delay) to distinguish between single and double-click actions.
    - **Navigation**: Added parent directory (..) entry at the top of file manager list for easier upward navigation. Improved preview mode navigation with proper return_to_mode state tracking for virtual and actual selections.
    - **Rendering**: Fixed screen clearing on zoom changes and terminal resize to eliminate visual artifacts. Added TAB key hint to preview grid help text.
    - **UX**: Double-click now opens entries directly without intermediate selection jumps. Centralized viewport calculation logic for better maintainability and consistent scrolling behavior.

- v1.2.6: Enhanced navigation and visual improvements.
    - **Navigation**: Implement enhanced file manager and preview navigation logic with improved startup behavior (enter file manager by default for directories), yellow border preview mode for non-image selections, blue border preview mode for image selections, and fixed Tab key navigation between modes.
    - **UX**: Add visual enhancement to highlight directories containing images in yellow for better visual distinction, while directories without images remain blue and images stay green.
    - **Performance**: Optimize preview grid rendering by clearing screen only on page changes to remove afterimage without flicker, and fix preview grid scrolling refresh and selection positioning artifacts.
    - **Navigation Logic**: Ensure proper image selection synchronization across modes and add functions for image detection and index mapping with improved return_to_mode logic for different navigation states.

- v1.2.4: Optimize rendering for flicker-free mode transitions and scrolling.
    - **Rendering**: Eliminated ghosting in File Manager by clearing lines (`\033[2K`) before printing rows. Removed full screen clears (`\033[2J`) during in-mode navigation to prevent flickering.
    - **Performance**: Prevented redundant display refreshes in Single Image View and Preview Grid when the image selection does not change.
    - **UX**: Added explicit full screen clears on entry to Grid Preview and File Manager modes to ensure a clean visual state when switching modes.

- v1.2.3: Enhanced mouse interaction and navigation polish.
    - **Single Image View**: Double-clicking switches to Grid Preview. Implemented a 400ms delay for the single-click "Next Image" action to prevent conflict, ensuring smooth transition to grid without skipping images.
    - **Preview Grid**: Double-clicking opens the image directly. Implemented delayed selection logic to prevent the selection from jumping before opening.
    - **File Manager**: Improved list centering to keep the selected item centered even when navigating to the end of the list.

- v1.2.2: Improve File Manager list centering.
    - **UX**: Allow scrolling past the bottom of the list to keep the selected item centered even when it is the last entry, ensuring consistent visual alignment.

- v1.2.1: Add mouse scroll support for Preview Grid.
    - **Preview Grid**: Enable mouse wheel to scroll/page through the image grid.

- v1.2.0: Comprehensive mouse support for enhanced navigation and interaction.
    - **Single Image View**: Left-click to next image, scroll wheel to navigate images.
    - **Preview Grid**: Left-click to select, double-click to open image.
    - **File Manager**: Left-click to select, double-click to open/enter, scroll wheel to navigate list.
    - **Technical**: Enabled SGR mouse mode (1006) for precise coordinates and scroll support. Implemented double-click detection and scroll event debouncing (100ms) for smoother experience.

- v1.1.17: Optimize navigation performance by avoiding unnecessary redraws. Added index change detection in image navigation functions (app_next_image, app_previous_image, app_goto_image) and in main input handling. The system now only triggers screen refreshes and preloader updates when the image index actually changes, eliminating unnecessary redraws when navigating to the same image (e.g., single-image directories or wrap-around scenarios). This improves responsiveness and reduces CPU usage.

- v1.1.16: Preserve preview grid scroll position. Fixes a bug where the preview grid's scroll position was reset to the top when re-entering preview mode (from single image view via Enter, or from file manager via Tab). The `app->preview_scroll = 0;` line was removed from `app_enter_preview` to ensure scroll context is maintained, improving user experience.

- v1.1.15: Align image preview order with file manager. This update ensures that image files displayed in the preview pane are sorted identically to how they appear in the file manager, using the custom AaBb… ordering. It also includes a critical fix for a segmentation fault that occurred when loading directories, caused by improper memory management during file list processing. Additionally, redundant code for `is_valid_image_file` checks has been removed for improved code clarity and minor performance gains.

- v1.1.14: Implement dithering control. Dithering is now disabled by default. Added `--dither` and `-D` command-line arguments to enable it. Implemented 'D' key binding to toggle dithering on/off in single image and preview grid modes. Fixed a bug where dithering setting was not correctly propagated to the preloader's internal renderer, and ensured cache is cleared upon toggling to force re-render. Resolved instability of dithering toggling in preview grid mode by correctly re-initializing and restarting the preloader with the updated dithering setting when toggled.

- v1.1.13: Enhance TAB key navigation and refine help text. Implemented contextual TAB behavior for file manager entry/exit, allowing return to previous view (single image or preview grid) on TAB from FM, and making TAB invalid if FM was entered directly without a prior view mode. Fixed compilation errors in src/app.c (missing parentheses in printf statements). Refined help text for preview grid mode, changing 'Enter Toggle' to 'Enter Open', replacing 'q Back' with 'ESC Exit', and capitalizing action words for consistency.

- v1.1.12: Fix mosaic artifacts in Konsole and other terminals by disabling dithering in the renderer configuration for image viewing, preview grid, and GIF playback.

- v1.1.11: Fix file manager selection jumping on long press of arrow keys by robustly parsing terminal escape sequences, supporting both `^[[` and `^[O` prefixes, and preventing greedy consumption of next sequence headers.

- v1.1.9: Fix GIF ghosting artifacts during window resize by clearing the screen area after each frame. Resolve Chafa assertion failures by correctly initializing dither_mode and color_extractor in renderer configuration. Fix compilation error in preloader module.

- v1.1.8: Add high-performance animated GIF support with correct frame timing and TrueColor rendering. Significantly improve image quality by enabling advanced Chafa features (work_factor=9, median color extraction, ordered dither, all optimizations) and removing ASCII symbol restrictions to allow full Unicode block character usage. Fix animation glitches when switching modes and ensure correct terminal resizing behavior during playback.

- v1.1.7: Unify all comments to English for better maintainability and consistency, standardize comment format throughout the codebase, improve clarity of comments in file manager scrolling functions, and fix minor comment issues in other functions.

- v1.1.6: Add invalid image file handling and display improvements, implement proper validation using magic numbers, show invalid files with [Invalid] label in file manager, only highlight filename (not [Invalid] label) when invalid files are selected, prevent opening invalid image files, add intelligent mode switching when opening invalid files, implement empty directory detection with centered (No items) message, and update browser scanning to filter out invalid image files.

- v1.1.5: Fix 0KB image file handling by implementing proper validation using magic numbers instead of strict image loading, add white background red text display for selected invalid files, show centered "（No items）" message in empty directories, skip invalid files in preview grid, and implement intelligent mode switching when opening invalid files directly.

- v1.1.4: Refactored code and documentation for improved performance and consistency. Restored ImageRenderer's internal caching due to its crucial role in performance, especially when preloading is disabled or during frequent dimension changes. Re-confirmed and removed various unused code including unnecessary <pthread.h> include, browser file filtering/sorting functions, input_is_key_pressed function, and unused mouse event fields. Synchronized docs/ARCHITECTURE.md and include/common.h with current codebase by removing outdated references.

- v1.1.1: Improve file manager scrolling/rendering so the highlighted row stays centered even when the directory has fewer rows than the viewport or when wrapping back to the top.

- v1.1.0: Fix Chinese filename centering in the browser by using UTF-8 display width instead of byte length and improve truncation to respect multibyte characters.

- v1.0.20: Improve file manager scrolling/rendering so the highlighted row stays centered even when the directory has fewer rows than the viewport or when wrapping back to the top.

- v1.0.19: Add support for image files without extensions by implementing magic number detection for JPEG, PNG, GIF, WebP, BMP, and TIFF formats, automatically detecting image files without file extensions by reading file headers while maintaining backward compatibility with extension-based detection.

- v1.0.18: Add wrap-around navigation in file manager (up from top jumps to bottom, down from bottom jumps to top), fix preview zoom jumping issues by using floating-point precision and proper rounding, improve terminal geometry fallback and Konsole handling, clamp terminal cell aspect ratio to sane range, and prefer local chafa at runtime.

- v1.0.16: File manager now sorts entries in AaBb order (uppercase before lowercase within each letter) and skips `$…` system items, matching `ls` ordering while keeping directories grouped ahead of files.

- v1.0.15: Add support for image files without extensions by implementing magic number detection for JPEG, PNG, GIF, WebP, BMP, and TIFF formats, automatically detecting image files without file extensions by reading file headers while maintaining backward compatibility with extension-based detection.

- v1.0.14: File manager header now truncates and centers long/UTF-8 paths correctly, and long file/folder names use smarter centering-friendly truncation.

- v1.0.13: Preview grid UX polish (single render on entry, predictable zoom with min 2 columns, better paging/wrap), preloader now truly pauses/disables without burning CPU and fixes cache ownership, and CLI error messages are clearer for help/version/argument cases.
