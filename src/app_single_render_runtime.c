#include "app.h"
#include "renderer.h"
#include "preloader.h"
#include "text_utils.h"
#include "preload_control.h"
#include "app_single_render_internal.h"

void app_render_help_overlay(PixelTermApp *app);

ErrorCode app_display_image_info(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    app->info_visible = !app->info_visible;
    return app_render_current_image(app);
}

ErrorCode app_display_help(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    app->help_visible = !app->help_visible;
    if (!app->help_visible) {
        return app_render_by_mode(app);
    }
    ErrorCode err = app_render_by_mode(app);
    if (err != ERROR_NONE) {
        return err;
    }
    app_render_help_overlay(app);
    return ERROR_NONE;
}

ErrorCode app_refresh_display(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    get_terminal_size(&app->term_width, &app->term_height);

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

    app_preloader_update_terminal(app);

    if (app->gif_player) {
        gif_player_update_terminal_size(app->gif_player);
    }
    if (app->video_player) {
        video_player_update_terminal_size(app->video_player);
    }

    return app_render_current_image(app);
}

void app_process_async_render(PixelTermApp *app) {
    if (!app || !app->async.image_pending) {
        return;
    }
    if (!app_is_single_mode(app)) {
        app_clear_async_render_state(app);
        return;
    }
    if (!app->preloader || !app->preload_enabled) {
        app_clear_async_render_state(app);
        return;
    }

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        app_clear_async_render_state(app);
        return;
    }
    if (app->current_index != app->async.image_index ||
        g_strcmp0(filepath, app->async.image_path) != 0) {
        return;
    }

    gint target_width = 0, target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);
    gint cached_width = 0, cached_height = 0;
    if (!preloader_get_cached_image_dimensions(app->preloader, filepath, target_width, target_height,
                                               &cached_width, &cached_height)) {
        return;
    }
    (void)cached_width;
    (void)cached_height;

    app->async.render_force_sync = TRUE;
    app->suppress_full_clear = TRUE;
    app_clear_async_render_state(app);
    app_render_current_image(app);
}
