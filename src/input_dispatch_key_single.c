#include "input_dispatch_key_modes_internal.h"

#include <stdio.h>
#include <string.h>

#include "input_dispatch_media_internal.h"

static const gint64 k_protocol_toggle_debounce_us = 150000;
static gint64 g_last_protocol_toggle_us = 0;
static const gdouble k_video_scale_step = 0.1;
static const KeyCode g_nav_keys_lr[] = {
    KEY_LEFT, (KeyCode)'h', KEY_UP, KEY_DOWN, KEY_RIGHT, (KeyCode)'l', KEY_PAGE_UP, KEY_PAGE_DOWN
};
static const KeyCode g_nav_keys_ud[] = {
    KEY_UP, (KeyCode)'k', KEY_LEFT, (KeyCode)'h', KEY_RIGHT, (KeyCode)'l', KEY_DOWN, (KeyCode)'j',
    KEY_PAGE_UP, KEY_PAGE_DOWN
};

static gboolean key_in_list(KeyCode key, const KeyCode *keys, size_t key_count) {
    for (size_t i = 0; i < key_count; i++) {
        if (keys[i] == key) {
            return TRUE;
        }
    }
    return FALSE;
}

static void skip_queued_navigation(InputHandler *input_handler,
                                   const KeyCode *keys,
                                   size_t key_count) {
    InputEvent skip_event;
    while (input_has_pending_input(input_handler)) {
        ErrorCode skip_error = input_get_event(input_handler, &skip_event);
        if (skip_error != ERROR_NONE) {
            break;
        }
        if (skip_event.type != INPUT_KEY_PRESS ||
            !key_in_list(skip_event.key_code, keys, key_count)) {
            input_unget_event(input_handler, &skip_event);
            break;
        }
    }
}

void input_dispatch_key_modes_toggle_video_playback(PixelTermApp *app) {
    if (!app || !app->video_player) {
        return;
    }
    if (!input_dispatch_current_is_video(app)) {
        return;
    }
    if (video_player_is_playing(app->video_player)) {
        video_player_pause(app->video_player);
    } else if (video_player_has_video(app->video_player)) {
        video_player_play(app->video_player);
    }
}

void input_dispatch_key_modes_toggle_video_fps(PixelTermApp *app) {
    if (!app || !input_dispatch_current_is_video(app) || !app->video_player) {
        return;
    }
    app->show_fps = !app->show_fps;
    app->video_player->show_stats = app->show_fps && !app->ui_text_hidden;
    if (!app->show_fps && !app->ui_text_hidden) {
        gint stats_row = 4;
        if (stats_row >= 1 && stats_row <= app->term_height) {
            gboolean restored_line = FALSE;
            VideoPlayer *player = app->video_player;
            if (player->last_frame_lines && player->last_frame_height > 0) {
                gint line_index = stats_row - player->last_frame_top_row;
                if (line_index >= 0 && line_index < (gint)player->last_frame_lines->len) {
                    const gchar *line = g_ptr_array_index(player->last_frame_lines, line_index);
                    printf("\033[%d;1H\033[2K", stats_row);
                    if (line) {
                        fwrite(line, 1, strlen(line), stdout);
                    }
                    restored_line = TRUE;
                }
            }
            if (!restored_line) {
                printf("\033[%d;1H\033[2K", stats_row);
            }
            fflush(stdout);
        }
    }
}

static void handle_video_scale_change(PixelTermApp *app, gdouble delta) {
    if (!app || !input_dispatch_current_is_video(app)) {
        return;
    }
    gdouble next_scale = app->video_scale + delta;
    if (next_scale < 0.3) {
        next_scale = 0.3;
    } else if (next_scale > 1.5) {
        next_scale = 1.5;
    }
    if (delta > 0.0) {
        gint base_w = app->term_width > 0 ? app->term_width : 80;
        gint base_h = app->term_height > 0 ? app->term_height : 24;
        if (app->info_visible) {
            base_h -= 10;
        } else {
            base_h -= 6;
        }
        if (base_h < 1) {
            base_h = 1;
        }
        gint scaled_w = (gint)(base_w * next_scale + 0.5);
        gint scaled_h = (gint)(base_h * next_scale + 0.5);
        if (scaled_w > base_w || scaled_h > base_h) {
            next_scale = app->video_scale;
        }
    }
    if (next_scale == app->video_scale) {
        return;
    }
    app->video_scale = next_scale;
    if (app->video_player) {
        video_player_stop(app->video_player);
    }
    app_render_current_image(app);
    if (app->video_player) {
        video_player_play(app->video_player);
    }
}

static void handle_video_protocol_toggle(PixelTermApp *app) {
    if (!app || !input_dispatch_current_is_video(app) || !app->video_player || !app->video_player->renderer) {
        return;
    }

    gboolean was_playing = video_player_is_playing(app->video_player);
    if (was_playing) {
        video_player_stop(app->video_player);
    }

    g_mutex_lock(&app->video_player->render_mutex);
    gboolean force_text = app->video_player->renderer->config.force_text;
    gboolean force_kitty = app->video_player->renderer->config.force_kitty;
    gboolean force_iterm2 = app->video_player->renderer->config.force_iterm2;
    gboolean force_sixel = app->video_player->renderer->config.force_sixel;
    ChafaPixelMode current_mode = CHAFA_PIXEL_MODE_SYMBOLS;
    if (app->video_player->renderer->canvas_config) {
        current_mode = chafa_canvas_config_get_pixel_mode(app->video_player->renderer->canvas_config);
    }
    gboolean was_text = force_text || current_mode == CHAFA_PIXEL_MODE_SYMBOLS;
    gboolean next_text = FALSE;
    gboolean next_kitty = FALSE;
    gboolean next_iterm2 = FALSE;
    gboolean next_sixel = FALSE;

    if (force_text) {
        next_sixel = TRUE;
    } else if (force_sixel) {
        next_iterm2 = TRUE;
    } else if (force_iterm2) {
        next_kitty = TRUE;
    } else if (force_kitty) {
        next_text = TRUE;
    } else {
        next_sixel = TRUE;
    }

    app->video_player->renderer->config.force_text = next_text;
    app->video_player->renderer->config.force_kitty = next_kitty;
    app->video_player->renderer->config.force_iterm2 = next_iterm2;
    app->video_player->renderer->config.force_sixel = next_sixel;
    gboolean next_graphics = next_kitty || next_iterm2 || next_sixel;
    gboolean should_clear = was_text && next_graphics;
    renderer_update_terminal_size(app->video_player->renderer);
    g_mutex_unlock(&app->video_player->render_mutex);

    if (should_clear) {
        video_player_clear_render_area(app->video_player);
    }

    if (was_playing) {
        video_player_play(app->video_player);
    }
}

void input_dispatch_handle_key_press_single(PixelTermApp *app,
                                            InputHandler *input_handler,
                                            const InputEvent *event) {
    switch (event->key_code) {
        case (KeyCode)' ':
            input_dispatch_key_modes_toggle_video_playback(app);
            break;
        case (KeyCode)'+':
        case (KeyCode)'=':
            if (input_dispatch_current_is_video(app)) {
                handle_video_scale_change(app, k_video_scale_step);
            }
            break;
        case (KeyCode)'-':
            if (input_dispatch_current_is_video(app)) {
                handle_video_scale_change(app, -k_video_scale_step);
            }
            break;
        case (KeyCode)'p':
        case (KeyCode)'P':
            {
                gint64 now_us = g_get_monotonic_time();
                if (g_last_protocol_toggle_us > 0 &&
                    (now_us - g_last_protocol_toggle_us) < k_protocol_toggle_debounce_us) {
                    break;
                }
                g_last_protocol_toggle_us = now_us;
                handle_video_protocol_toggle(app);
            }
            break;
        case KEY_LEFT:
        case (KeyCode)'h': {
            gint old_index = app_get_current_index(app);
            app_previous_image(app);
            if (old_index != app_get_current_index(app)) {
                app->suppress_full_clear = TRUE;
                app->async.render_request = TRUE;
                app_refresh_display(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case KEY_RIGHT:
        case (KeyCode)'l': {
            gint old_index = app_get_current_index(app);
            app_next_image(app);
            if (old_index != app_get_current_index(app)) {
                app->suppress_full_clear = TRUE;
                app->async.render_request = TRUE;
                app_refresh_display(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'k':
        case KEY_UP: {
            gint old_index = app_get_current_index(app);
            app_previous_image(app);
            if (old_index != app_get_current_index(app)) {
                app->suppress_full_clear = TRUE;
                app->async.render_request = TRUE;
                app_refresh_display(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case (KeyCode)'j':
        case KEY_DOWN: {
            gint old_index = app_get_current_index(app);
            app_next_image(app);
            if (old_index != app_get_current_index(app)) {
                app->suppress_full_clear = TRUE;
                app->async.render_request = TRUE;
                app_refresh_display(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_TAB:
            app->return_to_mode = RETURN_MODE_SINGLE;
            app_enter_file_manager(app);
            app_render_file_manager(app);
            break;
        case KEY_ENTER:
        case 13:
            if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
                app->return_to_mode = RETURN_MODE_PREVIEW;
            }
            if (app_enter_preview(app) == ERROR_NONE) {
                app_render_preview_grid(app);
            }
            break;
        default:
            break;
    }
}
