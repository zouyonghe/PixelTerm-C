#ifndef APP_H
#define APP_H

#include "common.h"
#include "preloader.h"

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
    
    // Legacy threading (deprecated)
    GThread *preload_thread;
    GMutex preload_mutex;
    GQueue *preload_queue;
    GHashTable *render_cache;
    GMutex cache_mutex;
    
    // Application state
    gboolean running;
    gboolean show_info;
    gboolean info_visible;  // Track if info is currently displayed
    gboolean preload_enabled;
    gboolean needs_redraw;
    gboolean file_manager_mode;  // Track if file manager is active
    
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
} PixelTermApp;

// Application lifecycle functions
PixelTermApp* app_create(void);
void app_destroy(PixelTermApp *app);
ErrorCode app_initialize(PixelTermApp *app);
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
ErrorCode app_render_file_manager(PixelTermApp *app);

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
