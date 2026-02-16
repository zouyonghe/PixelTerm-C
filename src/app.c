#define _GNU_SOURCE

#include "app.h"
#include "preload_control.h"

static const gdouble k_book_spread_ratio = 1.0;
static const gint k_book_spread_min_cols = 120;
static const gint k_book_spread_min_rows = 24;
static const gint k_book_spread_min_page_cols = 60;
static const gint k_book_spread_gutter_cols = 2;
static const gdouble k_book_cell_aspect = 0.5;

void app_get_image_target_dimensions(const PixelTermApp *app, gint *max_width, gint *max_height);

// Create a new application instance
static void app_init_preview_state(PixelTermApp *app) {
    app->preview.selected = 0;
    app->preview.scroll = 0;
    app->preview.zoom = 0; // 0 indicates uninitialized target cell width
    app->preview.selected_link = NULL;
    app->preview.selected_link_index = -1;
}

static void app_init_file_manager_state(PixelTermApp *app) {
    app->file_manager.entries_count = 0;
    app->file_manager.selected_entry = 0;
    app->file_manager.selected_link = NULL;
    app->file_manager.selected_link_index = -1;
    app->file_manager.scroll_offset = 0;
    app->file_manager.previous_selected_entry = 0;
}

static void app_init_book_state(PixelTermApp *app) {
    app->book.page = 0;
    app->book.page_count = 0;
    app->book.preview_selected = 0;
    app->book.preview_scroll = 0;
    app->book.preview_zoom = 0;
    app->book.jump_active = FALSE;
    app->book.jump_dirty = FALSE;
    app->book.jump_len = 0;
    app->book.jump_buf[0] = '\0';
    app->book.toc_selected = 0;
    app->book.toc_scroll = 0;
    app->book.toc_visible = FALSE;
}

static void app_init_input_state(PixelTermApp *app) {
    app->input.single_click.pending = FALSE;
    app->input.single_click.pending_time = 0;
    app->input.single_click.x = 0;
    app->input.single_click.y = 0;
    app->input.preview_click.pending = FALSE;
    app->input.preview_click.pending_time = 0;
    app->input.preview_click.x = 0;
    app->input.preview_click.y = 0;
    app->input.file_manager_click.pending = FALSE;
    app->input.file_manager_click.pending_time = 0;
    app->input.file_manager_click.x = 0;
    app->input.file_manager_click.y = 0;
    app->input.last_mouse_x = 0;
    app->input.last_mouse_y = 0;
}

static void app_init_async_state(PixelTermApp *app) {
    app->async.render_request = FALSE;
    app->async.image_pending = FALSE;
    app->async.render_force_sync = FALSE;
    app->async.image_index = -1;
    app->async.image_path = NULL;
}

static void app_init_runtime_defaults(PixelTermApp *app) {
    // g_new0() already zero-initializes pointers/flags and most counters.
    app->running = TRUE;
    app->video_scale = 1.0;
    app->preload_enabled = TRUE;
    app->render_work_factor = 9;
    app->gamma = 1.0;
    app->needs_redraw = TRUE;
    app->mode = APP_MODE_SINGLE;
    app->return_to_mode = RETURN_MODE_NONE;
    app->image_zoom = 1.0;
    app->term_width = 80;
    app->term_height = 24;
    app->last_error = ERROR_NONE;
}

PixelTermApp* app_create(void) {
    PixelTermApp *app = g_new0(PixelTermApp, 1);
    if (!app) {
        return NULL;
    }

    app_init_runtime_defaults(app);
    app_init_file_manager_state(app);
    app_init_preview_state(app);
    app_init_book_state(app);
    app_init_input_state(app);
    app_init_async_state(app);

    return app;
}

gboolean app_book_use_double_page(const PixelTermApp *app) {
    if (!app_is_book_mode(app)) {
        return FALSE;
    }
    gint width = 0;
    gint height = 0;
    app_get_image_target_dimensions(app, &width, &height);
    gint term_width = app->term_width > 0 ? app->term_width : width;
    gint term_height = app->term_height > 0 ? app->term_height : height;
    if (width <= 0) {
        width = app->term_width > 0 ? app->term_width : 80;
    }
    if (height <= 0) {
        height = app->term_height > 0 ? app->term_height : 24;
    }
    if (term_width < k_book_spread_min_cols || term_height < k_book_spread_min_rows) {
        return FALSE;
    }
    if (width < k_book_spread_min_page_cols * 2 + k_book_spread_gutter_cols) {
        return FALSE;
    }
    gdouble ratio = (gdouble)term_width / (gdouble)term_height;
    ratio *= k_book_cell_aspect;
    return ratio >= k_book_spread_ratio;
}

void app_get_image_target_dimensions(const PixelTermApp *app, gint *max_width, gint *max_height) {
    if (!max_width || !max_height) {
        return;
    }
    gint width = (app && app->term_width > 0) ? app->term_width : 80;
    gint height = (app && app->term_height > 0) ? app->term_height : 24;
    if (app && app->info_visible) {
        height -= 10;
    } else {
        // Single view reserves: title (row 1), spacer (row 2), index (row 3),
        // filename (row -2), spacer (row -1), footer (row -0).
        // Keep image position/size stable even when Zen hides UI text.
        height -= 6;
    }
    if (height < 1) {
        height = 1;
    }
    if (width < 1) {
        width = 1;
    }
    *max_width = width;
    *max_height = height;
}

void app_destroy(PixelTermApp *app) {
    if (!app) {
        return;
    }

    // Stop any running threads
    app->running = FALSE;

    // Stop and destroy preloader
    app_preloader_reset(app);

    // Stop and destroy GIF player
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
        gif_player_destroy(app->gif_player);
        app->gif_player = NULL;
    }

    // Stop and destroy video player
    if (app->video_player) {
        video_player_stop(app->video_player);
        video_player_destroy(app->video_player);
        app->video_player = NULL;
    }

    app_close_book(app);


    // Cleanup Chafa resources
    if (app->canvas) {
        chafa_canvas_unref(app->canvas);
    }

    if (app->canvas_config) {
        chafa_canvas_config_unref(app->canvas_config);
    }

    if (app->term_info) {
        chafa_term_info_unref(app->term_info);
    }

    // Cleanup file list
    if (app->image_files) {
        g_list_free_full(app->image_files, (GDestroyNotify)g_free);
    }

    // Cleanup directory path
    g_free(app->current_directory);

    // Cleanup file manager entries
    if (app->file_manager.entries) {
        g_list_free_full(app->file_manager.entries, (GDestroyNotify)g_free);
    }
    g_free(app->file_manager.directory);
    g_free(app->async.image_path);

    // Cleanup error
    if (app->gerror) {
        g_error_free(app->gerror);
    }

    g_free(app);
}

// Initialize application
ErrorCode app_initialize(PixelTermApp *app, gboolean dither_enabled) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    app->dither_enabled = dither_enabled;

    // Detect terminal capabilities
    ChafaTermDb *term_db = chafa_term_db_get_default();
    if (!term_db) {
        return ERROR_CHAFA_INIT;
    }

    app->term_info = chafa_term_db_detect(term_db, NULL);
    if (!app->term_info) {
        return ERROR_CHAFA_INIT;
    }

    // Get terminal dimensions
    get_terminal_size(&app->term_width, &app->term_height);

    // Create canvas configuration
    app->canvas_config = chafa_canvas_config_new();
    if (!app->canvas_config) {
        return ERROR_CHAFA_INIT;
    }

    // Configure canvas for optimal terminal display
    chafa_canvas_config_set_geometry(app->canvas_config, app->term_width, app->term_height - 6);
    chafa_canvas_config_set_canvas_mode(app->canvas_config, CHAFA_CANVAS_MODE_TRUECOLOR);
    chafa_canvas_config_set_color_space(app->canvas_config, CHAFA_COLOR_SPACE_RGB);
    chafa_canvas_config_set_pixel_mode(app->canvas_config, CHAFA_PIXEL_MODE_SYMBOLS);

    // Create canvas
    app->canvas = chafa_canvas_new(app->canvas_config);
    if (!app->canvas) {
        return ERROR_CHAFA_INIT;
    }

    // Initialize GIF player
    app->gif_player = gif_player_new(app->render_work_factor, app->force_text, app->force_sixel,
                                     app->force_kitty, app->force_iterm2, app->gamma);
    if (!app->gif_player) {
        return ERROR_MEMORY_ALLOC;
    }

    // Initialize video player
    app->video_player = video_player_new(app->render_work_factor, app->force_text, app->force_sixel,
                                         app->force_kitty, app->force_iterm2, app->gamma);
    if (!app->video_player) {
        gif_player_destroy(app->gif_player);
        app->gif_player = NULL;
        return ERROR_MEMORY_ALLOC;
    }

    return ERROR_NONE;
}

ErrorCode app_render_by_mode(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    if (app_is_book_preview_mode(app)) {
        return app_render_book_preview(app);
    }
    if (app_is_book_mode(app)) {
        return app_render_book_page(app);
    }
    if (app_is_preview_mode(app)) {
        return app_render_preview_grid(app);
    }
    if (app_is_file_manager_mode(app)) {
        return app_render_file_manager(app);
    }
    return app_refresh_display(app);
}



// Toggle preloading
void app_toggle_preload(PixelTermApp *app) {
    if (!app) {
        return;
    }

    app->preload_enabled = !app->preload_enabled;

    if (app->preload_enabled) {
        gboolean queue_tasks = (app->preloader == NULL);
        (void)app_preloader_enable(app, queue_tasks);
        return;
    }

    app_preloader_disable(app);
}

// Check if application should exit
gboolean app_should_exit(const PixelTermApp *app) {
    return !app || !app->running;
}
