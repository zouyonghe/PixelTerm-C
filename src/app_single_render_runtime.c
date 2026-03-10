#include "app.h"
#include "renderer.h"
#include "preloader.h"
#include "text_utils.h"
#include "preload_control.h"

static void app_clear_async_render_state(PixelTermApp *app) {
    if (!app) {
        return;
    }
    app->async.image_pending = FALSE;
    app->async.image_index = -1;
    g_clear_pointer(&app->async.image_path, g_free);
}

ErrorCode app_display_image_info(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    if (app->info_visible) {
        app->info_visible = FALSE;
        return app_render_current_image(app);
    }

    app->info_visible = TRUE;

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    gint width, height;
    ErrorCode error = renderer_get_media_dimensions(filepath, &width, &height);
    if (error != ERROR_NONE) {
        return error;
    }

    gchar *basename = g_path_get_basename(filepath);
    gchar *dirname = g_path_get_dirname(filepath);
    gchar *safe_basename = sanitize_for_terminal(basename);
    gchar *safe_dirname = sanitize_for_terminal(dirname);
    gint64 file_size = get_file_size(filepath);
    const char *ext = get_file_extension(filepath);

    gdouble file_size_mb = file_size / (1024.0 * 1024.0);
    gdouble aspect_ratio = (height > 0) ? (gdouble)width / height : 1.0;
    gint index = app_get_current_index(app) + 1;
    gint total = app_get_total_images(app);

    printf("\n\033[G");

    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    printf("\033[36m📸 Image Details\033[0m");
    printf("\n\033[G");
    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    printf("\033[36m📁 Filename:\033[0m %s\n\033[G", safe_basename);
    printf("\033[36m📂 Path:\033[0m %s\n\033[G", safe_dirname);
    printf("\033[36m📄 Index:\033[0m %d/%d\n\033[G", index, total);
    printf("\033[36m💾 File size:\033[0m %.1f MB\n\033[G", file_size_mb);
    printf("\033[36m📐 Dimensions:\033[0m %d x %d pixels\n\033[G", width, height);
    printf("\033[36m🎨 Format:\033[0m %s\n\033[G", ext ? ext + 1 : "unknown");
    printf("\033[36m🎭 Color mode:\033[0m RGB\n\033[G");
    printf("\033[36m📏 Aspect ratio:\033[0m %.2f\n\033[G", aspect_ratio);
    for (gint i = 0; i < 60; i++) printf("=");

    printf("\033[0m");
    fflush(stdout);

    g_free(safe_basename);
    g_free(safe_dirname);
    g_free(basename);
    g_free(dirname);

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
