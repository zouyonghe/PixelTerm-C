#ifndef APP_STATE_H
#define APP_STATE_H

#include "common.h"
#include "preloader.h"
#include "gif_player.h"
#include "video_player.h"
#include "book.h"

typedef enum {
    RETURN_MODE_NONE = -1,
    RETURN_MODE_SINGLE = 0,
    RETURN_MODE_PREVIEW = 1,
    RETURN_MODE_PREVIEW_VIRTUAL = 2
} ReturnMode;

typedef enum {
    APP_MODE_SINGLE = 0,
    APP_MODE_PREVIEW,
    APP_MODE_FILE_MANAGER,
    APP_MODE_BOOK,
    APP_MODE_BOOK_PREVIEW
} AppMode;

typedef enum {
    APP_PROTOCOL_AUTO = 0,
    APP_PROTOCOL_TEXT,
    APP_PROTOCOL_SIXEL,
    APP_PROTOCOL_KITTY,
    APP_PROTOCOL_ITERM2
} AppProtocolMode;

typedef struct {
    gboolean preload_enabled;
    gboolean dither_enabled;
    gboolean alt_screen_enabled;
    gboolean clear_workaround_enabled;
    gint work_factor;
    gdouble gamma;
    gboolean gamma_set;
    AppProtocolMode protocol_mode;
    gboolean force_text;
    gboolean force_sixel;
    gboolean force_kitty;
    gboolean force_iterm2;
} AppConfig;

typedef struct {
    gint selected;
    gint scroll;
    gint zoom;
    GList *selected_link;
    gint selected_link_index;
} PreviewState;

typedef struct {
    gchar *directory;
    GList *entries;
    gint entries_count;
    gint selected_entry;
    GList *selected_link;
    gint selected_link_index;
    gint scroll_offset;
    gint previous_selected_entry;
} FileManagerState;

typedef struct {
    BookDocument *doc;
    gchar *path;
    gint page;
    gint page_count;
    gint preview_selected;
    gint preview_scroll;
    gint preview_zoom;
    gboolean jump_active;
    gboolean jump_dirty;
    gint jump_len;
    char jump_buf[16];
    BookToc *toc;
    gint toc_selected;
    gint toc_scroll;
    gboolean toc_visible;
} BookState;

typedef struct {
    gboolean pending;
    gint64 pending_time;
    gint x;
    gint y;
} ClickTracker;

typedef struct {
    ClickTracker single_click;
    ClickTracker preview_click;
    ClickTracker file_manager_click;
    gint last_mouse_x;
    gint last_mouse_y;
} InputState;

typedef struct {
    gboolean render_request;
    gboolean image_pending;
    gboolean render_force_sync;
    gint image_index;
    gchar *image_path;
} AsyncState;

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
    VideoPlayer *video_player;

    // Application state
    gboolean running;
    gboolean info_visible;  // Track if info is currently displayed
    gboolean ui_text_hidden; // Hide all UI text overlays (single/preview)
    gboolean show_fps; // Toggle FPS overlay in video view
    gdouble video_scale; // Scale factor for video render size
    gboolean clear_workaround_enabled; // Enable double-clear workaround on full refresh
    gboolean preload_enabled;
    gboolean dither_enabled;
    gint render_work_factor;
    gdouble gamma;
    gboolean force_text;
    gboolean force_sixel;
    gboolean force_kitty;
    gboolean force_iterm2;
    gboolean needs_redraw;
    AppMode mode;  // Current UI mode (single/preview/file manager/book)
    gboolean show_hidden_files;  // Toggle visibility of dotfiles in file manager
    ReturnMode return_to_mode;   // Return mode after file manager
    gboolean suppress_full_clear; // Skip full clear on next single-image refresh
    gboolean delete_pending;     // Awaiting delete confirmation
    gint last_render_top_row;    // Single-view image top row for overlays
    gint last_render_height;     // Single-view image height for overlays
    gdouble image_zoom;          // Zoom factor for single image view
    gdouble image_pan_x;         // Pan offset in pixels for zoomed single image view
    gdouble image_pan_y;
    gint image_view_left_col;    // Single image render left column
    gint image_view_top_row;     // Single image render top row
    gint image_view_width;       // Single image render width in cells
    gint image_view_height;      // Single image render height in cells
    gint image_viewport_px_w;    // Single image viewport width in pixels
    gint image_viewport_px_h;    // Single image viewport height in pixels

    // Terminal info
    gint term_width;
    gint term_height;

    // Error handling
    ErrorCode last_error;
    GError *gerror;

    // File manager state
    FileManagerState file_manager;

    // Preview grid state
    PreviewState preview;
    gboolean needs_screen_clear; // Flag to indicate if screen needs full clear

    // Book state
    BookState book;

    // Input state
    InputState input;

    // Async rendering state
    AsyncState async;
} PixelTermApp;

static inline gboolean app_is_mode(const PixelTermApp *app, AppMode mode) {
    return app && app->mode == mode;
}

static inline gboolean app_is_single_mode(const PixelTermApp *app) {
    return app_is_mode(app, APP_MODE_SINGLE);
}

static inline gboolean app_is_preview_mode(const PixelTermApp *app) {
    return app_is_mode(app, APP_MODE_PREVIEW);
}

static inline gboolean app_is_file_manager_mode(const PixelTermApp *app) {
    return app_is_mode(app, APP_MODE_FILE_MANAGER);
}

static inline gboolean app_is_book_mode(const PixelTermApp *app) {
    return app_is_mode(app, APP_MODE_BOOK);
}

static inline gboolean app_is_book_preview_mode(const PixelTermApp *app) {
    return app_is_mode(app, APP_MODE_BOOK_PREVIEW);
}

#endif // APP_STATE_H
