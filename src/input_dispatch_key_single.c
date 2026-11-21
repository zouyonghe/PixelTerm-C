#include "input_dispatch_key_modes_internal.h"

#include "input_dispatch_media_internal.h"
#include "ui_render_utils.h"
#include "app_preview_transaction_internal.h"
#include "terminal_output.h"

static ErrorCode record_output_error(PixelTermApp *app, ErrorCode result) {
    if (result == ERROR_OUTPUT_IO) app->last_error = result;
    return result;
}

static const gint64 k_protocol_toggle_debounce_us = 150000;
static gint64 g_last_protocol_toggle_us = 0;
static const gdouble k_video_scale_step = 0.1;
static const gint64 k_video_seek_step_ms = 5000;
static InputDispatchVideoSeekFunc g_video_seek_func = video_player_seek_relative_ms;
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

static void handle_single_media_navigation(PixelTermApp *app,
                                           InputHandler *input_handler,
                                           ErrorCode (*navigate)(PixelTermApp *app),
                                           const KeyCode *keys,
                                           size_t key_count) {
    if (!app || !navigate) {
        return;
    }

    gint old_index = app_get_current_index(app);
    ErrorCode result = navigate(app);
    record_output_error(app, result);
    if (result != ERROR_NONE) {
        return;
    }
    if (old_index != app_get_current_index(app)) {
        app->suppress_full_clear = TRUE;
        app->async.render_request = TRUE;
        if (record_output_error(app, app_refresh_display(app)) != ERROR_NONE) return;
    }
    skip_queued_navigation(input_handler, keys, key_count);
}

static gboolean handle_video_seek(PixelTermApp *app, gint64 delta_ms, ErrorCode *result);

static void drain_queued_video_seek_repeats(PixelTermApp *app,
                                            InputHandler *input_handler,
                                            KeyCode seek_key,
                                            gint64 delta_ms) {
    if (!app || !input_handler || delta_ms == 0) {
        return;
    }

    InputEvent queued_event = {0};
    while (input_has_pending_input(input_handler)) {
        if (input_get_event(input_handler, &queued_event) != ERROR_NONE) {
            break;
        }
        if (queued_event.type != INPUT_KEY_PRESS || queued_event.key_code != seek_key) {
            input_unget_event(input_handler, &queued_event);
            break;
        }
        ErrorCode result;
        if (!handle_video_seek(app, delta_ms, &result)) {
            input_unget_event(input_handler, &queued_event);
            break;
        }
        /* Seek can commit before output fails; replaying this event would seek twice. */
        if (result == ERROR_OUTPUT_IO) break;
    }
}

void input_dispatch_key_single_set_video_seek_for_test(InputDispatchVideoSeekFunc func) {
    g_video_seek_func = func ? func : video_player_seek_relative_ms;
}

gint64 input_dispatch_key_single_get_video_seek_step_ms_for_test(void) {
    return k_video_seek_step_ms;
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
        record_output_error(app, video_player_play(app->video_player));
    }
}

void input_dispatch_key_modes_toggle_video_fps(PixelTermApp *app) {
    if (!app || !input_dispatch_current_is_video(app) || !app->video_player) {
        return;
    }
    app->show_fps = !app->show_fps;
    video_player_set_show_stats(app->video_player,
                                app->show_fps && !app->ui_text_hidden);
    if (!app->show_fps && !app->ui_text_hidden) {
        gint stats_row = VIDEO_PLAYER_STATS_ROW;
        if (stats_row >= 1 && stats_row <= app->term_height) {
            gchar *line = video_player_dup_cached_line_at_row(app->video_player,
                                                              stats_row);
            GString *output = g_string_new(NULL);
            g_string_append_printf(output, "\033[%d;1H\033[2K", stats_row);
            if (line) {
                g_string_append(output, line);
            }
            g_free(line);
            if (!terminal_output_write(output->str, output->len)) app->last_error = ERROR_OUTPUT_IO;
            g_string_free(output, TRUE);
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
    gboolean was_playing = app->video_player && video_player_is_playing(app->video_player);
    if (app->video_player) {
        if (record_output_error(app, video_player_stop(app->video_player)) != ERROR_NONE) return;
    }
    app->video_scale = next_scale;
    if (record_output_error(app, app_render_current_image(app)) != ERROR_NONE) return;
    if (was_playing && app->video_player) {
        record_output_error(app, video_player_play(app->video_player));
    }
}

static void handle_video_protocol_toggle(PixelTermApp *app) {
    if (!app || !input_dispatch_current_is_video(app) || !app->video_player) {
        return;
    }

    gboolean was_playing = video_player_is_playing(app->video_player);
    VideoPlayer *player = app->video_player;
    gboolean redraw = video_player_protocol_cycle_needs_redraw(player);
    if (was_playing) {
        if (record_output_error(app, video_player_stop(app->video_player)) != ERROR_NONE) {
            return;
        }
    }
    if ((was_playing || redraw) && app->force_kitty && !ui_clear_kitty_images(app)) {
        app->last_error = ERROR_OUTPUT_IO;
        return;
    }
    if ((was_playing || redraw) &&
        record_output_error(app, video_player_clear_render_area(player)) != ERROR_NONE) return;
    if (record_output_error(app, video_player_cycle_protocol_checked(player, &redraw)) != ERROR_NONE)
        return;

    if (was_playing) {
        record_output_error(app, video_player_play(app->video_player));
    } else if (redraw) {
        record_output_error(app, video_player_refresh_paused_frame(player));
    }
}

static gboolean can_handle_video_seek(const PixelTermApp *app) {
    return app && input_dispatch_current_is_video(app) && app->video_player &&
           video_player_has_video(app->video_player);
}

static gboolean handle_video_seek(PixelTermApp *app, gint64 delta_ms, ErrorCode *result) {
    if (!can_handle_video_seek(app)) {
        return FALSE;
    }
    if (!g_video_seek_func) return FALSE;

    *result = record_output_error(app, g_video_seek_func(app->video_player, delta_ms));
    if (*result == ERROR_NONE) {
        app->video_progress_dirty = TRUE;
    }
    return *result == ERROR_NONE || *result == ERROR_OUTPUT_IO;
}

void input_dispatch_scroll_video_seek(PixelTermApp *app, gint direction) {
    ErrorCode result;
    handle_video_seek(app, direction > 0 ? k_video_seek_step_ms : -k_video_seek_step_ms, &result);
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
        case KEY_LEFT: {
            ErrorCode result;
            if (handle_video_seek(app, -k_video_seek_step_ms, &result)) {
                if (result != ERROR_OUTPUT_IO)
                    drain_queued_video_seek_repeats(app, input_handler, KEY_LEFT, -k_video_seek_step_ms);
                break;
            }
            handle_single_media_navigation(app,
                                           input_handler,
                                           app_previous_image,
                                           g_nav_keys_lr,
                                           G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'h': {
            handle_single_media_navigation(app,
                                           input_handler,
                                           app_previous_image,
                                           g_nav_keys_lr,
                                           G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case KEY_RIGHT: {
            ErrorCode result;
            if (handle_video_seek(app, k_video_seek_step_ms, &result)) {
                if (result != ERROR_OUTPUT_IO)
                    drain_queued_video_seek_repeats(app, input_handler, KEY_RIGHT, k_video_seek_step_ms);
                break;
            }
            handle_single_media_navigation(app,
                                           input_handler,
                                           app_next_image,
                                           g_nav_keys_lr,
                                           G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'l': {
            handle_single_media_navigation(app,
                                           input_handler,
                                           app_next_image,
                                           g_nav_keys_lr,
                                           G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'k':
        case KEY_UP: {
            handle_single_media_navigation(app,
                                           input_handler,
                                           app_previous_image,
                                           g_nav_keys_ud,
                                           G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case (KeyCode)'j':
        case KEY_DOWN: {
            handle_single_media_navigation(app,
                                           input_handler,
                                           app_next_image,
                                           g_nav_keys_ud,
                                           G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_TAB:
            app->return_to_mode = RETURN_MODE_SINGLE;
            if (record_output_error(app, app_enter_file_manager(app)) == ERROR_NONE)
                record_output_error(app, app_render_file_manager(app));
            break;
        case KEY_ENTER:
        case 13:
            record_output_error(app, app_enter_preview_and_render(app));
            break;
        default:
            break;
    }
}
