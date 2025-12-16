#ifndef APP_H
#define APP_H

#include "common.h"
#include "preloader.h"
#include "gif_player.h"

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
    gboolean preload_enabled;
    gboolean dither_enabled;
    gboolean needs_redraw;
    gboolean file_manager_mode;  // Track if file manager is active
    gboolean show_hidden_files;  // Toggle visibility of dotfiles in file manager
    gboolean preview_mode;       // Grid preview mode
    gint preview_zoom;           // Preview zoom level (legacy, kept for compatibility)
    gint return_to_mode;         // Return mode after file manager (0=single, 1=preview, -1=none)
    
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

    // Preview grid state
    gint preview_selected;
    gint preview_scroll;
} PixelTermApp;

// Application lifecycle functions
PixelTermApp* app_create(void);
void app_destroy(PixelTermApp *app);
ErrorCode app_initialize(PixelTermApp *app, gboolean dither_enabled);
ErrorCode app_load_directory(PixelTermApp *app, const char *directory);
ErrorCode app_load_single_file(PixelTermApp *app, const char *filepath);

// Navigation functions
ErrorCode app_next_image(PixelTermApp *app);
ErrorCode app_previous_image(PixelTermApp *app);
ErrorCode app_goto_image(PixelTermApp *app, gint index);

// Directory switching functions
ErrorCode app_switch_to_next_directory(PixelTermApp *app);
ErrorCode app_switch_to_parent_directory(PixelTermApp *app);

// File manager functions
ErrorCode app_enter_file_manager(PixelTermApp *app);
ErrorCode app_exit_file_manager(PixelTermApp *app);
ErrorCode app_file_manager_up(PixelTermApp *app);
ErrorCode app_file_manager_down(PixelTermApp *app);
ErrorCode app_file_manager_left(PixelTermApp *app);
ErrorCode app_file_manager_right(PixelTermApp *app);
ErrorCode app_file_manager_enter(PixelTermApp *app);
ErrorCode app_file_manager_jump_to_letter(PixelTermApp *app, char letter);
ErrorCode app_file_manager_refresh(PixelTermApp *app);
ErrorCode app_file_manager_toggle_hidden(PixelTermApp *app);
ErrorCode app_render_file_manager(PixelTermApp *app);
ErrorCode app_handle_mouse_file_manager(PixelTermApp *app, gint mouse_x, gint mouse_y);

// Preview grid functions
ErrorCode app_enter_preview(PixelTermApp *app);
ErrorCode app_exit_preview(PixelTermApp *app, gboolean open_selected);
ErrorCode app_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col);
ErrorCode app_preview_change_zoom(PixelTermApp *app, gint delta);
ErrorCode app_preview_page_move(PixelTermApp *app, gint direction);
ErrorCode app_render_preview_grid(PixelTermApp *app);
ErrorCode app_preview_print_info(PixelTermApp *app);
ErrorCode app_handle_mouse_click_preview(PixelTermApp *app, gint mouse_x, gint mouse_y);

// Display functions
ErrorCode app_render_current_image(PixelTermApp *app);
ErrorCode app_display_image_info(PixelTermApp *app);
ErrorCode app_refresh_display(PixelTermApp *app);

// State management
void app_toggle_preload(PixelTermApp *app);
gboolean app_should_exit(const PixelTermApp *app);
ErrorCode app_delete_current_image(PixelTermApp *app);

// Utility functions
gint app_get_current_index(const PixelTermApp *app);
gint app_get_total_images(const PixelTermApp *app);
const gchar* app_get_current_filepath(const PixelTermApp *app);
gboolean app_has_images(const PixelTermApp *app);

#endif // APP_H
