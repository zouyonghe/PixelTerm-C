#include "app.h"
#include "app_media_session.h"
#include "renderer.h"
#include "preloader.h"
#include "media_utils.h"
#include "text_utils.h"
#include "preload_control.h"
#include "grid_render.h"
#include "pixbuf_utils.h"
#include "app_single_render_test_internal.h"
#include "ui_render_utils.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>

static const AppSingleRenderTestHooks *app_single_render_test_hooks = NULL;

#define APP_SINGLE_RENDER_CALL(field, fallback, ...) \
    ((app_single_render_test_hooks && app_single_render_test_hooks->field) ? \
        app_single_render_test_hooks->field(__VA_ARGS__) : fallback(__VA_ARGS__))

void app_single_render_set_test_hooks(const AppSingleRenderTestHooks *hooks) {
    app_single_render_test_hooks = hooks;
}

void app_single_render_reset_test_hooks(void) {
    app_single_render_test_hooks = NULL;
}

static void app_clear_async_render_state(PixelTermApp *app) {
    if (!app) {
        return;
    }
    app->async.image_pending = FALSE;
    app->async.image_index = -1;
    g_clear_pointer(&app->async.image_path, g_free);
}

static void app_queue_async_render(PixelTermApp *app, const gchar *filepath,
                                   gint target_width, gint target_height) {
    if (!app || !filepath) {
        return;
    }
    app->async.image_pending = TRUE;
    app->async.image_index = app->current_index;
    g_free(app->async.image_path);
    app->async.image_path = g_strdup(filepath);
    if (app->preloader && app->preload_enabled) {
        preloader_add_task(app->preloader, filepath, 0, target_width, target_height);
    }
}

static void app_render_single_placeholder(PixelTermApp *app, const gchar *filepath) {
    if (!app || !filepath || app->ui_text_hidden) {
        return;
    }
    get_terminal_size(&app->term_width, &app->term_height);

    ui_begin_sync_update();
    printf("\033[H\033[0m");

    const char *title = "Image View";
    gint title_len = (gint)strlen(title);
    gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
    printf("\033[1;1H\033[2K");
    for (gint i = 0; i < title_pad; i++) putchar(' ');
    printf("%s", title);

    printf("\033[2;1H\033[2K");

    gint current = app_get_current_index(app) + 1;
    gint total = app_get_total_images(app);
    if (current < 1) current = 1;
    if (total < 1) total = 1;
    char idx_text[32];
    g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
    gint idx_len = (gint)strlen(idx_text);
    gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
    printf("\033[3;1H\033[2K");
    for (gint i = 0; i < idx_pad; i++) putchar(' ');
    printf("%s", idx_text);

    gchar *basename = g_path_get_basename(filepath);
    gchar *safe_basename = sanitize_for_terminal(basename);
    gint max_width = ui_filename_max_width(app);
    if (max_width <= 0) {
        max_width = app->term_width;
    }
    gchar *display_name = truncate_utf8_middle_keep_suffix(safe_basename, max_width);
    gint filename_len = utf8_display_width(display_name);
    gint filename_start_col = (app->term_width - filename_len) / 2;
    if (filename_start_col < 0) filename_start_col = 0;
    gint filename_row = (app->term_height >= 3) ? (app->term_height - 2) : 1;
    printf("\033[%d;1H\033[2K", filename_row);
    for (gint i = 0; i < filename_start_col; i++) putchar(' ');
    printf("\033[34m%s\033[0m", display_name);
    g_free(display_name);
    g_free(safe_basename);
    g_free(basename);

    if (app->term_height > 0) {
        const HelpSegment segments[] = {
            {"←/→", "Prev/Next"},
            {"Enter", "Preview"},
            {"TAB", "Toggle"},
            {"i", "Info"},
            {"r", "Delete"},
            {"~", "Zen"},
            {"ESC", "Exit"}
        };
        printf("\033[%d;1H\033[2K", app->term_height);
        ui_print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
    }

    ui_end_sync_update();
    fflush(stdout);
}


ErrorCode app_render_current_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Reset info visibility when rendering image
    app->info_visible = FALSE;

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if it's an animated image/video file and handle animation
    MediaKind media_kind = APP_SINGLE_RENDER_CALL(media_classify, media_classify, filepath);
    gboolean is_animated_image = media_is_animated_image(media_kind);
    gboolean is_video = media_is_video(media_kind);
    gboolean gif_is_animated = FALSE;

    if (is_video && app->video_player) {
        if (!app->video_player->filepath || g_strcmp0(app->video_player->filepath, filepath) != 0) {
            ErrorCode load_result = APP_SINGLE_RENDER_CALL(video_player_load,
                                                           video_player_load,
                                                           app->video_player,
                                                           filepath);
            if (load_result != ERROR_NONE) {
                is_video = FALSE;
            }
        }
    }

    if (is_animated_image && app->gif_player && !is_video) {
        // First, check if we need to load the animated image
        if (!app->gif_player->filepath || g_strcmp0(app->gif_player->filepath, filepath) != 0) {
            ErrorCode load_result = APP_SINGLE_RENDER_CALL(gif_player_load,
                                                           gif_player_load,
                                                           app->gif_player,
                                                           filepath);
            if (load_result != ERROR_NONE) {
                // If animation loading fails, just treat it as a regular image
                is_animated_image = FALSE;
            }
        }
    }
    if (is_animated_image && app->gif_player && !is_video) {
        gif_is_animated = APP_SINGLE_RENDER_CALL(gif_player_is_animated,
                                                 gif_player_is_animated,
                                                 app->gif_player);
    }

    MediaKind active_kind = MEDIA_KIND_IMAGE;
    if (is_video) {
        active_kind = MEDIA_KIND_VIDEO;
    } else if (is_animated_image) {
        active_kind = MEDIA_KIND_ANIMATED_IMAGE;
    }
    app_media_stop_inactive_players(app, active_kind);

    gint target_width = 0, target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);
    gint image_area_height = target_height;
    if (is_video) {
        gdouble scale = app->video_scale;
        if (scale < 0.3) {
            scale = 0.3;
        } else if (scale > 1.5) {
            scale = 1.5;
        }
        target_width = (gint)(target_width * scale + 0.5);
        target_height = (gint)(target_height * scale + 0.5);
        if (target_width < 1) {
            target_width = 1;
        }
        if (target_height < 1) {
            target_height = 1;
        }
    }

    gint cell_w = 0, cell_h = 0;
    APP_SINGLE_RENDER_CALL(get_terminal_cell_geometry,
                           get_terminal_cell_geometry,
                           &cell_w,
                           &cell_h);
    if (cell_w <= 0) cell_w = 10;
    if (cell_h <= 0) cell_h = 20;
    app->image_viewport_px_w = MAX(1, target_width * cell_w);
    app->image_viewport_px_h = MAX(1, target_height * cell_h);

    gdouble image_zoom = app->image_zoom;
    if (image_zoom < 1.0) {
        image_zoom = 1.0;
    }
    app->image_zoom = image_zoom;
    if (image_zoom <= 1.0) {
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;
    }

    gboolean async_request = app->async.render_request;
    app->async.render_request = FALSE;
    gboolean use_zoom = (!is_video && !gif_is_animated && image_zoom > 1.0 + 0.001);
    if (!app->async.render_force_sync &&
        async_request &&
        app->preloader &&
        app->preload_enabled &&
        !is_video &&
        !gif_is_animated &&
        !use_zoom) {
        gint cached_width = 0, cached_height = 0;
        if (!preloader_get_cached_image_dimensions(app->preloader, filepath, target_width, target_height,
                                                   &cached_width, &cached_height)) {
            app_queue_async_render(app, filepath, target_width, target_height);
            app_render_single_placeholder(app, filepath);
            return ERROR_NONE;
        }
    }
    if (app->async.render_force_sync) {
        app->async.render_force_sync = FALSE;
    }
    if (app->async.image_pending &&
        app->async.image_index == app->current_index &&
        g_strcmp0(app->async.image_path, filepath) == 0) {
        app_clear_async_render_state(app);
    }

    // Title + index area (single image view)
    gint image_area_top_row = 4; // Keep layout stable even in Zen (UI hidden)
    gint image_render_top_row = image_area_top_row;
    if (is_video && target_height > 0 && image_area_height > target_height) {
        gint vpad = (image_area_height - target_height) / 2;
        if (vpad > 0) {
            image_render_top_row += vpad;
        }
    }

    APP_SINGLE_RENDER_CALL(ui_begin_sync_update, ui_begin_sync_update);
    APP_SINGLE_RENDER_CALL(ui_clear_kitty_images, ui_clear_kitty_images, app);
    // Clear screen and reset terminal state
    if (app && app->suppress_full_clear) {
        app->suppress_full_clear = FALSE;
        printf("\033[H\033[0m");
        if (app->ui_text_hidden) {
            APP_SINGLE_RENDER_CALL(ui_clear_single_view_lines, ui_clear_single_view_lines, app);
        }
        APP_SINGLE_RENDER_CALL(ui_clear_area, ui_clear_area, app, image_area_top_row, image_area_height);
    } else {
        APP_SINGLE_RENDER_CALL(ui_clear_screen_for_refresh, ui_clear_screen_for_refresh, app);
    }
    if (app->gif_player) {
        gif_player_set_render_area(app->gif_player,
                                   app->term_width,
                                   app->term_height,
                                   image_area_top_row,
                                   target_height,
                                   target_width,
                                   target_height);
    }
    if (app->video_player) {
        video_player_set_render_area(app->video_player,
                                     app->term_width,
                                     app->term_height,
                                     image_render_top_row,
                                     target_height,
                                     target_width,
                                     target_height);
        app->video_player->show_stats = app->show_fps && !app->ui_text_hidden;
    }
    if (!app->ui_text_hidden && app->term_height > 0) {
        const char *title = is_video ? "Video View" : "Image View";
        gint title_len = strlen(title);
        gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
        printf("\033[1;1H\033[2K");
        for (gint i = 0; i < title_pad; i++) putchar(' ');
        printf("%s", title);

        // Row 2: spacer
        printf("\033[2;1H\033[2K");

        // Row 3: Index indicator centered (numbers only)
        gint current = app_get_current_index(app) + 1;
        gint total = app_get_total_images(app);
        if (current < 1) current = 1;
        if (total < 1) total = 1;
        char idx_text[32];
        g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
        gint idx_len = (gint)strlen(idx_text);
        gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
        printf("\033[3;1H\033[2K");
        for (gint i = 0; i < idx_pad; i++) putchar(' ');
        printf("%s", idx_text);
    }

    if (is_video) {
        gint effective_width = target_width > 0 ? target_width : app->term_width;
        if (effective_width > app->term_width) {
            effective_width = app->term_width;
        }
        if (effective_width < 0) {
            effective_width = 0;
        }
        gint left_pad = (app->term_width > effective_width) ? (app->term_width - effective_width) / 2 : 0;
        if (left_pad < 0) left_pad = 0;

        if (filepath && !app->ui_text_hidden) {
            gchar *basename = g_path_get_basename(filepath);
            gchar *safe_basename = sanitize_for_terminal(basename);
            if (safe_basename) {
                gint max_width = ui_filename_max_width(app);
                if (max_width <= 0) {
                    max_width = app->term_width;
                }
                gchar *display_name = truncate_utf8_middle_keep_suffix(safe_basename, max_width);
                gint filename_len = utf8_display_width(display_name);
                gint image_center_col = effective_width / 2;
                gint filename_start_col = left_pad + image_center_col - filename_len / 2;
                if (filename_start_col < 0) filename_start_col = 0;
                if (filename_start_col + filename_len > app->term_width) {
                    filename_start_col = app->term_width - filename_len;
                }
                if (filename_start_col < 0) filename_start_col = 0;
                gint filename_row = (app->term_height >= 3) ? (app->term_height - 2) : 1;
                printf("\033[%d;1H\033[2K", filename_row);
                printf("\033[%d;%dH", filename_row, filename_start_col + 1);
                printf("\033[34m%s\033[0m", display_name);
                g_free(display_name);
                g_free(safe_basename);
                g_free(basename);
            }
        }

        if (app->term_height > 0 && !app->ui_text_hidden) {
            const HelpSegment segments[] = {
                {"←/→", "Prev/Next"},
                {"Space", "Pause/Play"},
                {"F", "FPS"},
                {"P", "Protocol"},
                {"+/-", "Scale"},
                {"Enter", "Preview"},
                {"TAB", "Toggle"},
                {"r", "Delete"},
                {"~", "Zen"},
                {"ESC", "Exit"}
            };
            printf("\033[%d;1H\033[2K", app->term_height);
            ui_print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
        }

        if (app->video_player) {
            APP_SINGLE_RENDER_CALL(video_player_play, video_player_play, app->video_player);
            app->needs_redraw = FALSE;
        } else {
            APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
            return ERROR_INVALID_IMAGE;
        }

        APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
        fflush(stdout);
        return ERROR_NONE;
    }

    // Check if image is already cached
    GString *rendered = NULL;
    gint image_width = 0;
    gint image_height = 0;

    if (use_zoom) {
        GError *load_error = NULL;
        GdkPixbuf *pixbuf = pixbuf_utils_load_from_stream(filepath, &load_error);
        if (!pixbuf) {
            if (load_error) {
                g_error_free(load_error);
            }
            ui_end_sync_update();
            return ERROR_INVALID_IMAGE;
        }

        gint orig_w = gdk_pixbuf_get_width(pixbuf);
        gint orig_h = gdk_pixbuf_get_height(pixbuf);
        if (orig_w < 1) orig_w = 1;
        if (orig_h < 1) orig_h = 1;

        gdouble scale_w = (gdouble)app->image_viewport_px_w / (gdouble)orig_w;
        gdouble scale_h = (gdouble)app->image_viewport_px_h / (gdouble)orig_h;
        gdouble base_scale = scale_w < scale_h ? scale_w : scale_h;
        if (!isfinite(base_scale) || base_scale <= 0.0) {
            base_scale = 1.0;
        }
        gdouble desired_scale = base_scale * image_zoom;
        gdouble scaled_w = (gdouble)orig_w * desired_scale;
        gdouble scaled_h = (gdouble)orig_h * desired_scale;
        const gdouble max_dim = 4096.0;
        if (scaled_w > max_dim || scaled_h > max_dim) {
            gdouble descale = scaled_w / max_dim;
            gdouble descale_h = scaled_h / max_dim;
            if (descale_h > descale) {
                descale = descale_h;
            }
            if (descale > 1.0) {
                desired_scale /= descale;
                scaled_w = (gdouble)orig_w * desired_scale;
                scaled_h = (gdouble)orig_h * desired_scale;
            }
        }

        gint scaled_px_w = (gint)ceil(scaled_w);
        gint scaled_px_h = (gint)ceil(scaled_h);
        if (scaled_px_w < 1) scaled_px_w = 1;
        if (scaled_px_h < 1) scaled_px_h = 1;

        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, scaled_px_w, scaled_px_h, GDK_INTERP_BILINEAR);
        g_object_unref(pixbuf);
        if (!scaled) {
            ui_end_sync_update();
            return ERROR_MEMORY_ALLOC;
        }

        gint crop_w = app->image_viewport_px_w;
        gint crop_h = app->image_viewport_px_h;
        if (crop_w < 1) crop_w = 1;
        if (crop_h < 1) crop_h = 1;
        if (crop_w > scaled_px_w) crop_w = scaled_px_w;
        if (crop_h > scaled_px_h) crop_h = scaled_px_h;

        gint max_pan_x = MAX(0, scaled_px_w - crop_w);
        gint max_pan_y = MAX(0, scaled_px_h - crop_h);
        if (app->image_pan_x < 0.0) app->image_pan_x = 0.0;
        if (app->image_pan_y < 0.0) app->image_pan_y = 0.0;
        if (app->image_pan_x > max_pan_x) app->image_pan_x = max_pan_x;
        if (app->image_pan_y > max_pan_y) app->image_pan_y = max_pan_y;

        gint crop_x = (gint)(app->image_pan_x + 0.5);
        gint crop_y = (gint)(app->image_pan_y + 0.5);
        if (crop_x < 0) crop_x = 0;
        if (crop_y < 0) crop_y = 0;
        if (crop_x > max_pan_x) crop_x = max_pan_x;
        if (crop_y > max_pan_y) crop_y = max_pan_y;

        GdkPixbuf *render_pixbuf = scaled;
        if (crop_w < scaled_px_w || crop_h < scaled_px_h) {
            render_pixbuf = gdk_pixbuf_new_subpixbuf(scaled, crop_x, crop_y, crop_w, crop_h);
            if (!render_pixbuf) {
                g_object_unref(scaled);
                ui_end_sync_update();
                return ERROR_MEMORY_ALLOC;
            }
        } else {
            g_object_ref(render_pixbuf);
        }

        ImageRenderer *renderer = APP_SINGLE_RENDER_CALL(renderer_create, renderer_create);
        if (!renderer) {
            g_object_unref(render_pixbuf);
            g_object_unref(scaled);
            APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
            return ERROR_MEMORY_ALLOC;
        }

        RendererConfig config = {
            .max_width = target_width,
            .max_height = target_height,
            .preserve_aspect_ratio = TRUE,
            .dither = app->dither_enabled,
            .color_space = CHAFA_COLOR_SPACE_RGB,
            .work_factor = app->render_work_factor,
            .force_text = app->force_text,
            .force_sixel = app->force_sixel,
            .force_kitty = app->force_kitty,
            .force_iterm2 = app->force_iterm2,
            .gamma = app->gamma,
            .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
            .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
            .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
        };

        ErrorCode error = APP_SINGLE_RENDER_CALL(renderer_initialize,
                                                 renderer_initialize,
                                                 renderer,
                                                 &config);
        if (error != ERROR_NONE) {
            APP_SINGLE_RENDER_CALL(renderer_destroy, renderer_destroy, renderer);
            g_object_unref(render_pixbuf);
            g_object_unref(scaled);
            APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
            return error;
        }

        rendered = renderer_render_image_data(renderer,
                                              gdk_pixbuf_get_pixels(render_pixbuf),
                                              gdk_pixbuf_get_width(render_pixbuf),
                                              gdk_pixbuf_get_height(render_pixbuf),
                                              gdk_pixbuf_get_rowstride(render_pixbuf),
                                              gdk_pixbuf_get_n_channels(render_pixbuf));
        APP_SINGLE_RENDER_CALL(renderer_get_rendered_dimensions,
                               renderer_get_rendered_dimensions,
                               renderer,
                               &image_width,
                               &image_height);

        APP_SINGLE_RENDER_CALL(renderer_destroy, renderer_destroy, renderer);
        g_object_unref(render_pixbuf);
        g_object_unref(scaled);

        if (!rendered) {
            APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
            return ERROR_INVALID_IMAGE;
        }
    } else {
        if (app->preloader && app->preload_enabled) {
            rendered = preloader_get_cached_image(app->preloader, filepath, target_width, target_height);
        }

        // If not in cache, render it normally
        if (!rendered) {
            // Create renderer
            ImageRenderer *renderer = APP_SINGLE_RENDER_CALL(renderer_create, renderer_create);
            if (!renderer) {
                APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
                return ERROR_MEMORY_ALLOC;
            }

            // Configure renderer
            RendererConfig config = {
                .max_width = target_width,
                .max_height = target_height, // Normal: use almost full height, Info: reserve space
                .preserve_aspect_ratio = TRUE,
                .dither = app->dither_enabled,
                .color_space = CHAFA_COLOR_SPACE_RGB,
                .work_factor = app->render_work_factor,
                .force_text = app->force_text,
                .force_sixel = app->force_sixel,
                .force_kitty = app->force_kitty,
                .force_iterm2 = app->force_iterm2,
                .gamma = app->gamma,
                .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
                .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
                .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
            };

            ErrorCode error = APP_SINGLE_RENDER_CALL(renderer_initialize,
                                                     renderer_initialize,
                                                     renderer,
                                                     &config);
            if (error != ERROR_NONE) {
                APP_SINGLE_RENDER_CALL(renderer_destroy, renderer_destroy, renderer);
                APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
                return error;
            }

            // Render image
            rendered = APP_SINGLE_RENDER_CALL(renderer_render_image_file,
                                              renderer_render_image_file,
                                              renderer,
                                              filepath);
            if (!rendered) {
                APP_SINGLE_RENDER_CALL(renderer_destroy, renderer_destroy, renderer);
                APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
                return ERROR_INVALID_IMAGE;
            }

            // Get rendered image dimensions
            APP_SINGLE_RENDER_CALL(renderer_get_rendered_dimensions,
                                   renderer_get_rendered_dimensions,
                                   renderer,
                                   &image_width,
                                   &image_height);

            // Add to cache if preloader is available
            if (app->preloader && app->preload_enabled) {
                preloader_cache_add(app->preloader,
                                    filepath,
                                    rendered,
                                    image_width,
                                    image_height,
                                    renderer_is_graphics_mode(renderer),
                                    target_width,
                                    target_height);
            }

            APP_SINGLE_RENDER_CALL(renderer_destroy, renderer_destroy, renderer);
        } else {
            // For cached images, get the actual dimensions from cache
            if (!preloader_get_cached_image_dimensions(app->preloader, filepath, target_width, target_height, &image_width, &image_height)) {
                // Fallback: count lines in the rendered output to get actual height
                image_width = app->term_width;
                image_height = 1; // Start with 1 for first line

                for (gsize i = 0; i < rendered->len; i++) {
                    if (rendered->str[i] == '\n') {
                        image_height++;
                    }
                }
            }
        }
    }

    // Determine effective width for centering
    gint effective_width = image_width > 0 ? image_width : target_width;
    if (effective_width > app->term_width) {
        effective_width = app->term_width;
    }
    if (effective_width < 0) {
        effective_width = 0;
    }
    gint left_pad = (app->term_width > effective_width) ? (app->term_width - effective_width) / 2 : 0;
    if (left_pad < 0) left_pad = 0;

    // Vertically center the rendered image inside the image area
    gint image_top_row = image_area_top_row;
    if (target_height > 0 && image_height > 0 && image_height < target_height) {
        gint vpad = (target_height - image_height) / 2;
        if (vpad < 0) vpad = 0;
        image_top_row = image_area_top_row + vpad;
    }
    if (app) {
        gint stored_height = image_height > 0 ? image_height : (target_height > 0 ? target_height : 1);
        app->last_render_top_row = image_top_row;
        app->last_render_height = stored_height;
        app->image_view_left_col = left_pad + 1;
        app->image_view_top_row = image_top_row;
        app->image_view_width = image_width > 0 ? image_width : effective_width;
        app->image_view_height = stored_height;
    }

    gchar *pad_buffer = NULL;
    if (left_pad > 0) {
        pad_buffer = g_malloc(left_pad);
        memset(pad_buffer, ' ', left_pad);
    }

    const gchar *line_ptr = rendered->str;
    gint row = image_top_row;
    while (line_ptr && *line_ptr) {
        const gchar *newline = strchr(line_ptr, '\n');
        gint line_len = newline ? (gint)(newline - line_ptr) : (gint)strlen(line_ptr);
        printf("\033[%d;1H", row);
        if (left_pad > 0) {
            fwrite(pad_buffer, 1, left_pad, stdout);
        }
        if (line_len > 0) {
            fwrite(line_ptr, 1, line_len, stdout);
        }
        if (!newline) {
            break;
        }
        line_ptr = newline + 1;
        row++;
    }
    g_free(pad_buffer);

    // Calculate filename position relative to image center
    if (filepath && !app->ui_text_hidden) {
        gchar *basename = g_path_get_basename(filepath);
        gchar *safe_basename = sanitize_for_terminal(basename);
        if (safe_basename) {
            gint max_width = ui_filename_max_width(app);
            if (max_width <= 0) {
                max_width = app->term_width;
            }
            gchar *display_name = truncate_utf8_middle_keep_suffix(safe_basename, max_width);
            gint filename_len = utf8_display_width(display_name);
            // Center filename relative to image width, but ensure it stays within terminal bounds
            gint image_center_col = effective_width / 2;
            gint filename_start_col = left_pad + image_center_col - filename_len / 2;

            // Ensure filename doesn't go beyond terminal bounds
            if (filename_start_col < 0) filename_start_col = 0;
            if (filename_start_col + filename_len > app->term_width) {
                filename_start_col = app->term_width - filename_len;
            }
            if (filename_start_col < 0) filename_start_col = 0;

            // Keep filename on the third-to-last line to keep it outside the image area
            gint filename_row = (app->term_height >= 3) ? (app->term_height - 2) : 1;
            printf("\033[%d;1H\033[2K", filename_row);
            printf("\033[%d;%dH", filename_row, filename_start_col + 1);
            printf("\033[34m%s\033[0m", display_name); // Blue filename with reset
            g_free(display_name);
            g_free(safe_basename);
            g_free(basename);
        }
    }

    // Footer hints (single image view) centered on last line
    if (app->term_height > 0 && !app->ui_text_hidden) {
        const HelpSegment segments[] = {
            {"←/→", "Prev/Next"},
            {"Enter", "Preview"},
            {"TAB", "Toggle"},
            {"i", "Info"},
            {"r", "Delete"},
            {"~", "Zen"},
            {"ESC", "Exit"}
        };
        printf("\033[%d;1H\033[2K", app->term_height);
        ui_print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
    }

    // If it's an animated image and player is available, start playing if animated
    if (gif_is_animated && app->gif_player) {
        // For first render, just show the first frame, then start animation
        APP_SINGLE_RENDER_CALL(gif_player_play, gif_player_play, app->gif_player);
        // Indicate that we are currently displaying an animated GIF
        app->needs_redraw = FALSE; // Don't immediately redraw since animation will handle updates
    }

    APP_SINGLE_RENDER_CALL(ui_end_sync_update, ui_end_sync_update);
    fflush(stdout);

    g_string_free(rendered, TRUE);

    return ERROR_NONE;
}
