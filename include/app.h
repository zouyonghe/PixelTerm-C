#ifndef APP_H
#define APP_H

#include "common.h"
#include "preloader.h"
#include "gif_player.h"

typedef enum {
    RETURN_MODE_NONE = -1,
    RETURN_MODE_SINGLE = 0,
    RETURN_MODE_PREVIEW = 1,
    RETURN_MODE_PREVIEW_VIRTUAL = 2
} ReturnMode;

// Main application structure
typedef struct {
    // Chafa integration
    ChafaCanvas *canvas;
    ChafaCanvasConfig *canvas_config;
    ChafaTermInfo *term_info;
    
    // File management
    GList *image_files;
    gchar *current_directory;
    gint current_index;
    gint total_images;
    
    // Preloading
    ImagePreloader *preloader;
    
    // Animation support
    GifPlayer *gif_player;
    
    // Application state
    gboolean running;
    gboolean show_info;
    gboolean info_visible;  // Track if info is currently displayed
    gboolean ui_text_hidden; // Hide all UI text overlays (single/preview)
    gboolean clear_workaround_enabled; // Enable double-clear workaround on full refresh
    gboolean preload_enabled;
    gboolean dither_enabled;
    gint render_work_factor;
    gboolean force_sixel;
    gboolean needs_redraw;
    gboolean file_manager_mode;  // Track if file manager is active
    gboolean show_hidden_files;  // Toggle visibility of dotfiles in file manager
    gboolean preview_mode;       // Grid preview mode
    gint preview_zoom;           // Preview zoom level (legacy, kept for compatibility)
    ReturnMode return_to_mode;   // Return mode after file manager
    gboolean suppress_full_clear; // Skip full clear on next single-image refresh
    
    // Terminal info
    gint term_width;
    gint term_height;
    
    // Error handling
    ErrorCode last_error;
    GError *gerror;
    
    // File manager state
    gchar *file_manager_directory;
    GList *directory_entries;
    gint selected_entry;
    gint scroll_offset;
    gint previous_selected_entry; // Remember previous selection when entering yellow preview mode

    // Preview grid state
    gint preview_selected;
    gint preview_scroll;
    gboolean needs_screen_clear; // Flag to indicate if screen needs full clear

    // Input state
    gboolean pending_single_click; // For single image view
    gint64 pending_click_time;

    gboolean pending_grid_single_click; // For preview grid view
    gint64 pending_grid_click_time;
    gint pending_grid_click_x;
    gint pending_grid_click_y;

    gboolean pending_file_manager_single_click;
    gint64 pending_file_manager_click_time;
    gint pending_file_manager_click_x;
    gint pending_file_manager_click_y;
} PixelTermApp;

// Application lifecycle functions
/**
 * @brief Creates a new PixelTermApp instance and initializes its members.
 * 
 * Allocates memory for a new `PixelTermApp` structure and sets all its
 * pointers to NULL and other members to their default initial values.
 * 
 * @return A pointer to the newly created `PixelTermApp` instance on success,
 *         or NULL if memory allocation fails.
 */
PixelTermApp* app_create(void);
/**
 * @brief Destroys a PixelTermApp instance and frees all associated resources.
 * 
 * This function is responsible for cleaning up all allocated memory and
 * stopping any running threads (e.g., preloader, GIF player) associated
 * with the provided `PixelTermApp` instance.
 * 
 * @param app A pointer to the `PixelTermApp` instance to destroy.
 */
void app_destroy(PixelTermApp *app);
/**
 * @brief Initializes the PixelTermApp instance.
 * 
 * Sets up terminal capabilities, retrieves terminal dimensions, creates
 * Chafa canvas configuration and canvas, and initializes the GIF player.
 * 
 * @param app A pointer to the `PixelTermApp` instance to initialize.
 * @param dither_enabled Boolean indicating whether dithering should be enabled.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if initialization fails.
 */
ErrorCode app_initialize(PixelTermApp *app, gboolean dither_enabled);
/**
 * @brief Loads image files from a specified directory into the application.
 * 
 * Cleans up any existing image lists and preloader state, then scans the
 * given directory for supported image files. The found files are sorted
 * and added to the application's image list. If preloading is enabled,
 * it initializes and starts the preloader for the new directory.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param directory The path to the directory to load images from.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         directory cannot be loaded or scanned. Note: returns `ERROR_NONE`
 *         even if no images are found in the directory; callers should check
 *         `app_has_images()`.
 */
ErrorCode app_load_directory(PixelTermApp *app, const char *directory);
/**
 * @brief Loads a single image file into the application.
 * 
 * Determines the directory of the provided file and loads all images from
 * that directory, then sets the current image to the specified file.
 * This function also performs validation to ensure the file is an image.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param filepath The full path to the image file to load.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         file is not found, is not a valid image, or the directory
 *         loading fails.
 */
ErrorCode app_load_single_file(PixelTermApp *app, const char *filepath);

// Navigation functions
/**
 * @brief Navigates to the next image in the current list.
 * 
 * If the current image is the last one, it wraps around to the first image.
 * Triggers a redraw and updates preloader tasks if the image changes.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or `ERROR_INVALID_IMAGE` if no images are loaded.
 */
ErrorCode app_next_image(PixelTermApp *app);
/**
 * @brief Navigates to the previous image in the current list.
 * 
 * If the current image is the first one, it wraps around to the last image.
 * Triggers a redraw and updates preloader tasks if the image changes.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or `ERROR_INVALID_IMAGE` if no images are loaded.
 */
ErrorCode app_previous_image(PixelTermApp *app);
/**
 * @brief Navigates to a specific image by its index.
 * 
 * If the provided index is valid and different from the current image index,
 * the application will update its state to display the new image.
 * Triggers a redraw and updates preloader tasks if the image changes.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param index The 0-based index of the image to navigate to.
 * @return `ERROR_NONE` on success, or `ERROR_INVALID_IMAGE` if no images are loaded
 *         or the index is out of bounds.
 */
ErrorCode app_goto_image(PixelTermApp *app, gint index);

// Directory switching functions
/**
 * @brief Switches to the next directory in a predefined sequence or logic.
 * 
 * The exact logic for determining the "next" directory is implemented internally.
 * This function handles changing the current directory context and reloading images.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         directory cannot be switched or loaded.
 */
ErrorCode app_switch_to_next_directory(PixelTermApp *app);
/**
 * @brief Switches the application's context to the parent of the current directory.
 * 
 * If the application is already at the root of the filesystem or if there's
 * no accessible parent, it remains in the current directory. This function
 * handles updating the current directory context and reloading images.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         parent directory cannot be accessed or loaded.
 */
ErrorCode app_switch_to_parent_directory(PixelTermApp *app);

// File manager functions
/**
 * @brief Enters the file manager mode.
 * 
 * This function transitions the application into file manager mode,
 * stopping any active GIF playback and preparing the file manager
 * view. It sets the initial selection and scroll offset, and refreshes
 * the file listing for the current or specified directory.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if an
 *         error occurs during file manager setup or directory refresh.
 */
ErrorCode app_enter_file_manager(PixelTermApp *app);
/**
 * @brief Exits the file manager mode.
 * 
 * This function transitions the application out of file manager mode,
 * cleans up file manager related resources, and resets display flags
 * to prepare for image viewing.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if an
 *         error occurs during cleanup.
 */
ErrorCode app_exit_file_manager(PixelTermApp *app);
/**
 * @brief Moves the selection up in the file manager view.
 * 
 * Decrements the selected entry index, wrapping around to the last entry
 * if already at the top. Adjusts the scroll offset to keep the selection
 * visible on screen.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or `ERROR_MEMORY_ALLOC` if the app
 *         is NULL or not in file manager mode.
 */
ErrorCode app_file_manager_up(PixelTermApp *app);
/**
 * @brief Moves the selection down in the file manager view.
 * 
 * Increments the selected entry index, wrapping around to the first entry
 * if already at the bottom. Adjusts the scroll offset to keep the selection
 * visible on screen.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or `ERROR_MEMORY_ALLOC` if the app
 *         is NULL or not in file manager mode.
 */
ErrorCode app_file_manager_down(PixelTermApp *app);
/**
 * @brief Navigates to the parent directory in file manager mode.
 * 
 * Changes the current directory to its parent. If already at the root,
 * or if the parent is inaccessible, the directory remains unchanged.
 * The selection is restored to the previous child directory if possible.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, `ERROR_FILE_NOT_FOUND` if the parent
 *         directory cannot be canonicalized, or `ERROR_MEMORY_ALLOC`
 *         if the app is NULL or not in file manager mode.
 */
ErrorCode app_file_manager_left(PixelTermApp *app);
/**
 * @brief Attempts to enter the currently selected entry in file manager mode.
 * 
 * If the selected entry is a directory, it navigates into that directory.
 * If it's an image file, it loads the image and exits file manager mode.
 * This effectively delegates to `app_file_manager_enter`.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         operation fails (e.g., invalid selection, file not found).
 */
ErrorCode app_file_manager_right(PixelTermApp *app);
/**
 * @brief Enters the currently selected entry in file manager mode.
 * 
 * If the selected entry is a directory, the application navigates into it
 * and refreshes the file manager view. If it's a valid image file, the
 * image is loaded, and the application exits file manager mode to display
 * the image.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         selection is invalid, the file/directory cannot be accessed, or
 *         the image is invalid.
 */
ErrorCode app_file_manager_enter(PixelTermApp *app);
/**
 * @brief Jumps the file manager selection to the next entry starting with a specific letter.
 * 
 * Searches for the next entry in the current file manager listing whose name
 * (case-insensitive) begins with the provided letter, starting from the current
 * selection and wrapping around if necessary. The view is adjusted to show
 * the new selection.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param letter The character to search for.
 * @return `ERROR_NONE` on success.
 */
ErrorCode app_file_manager_jump_to_letter(PixelTermApp *app, char letter);
/**
 * @brief Refreshes the file manager's directory listing.
 * 
 * Clears existing entries, rescans the current file manager directory,
 * and rebuilds the list of entries, applying sorting and visibility filters.
 * The selection is then adjusted to maintain visibility.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or `ERROR_FILE_NOT_FOUND` if the
 *         directory cannot be opened or accessed.
 */
ErrorCode app_file_manager_refresh(PixelTermApp *app);
/**
 * @brief Toggles the visibility of hidden files in the file manager.
 * 
 * When toggled, the file manager listing is refreshed. The previous selection
 * is preserved if the entry remains visible, and the scroll offset is adjusted.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         file manager is not active or refreshing fails.
 */
ErrorCode app_file_manager_toggle_hidden(PixelTermApp *app);
/**
 * @brief Checks if the currently selected entry in the file manager is an image file.
 * 
 * This function verifies if the selected item is a regular file and if it
 * can be identified as a supported image format.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `TRUE` if the selected entry is a valid image file, `FALSE` otherwise.
 */
gboolean app_file_manager_selection_is_image(PixelTermApp *app);
/**
 * @brief Retrieves the index of the currently selected image file within the application's image list.
 * 
 * This function is relevant when in file manager mode and an image file is selected.
 * It searches the main `image_files` list for the selected file's path to return its index.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return The 0-based index of the selected image in the `image_files` list,
 *         or -1 if the file manager is not active, no entry is selected,
 *         or the selected entry is not found in the image list.
 */
gint app_file_manager_get_selected_image_index(PixelTermApp *app);
/**
 * @brief Checks if the current file manager directory contains any valid image files.
 * 
 * This function iterates through the entries in the currently browsed directory
 * to determine if at least one image file is present.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `TRUE` if the directory contains one or more image files, `FALSE` otherwise.
 */
gboolean app_file_manager_has_images(PixelTermApp *app);
/**
 * @brief Renders the current state of the file manager to the terminal.
 * 
 * This function calculates the layout for the file manager, displays the
 * directory path, and lists the entries with appropriate highlighting for
 * selected items and scroll position.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on successful rendering, or an appropriate `ErrorCode`
 *         if the file manager is not active or other rendering issues occur.
 */
ErrorCode app_render_file_manager(PixelTermApp *app);
/**
 * @brief Handles a mouse click event in the file manager view.
 * 
 * Translates mouse coordinates into a selection within the file manager list,
 * updating the `selected_entry` and adjusting the scroll offset to keep
 * the new selection visible.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param mouse_x The X-coordinate of the mouse click.
 * @param mouse_y The Y-coordinate of the mouse click.
 * @return `ERROR_NONE` on success, or `ERROR_MEMORY_ALLOC` if the app
 *         is NULL or not in file manager mode.
 */
ErrorCode app_handle_mouse_file_manager(PixelTermApp *app, gint mouse_x, gint mouse_y);
/**
 * @brief Opens the file manager entry located at the specified mouse position.
 * 
 * Performs the same hit-test logic as a single click but immediately
 * invokes `app_file_manager_enter()` without altering the current selection
 * if the operation fails. This is intended for double-click scenarios where
 * the entry should be opened directly.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param mouse_x The X-coordinate of the mouse double click.
 * @param mouse_y The Y-coordinate of the mouse double click.
 * @return `ERROR_NONE` if the entry was opened, or an `ErrorCode`
 *         describing why the action failed.
 */
ErrorCode app_file_manager_enter_at_position(PixelTermApp *app, gint mouse_x, gint mouse_y);

// Preview grid functions
/**
 * @brief Enters the preview grid mode.
 * 
 * This function transitions the application into a grid view of images,
 * stopping any active GIF playback and preparing the grid layout.
 * The preview state variables are initialized, and the grid is rendered.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if no
 *         images are loaded or an error occurs during setup.
 */
ErrorCode app_enter_preview(PixelTermApp *app);
/**
 * @brief Exits the preview grid mode.
 * 
 * This function transitions the application out of the preview grid mode.
 * If `open_selected` is TRUE, the currently selected image in the grid
 * will be loaded and displayed in single image view.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param open_selected A boolean indicating whether to open the selected
 *                      image in single view mode upon exiting preview.
 * @return `ERROR_NONE` on success.
 */
ErrorCode app_exit_preview(PixelTermApp *app, gboolean open_selected);
/**
 * @brief Moves the selection within the preview grid.
 * 
 * Adjusts the currently selected image in the grid based on row and
 * column deltas. Handles wrapping around rows/columns and adjusts
 * the scroll offset to ensure the new selection is visible.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param delta_row The number of rows to move the selection (e.g., -1 for up, 1 for down).
 * @param delta_col The number of columns to move the selection (e.g., -1 for left, 1 for right).
 * @return `ERROR_NONE` on success, or `ERROR_MEMORY_ALLOC` if the app
 *         is NULL or not in preview mode, or `ERROR_INVALID_IMAGE` if no images are loaded.
 */
ErrorCode app_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col);
/**
 * @brief Changes the zoom level (cell width) in the preview grid.
 * 
 * Adjusts the target column count for the grid, effectively zooming in or out.
 * The `delta` parameter determines the direction and magnitude of the zoom change.
 * The new zoom level is clamped within reasonable minimum and maximum column counts.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param delta An integer representing the change in zoom level (e.g., positive for zoom in, negative for zoom out).
 * @return `ERROR_NONE` on success, or `ERROR_MEMORY_ALLOC` if the app is NULL
 *         or not in preview mode. Returns `ERROR_NONE` without action if
 *         already at zoom limits.
 */
ErrorCode app_preview_change_zoom(PixelTermApp *app, gint delta);
/**
 * @brief Moves the preview grid selection by a full page.
 * 
 * Scrolls the preview grid up or down by the number of visible rows,
 * effectively navigating page by page.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param direction An integer representing the direction of page movement
 *                  (e.g., positive for page down, negative for page up).
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` from
 *         `app_preview_move_selection`.
 */
ErrorCode app_preview_page_move(PixelTermApp *app, gint direction);
/**
 * @brief Renders the preview grid of images to the terminal.
 * 
 * This function calculates the layout for the image grid, renders the
 * visible image thumbnails, and highlights the currently selected image.
 * It also displays relevant information for the selected image on the status line.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on successful rendering, or an appropriate `ErrorCode`
 *         if the application is not in preview mode, no images are loaded,
 *         or other rendering issues occur.
 */
ErrorCode app_render_preview_grid(PixelTermApp *app);
/**
 * @brief Updates only the affected preview cells after a selection change.
 *
 * Updates only the selection border and filename line when the scroll position
 * does not change, avoiding a full grid refresh.
 *
 * @param app A pointer to the `PixelTermApp` instance.
 * @param old_index The previously selected image index.
 * @return `ERROR_NONE` on successful rendering, or an appropriate `ErrorCode`
 *         if the application is not in preview mode or no images are loaded.
 */
ErrorCode app_render_preview_selection_change(PixelTermApp *app, gint old_index);
/**
 * @brief Prints brief information for the currently selected item in preview mode.
 * 
 * This function displays details such as filename, path, dimensions, and file size
 * for the image currently highlighted in the preview grid. The information is
 * rendered on a dedicated status line.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         application is not in preview mode or no images are loaded.
 */
ErrorCode app_preview_print_info(PixelTermApp *app);
// Handle mouse click in preview grid mode
/**
 * @brief Handles a mouse click event in the preview grid view.
 * 
 * Translates mouse coordinates into a selection within the preview grid,
 * updating the `preview_selected` index. This function is typically used
 * to select an image for single view or to highlight an image in the grid.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @param mouse_x The X-coordinate of the mouse click.
 * @param mouse_y The Y-coordinate of the mouse click.
 * @param redraw_needed A boolean pointer that will be set to `TRUE` if the
 *                      click resulted in a change requiring a screen redraw.
 * @return `ERROR_NONE` on success, or `ERROR_MEMORY_ALLOC` if the app
 *         is NULL or not in preview mode.
 */
ErrorCode app_handle_mouse_click_preview(PixelTermApp *app, gint mouse_x, gint mouse_y, gboolean *redraw_needed);

// Display functions
/**
 * @brief Renders the currently selected image to the terminal.
 * 
 * This function handles fetching the image (either from cache or by rendering),
 * applying dithering if enabled, and displaying it on the terminal.
 * It also manages GIF playback and displays filename information.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on successful rendering, or an appropriate `ErrorCode`
 *         if no images are loaded, the file is not found, or rendering fails.
 */
ErrorCode app_render_current_image(PixelTermApp *app);
/**
 * @brief Toggles the display of detailed information for the current image.
 * 
 * When activated, this function displays comprehensive metadata about the
 * currently viewed image, including dimensions, file size, format, and
 * aspect ratio. If the information is already visible, it hides it and
 * redraws the image.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if no
 *         images are loaded or the image file is not found/invalid.
 */
ErrorCode app_display_image_info(PixelTermApp *app);
/**
 * @brief Refreshes the entire application display.
 * 
 * This function updates the terminal size and then delegates to the
 * appropriate rendering function based on the current application mode
 * (preview grid, file manager, or single image view). It also updates
 * preloader and GIF player with new terminal dimensions.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if rendering
 *         of the current mode fails.
 */
ErrorCode app_refresh_display(PixelTermApp *app);
/**
 * @brief Renders the display based on the current application mode.
 *
 * This function chooses the appropriate renderer for preview grid,
 * file manager, or single image view.
 *
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if rendering
 *         of the current mode fails.
 */
ErrorCode app_render_by_mode(PixelTermApp *app);

// State management
/**
 * @brief Toggles the image preloading feature on or off.
 * 
 * When enabling preloading, if a preloader doesn't exist, it is created
 * and initialized. When disabling, the preloader is stopped and its queue cleared.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 */
void app_toggle_preload(PixelTermApp *app);
/**
 * @brief Checks if the application should terminate.
 * 
 * This function returns the current running status of the application,
 * indicating whether the main loop should continue or exit.
 * 
 * @param app A pointer to the constant `PixelTermApp` instance.
 * @return `TRUE` if the application should exit, `FALSE` otherwise.
 */
gboolean app_should_exit(const PixelTermApp *app);
/**
 * @brief Deletes the currently displayed image file from the filesystem.
 * 
 * This function removes the file corresponding to the current image.
 * It includes safety checks to prevent accidental deletion of important
 * files (e.g., symlinks, files outside the current directory). After deletion,
 * the image is removed from the application's list, and preload tasks are updated.
 * 
 * @param app A pointer to the `PixelTermApp` instance.
 * @return `ERROR_NONE` on successful deletion, or an appropriate `ErrorCode`
 *         if the file cannot be deleted, is not found, or is protected.
 */
ErrorCode app_delete_current_image(PixelTermApp *app);

// Utility functions
/**
 * @brief Retrieves the 0-based index of the currently displayed image.
 * 
 * @param app A pointer to the constant `PixelTermApp` instance.
 * @return The index of the current image, or -1 if the application instance is NULL.
 */
gint app_get_current_index(const PixelTermApp *app);
/**
 * @brief Retrieves the total number of images currently loaded in the application.
 * 
 * @param app A pointer to the constant `PixelTermApp` instance.
 * @return The total number of images, or 0 if the application instance is NULL or no images are loaded.
 */
gint app_get_total_images(const PixelTermApp *app);
/**
 * @brief Retrieves the full path of the currently displayed image file.
 * 
 * @param app A pointer to the constant `PixelTermApp` instance.
 * @return A constant pointer to the string representing the current image's
 *         filepath, or NULL if the application instance is NULL, no images
 *         are loaded, or the current index is invalid. The returned pointer
 *         points to internal data and should not be freed.
 */
const gchar* app_get_current_filepath(const PixelTermApp *app);
/**
 * @brief Checks if the application currently has any images loaded.
 * 
 * @param app A pointer to the constant `PixelTermApp` instance.
 * @return `TRUE` if images are loaded, `FALSE` otherwise.
 */
gboolean app_has_images(const PixelTermApp *app);

#endif // APP_H
