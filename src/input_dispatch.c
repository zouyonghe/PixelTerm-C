#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <glib.h>

#include "input_dispatch.h"
#include "common.h"
#include "media_utils.h"
#include "text_utils.h"
#include "video_player.h"

static const gint64 k_click_threshold_us = 400000;
static const gint64 k_protocol_toggle_debounce_us = 150000;
static gint64 g_last_protocol_toggle_us = 0;
static const gdouble k_image_zoom_step = 0.2;
static const gdouble k_video_scale_step = 0.1;
static const gint k_book_jump_max_digits = 12;
static const KeyCode g_nav_keys_lr[] = {
    KEY_LEFT, (KeyCode)'h', KEY_UP, KEY_DOWN, KEY_RIGHT, (KeyCode)'l', KEY_PAGE_UP, KEY_PAGE_DOWN
};
static const KeyCode g_nav_keys_ud[] = {
    KEY_UP, (KeyCode)'k', KEY_LEFT, (KeyCode)'h', KEY_RIGHT, (KeyCode)'l', KEY_DOWN, (KeyCode)'j',
    KEY_PAGE_UP, KEY_PAGE_DOWN
};
static const KeyCode g_nav_keys_page[] = {
    KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, (KeyCode)'a'
};

static void handle_delete_current_image(PixelTermApp *app);
static void handle_key_press_book_toc(PixelTermApp *app,
                                      InputHandler *input_handler,
                                      const InputEvent *event);
static void handle_mouse_press_book_toc(PixelTermApp *app, const InputEvent *event);
static void handle_mouse_double_click_book_toc(PixelTermApp *app, const InputEvent *event);

static gboolean app_current_is_video(const PixelTermApp *app) {
    if (!app_is_single_mode(app)) {
        return FALSE;
    }
    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return FALSE;
    }
    MediaKind media_kind = media_classify(filepath);
    return media_is_video(media_kind);
}

static gboolean app_current_is_animated_image(const PixelTermApp *app) {
    if (!app_is_single_mode(app)) {
        return FALSE;
    }
    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return FALSE;
    }
    MediaKind media_kind = media_classify(filepath);
    if (!media_is_animated_image(media_kind)) {
        return FALSE;
    }
    if (app->gif_player && app->gif_player->filepath &&
        g_strcmp0(app->gif_player->filepath, filepath) == 0) {
        return gif_player_is_animated(app->gif_player);
    }
    return FALSE;
}

static gint delete_prompt_display_width(const char *text) {
    return utf8_display_width(text);
}

static gint delete_prompt_row(const PixelTermApp *app) {
    if (!app) {
        return 1;
    }

    gint term_height = app->term_height > 0 ? app->term_height : 24;
    gint row = term_height - 1;

    if (app_is_single_mode(app)) {
        if (app_current_is_video(app) && app->video_player && app->video_player->last_frame_height > 0) {
            row = app->video_player->last_frame_top_row + app->video_player->last_frame_height;
        } else if (app->last_render_height > 0 && app->last_render_top_row > 0) {
            row = app->last_render_top_row + app->last_render_height;
        }
    }

    if (row < 1) {
        row = 1;
    } else if (row > term_height - 1) {
        row = term_height - 1;
    }

    return row;
}

static void app_show_delete_prompt(PixelTermApp *app) {
    if (!app) {
        return;
    }
    const char *message = "Press r again to delete";
    gint term_width = app->term_width > 0 ? app->term_width : 80;
    gint row = delete_prompt_row(app);

    gint message_len = delete_prompt_display_width(message);
    gint col = term_width > message_len ? (term_width - message_len) / 2 + 1 : 1;

    printf("\033[%d;1H\033[2K", row);
    printf("\033[%d;%dH\033[31m%s\033[0m", row, col, message);
    fflush(stdout);
}

static void app_clear_delete_prompt(PixelTermApp *app) {
    if (!app) {
        return;
    }
    gint row = delete_prompt_row(app);
    printf("\033[%d;1H\033[2K", row);
    fflush(stdout);
}

static void handle_delete_current_in_preview(PixelTermApp *app) {
    if (app_has_images(app)) {
        app->current_index = app->preview.selected;
        app_delete_current_image(app);
    }

    if (app_has_images(app)) {
        if (app->current_index < 0) app->current_index = 0;
        if (app->current_index >= app->total_images) app->current_index = app->total_images - 1;
        app->preview.selected = app->current_index;
        app->needs_screen_clear = TRUE;
        app_render_preview_grid(app);
    } else {
        app_set_mode(app, APP_MODE_SINGLE);
        app->needs_screen_clear = TRUE;
        if (app_enter_file_manager(app) == ERROR_NONE) {
            app_render_file_manager(app);
        } else {
            app_refresh_display(app);
        }
    }
}

static gboolean handle_delete_request(PixelTermApp *app, const InputEvent *event) {
    if (!app || !event || event->type != INPUT_KEY_PRESS) {
        return FALSE;
    }

    if (app_is_file_manager_mode(app) || app_is_book_preview_mode(app) || app_is_book_mode(app)) {
        if (app->delete_pending) {
            app->delete_pending = FALSE;
            app_clear_delete_prompt(app);
        }
        return FALSE;
    }

    if (app->delete_pending) {
        app->delete_pending = FALSE;
        if (event->key_code == (KeyCode)'r') {
            if (app_is_preview_mode(app)) {
                handle_delete_current_in_preview(app);
            } else {
                handle_delete_current_image(app);
            }
            return TRUE;
        }
        app_clear_delete_prompt(app);
        return FALSE;
    }

    if (event->key_code == (KeyCode)'r') {
        app->delete_pending = TRUE;
        app_show_delete_prompt(app);
        return TRUE;
    }

    return FALSE;
}

static void app_pause_video_for_resize(PixelTermApp *app) {
    if (!app || !app->video_player) {
        return;
    }
    if (!app_current_is_video(app)) {
        return;
    }
    if (video_player_is_playing(app->video_player)) {
        video_player_pause(app->video_player);
    }
}

static void app_toggle_video_playback(PixelTermApp *app) {
    if (!app || !app->video_player) {
        return;
    }
    if (!app_current_is_video(app)) {
        return;
    }
    if (video_player_is_playing(app->video_player)) {
        video_player_pause(app->video_player);
    } else if (video_player_has_video(app->video_player)) {
        video_player_play(app->video_player);
    }
}

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
        // Skip this navigation event
    }
}

static void handle_mouse_press_preview(PixelTermApp *app, const InputEvent *event) {
    app->input.pending_grid_single_click = TRUE;
    app->input.pending_grid_click_time = g_get_monotonic_time();
    app->input.pending_grid_click_x = event->mouse_x;
    app->input.pending_grid_click_y = event->mouse_y;
}

static void handle_mouse_press_file_manager(PixelTermApp *app, const InputEvent *event) {
    app->input.pending_file_manager_single_click = TRUE;
    app->input.pending_file_manager_click_time = g_get_monotonic_time();
    app->input.pending_file_manager_click_x = event->mouse_x;
    app->input.pending_file_manager_click_y = event->mouse_y;
}

static void handle_mouse_press_book_toc(PixelTermApp *app, const InputEvent *event) {
    gboolean redraw_needed = FALSE;
    app_handle_mouse_click_book_toc(app,
                                    event->mouse_x,
                                    event->mouse_y,
                                    &redraw_needed,
                                    NULL);
    if (redraw_needed) {
        app_render_book_toc(app);
    }
}

static void handle_mouse_press_single(PixelTermApp *app, const InputEvent *event) {
    if (event->mouse_button == MOUSE_BUTTON_LEFT && app_current_is_video(app)) {
        app_toggle_video_playback(app);
        app->input.pending_single_click = FALSE;
        return;
    }
    app->input.pending_single_click = TRUE;
    app->input.pending_click_time = g_get_monotonic_time();
}

static void handle_mouse_press(PixelTermApp *app, const InputEvent *event) {
    if (app->book.toc_visible) {
        handle_mouse_press_book_toc(app, event);
        return;
    }
    if (app_is_book_preview_mode(app) || app_is_preview_mode(app)) {
        handle_mouse_press_preview(app, event);
    } else if (app_is_file_manager_mode(app)) {
        handle_mouse_press_file_manager(app, event);
    } else if (app_is_book_mode(app)) {
        app->input.pending_single_click = TRUE;
        app->input.pending_click_time = g_get_monotonic_time();
    } else {
        handle_mouse_press_single(app, event);
    }
}

static void handle_mouse_double_click_preview(PixelTermApp *app, const InputEvent *event) {
    app->input.pending_grid_single_click = FALSE;

    gboolean redraw_needed = FALSE;
    gboolean hit = FALSE;
    app_handle_mouse_click_preview(app, event->mouse_x, event->mouse_y, &redraw_needed, &hit);
    if (!hit) {
        return;
    }

    if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
        app->return_to_mode = RETURN_MODE_PREVIEW;
    }
    app_set_mode(app, APP_MODE_SINGLE);
    app_render_current_image(app);
}

static void handle_mouse_double_click_book_preview(PixelTermApp *app, const InputEvent *event) {
    app->input.pending_grid_single_click = FALSE;
    gboolean redraw_needed = FALSE;
    gboolean hit = FALSE;
    app_handle_mouse_click_book_preview(app, event->mouse_x, event->mouse_y, &redraw_needed, &hit);
    if (!hit) {
        return;
    }
    if (app_enter_book_page(app, app->book.preview_selected) == ERROR_NONE) {
        app_render_book_page(app);
    }
}

static void handle_mouse_double_click_file_manager(PixelTermApp *app, const InputEvent *event) {
    app->input.pending_file_manager_single_click = FALSE;
    ErrorCode err = app_file_manager_enter_at_position(app, event->mouse_x, event->mouse_y);
    if (err == ERROR_NONE && app_is_file_manager_mode(app)) {
        app_render_file_manager(app);
    }
}

static void handle_mouse_double_click_book(PixelTermApp *app, const InputEvent *event) {
    (void)event;
    app->input.pending_single_click = FALSE;
    if (app_enter_book_preview(app) == ERROR_NONE) {
        app_render_book_preview(app);
    }
}

static void handle_mouse_double_click_book_toc(PixelTermApp *app, const InputEvent *event) {
    gboolean redraw_needed = FALSE;
    gboolean hit = FALSE;
    app_handle_mouse_click_book_toc(app,
                                    event->mouse_x,
                                    event->mouse_y,
                                    &redraw_needed,
                                    &hit);
    if (!hit) {
        return;
    }
    gint page = app_book_toc_get_selected_page(app);
    app->book.toc_visible = FALSE;
    if (page >= 0 && app_enter_book_page(app, page) == ERROR_NONE) {
        app_render_book_page(app);
    } else if (app_is_book_preview_mode(app)) {
        app_render_book_preview(app);
    } else {
        app_render_book_page(app);
    }
}

static void handle_mouse_double_click_single(PixelTermApp *app, const InputEvent *event) {
    (void)event;
    app->input.pending_single_click = FALSE;

    if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
        app->return_to_mode = RETURN_MODE_PREVIEW;
    }
    if (app_enter_preview(app) == ERROR_NONE) {
        app_render_preview_grid(app);
    }
}

static void handle_mouse_double_click(PixelTermApp *app, const InputEvent *event) {
    if (app->book.toc_visible) {
        handle_mouse_double_click_book_toc(app, event);
        return;
    }
    if (app_is_book_preview_mode(app)) {
        handle_mouse_double_click_book_preview(app, event);
    } else if (app_is_preview_mode(app)) {
        handle_mouse_double_click_preview(app, event);
    } else if (app_is_file_manager_mode(app)) {
        handle_mouse_double_click_file_manager(app, event);
    } else if (app_is_book_mode(app)) {
        handle_mouse_double_click_book(app, event);
    } else {
        handle_mouse_double_click_single(app, event);
    }
}

static void handle_mouse_scroll_preview(PixelTermApp *app, const InputEvent *event) {
    gint old_selected = app->preview.selected;
    gint old_scroll = app->preview.scroll;
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        app_preview_page_move(app, -1);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        app_preview_page_move(app, 1);
    }
    if (app->preview.scroll != old_scroll) {
        app_render_preview_grid(app);
    } else if (app->preview.selected != old_selected) {
        app_render_preview_selection_change(app, old_selected);
    }
}

static gboolean image_get_mouse_anchor(PixelTermApp *app, gdouble *rel_px_x, gdouble *rel_px_y) {
    if (!app || !rel_px_x || !rel_px_y) {
        return FALSE;
    }
    if (app->image_view_width <= 0 || app->image_view_height <= 0 ||
        app->image_viewport_px_w <= 0 || app->image_viewport_px_h <= 0) {
        return FALSE;
    }

    gint x = app->input.last_mouse_x;
    gint y = app->input.last_mouse_y;
    if (x < app->image_view_left_col ||
        y < app->image_view_top_row ||
        x >= app->image_view_left_col + app->image_view_width ||
        y >= app->image_view_top_row + app->image_view_height) {
        return FALSE;
    }

    gdouble rel_x_cells = (gdouble)(x - app->image_view_left_col);
    gdouble rel_y_cells = (gdouble)(y - app->image_view_top_row);
    gdouble frac_x = rel_x_cells / (gdouble)app->image_view_width;
    gdouble frac_y = rel_y_cells / (gdouble)app->image_view_height;
    if (frac_x < 0.0) frac_x = 0.0;
    if (frac_x > 1.0) frac_x = 1.0;
    if (frac_y < 0.0) frac_y = 0.0;
    if (frac_y > 1.0) frac_y = 1.0;

    *rel_px_x = frac_x * app->image_viewport_px_w;
    *rel_px_y = frac_y * app->image_viewport_px_h;
    return TRUE;
}

static void image_adjust_zoom(PixelTermApp *app, gdouble delta) {
    if (!app_is_single_mode(app)) {
        return;
    }
    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return;
    }
    if (app_current_is_video(app) || app_current_is_animated_image(app)) {
        return;
    }
    if (delta < 0.0 && app->image_zoom <= 1.0 + 0.001) {
        return;
    }

    gdouble old_zoom = app->image_zoom;
    gdouble new_zoom = old_zoom + delta;
    if (new_zoom < 1.0) new_zoom = 1.0;
    if (fabs(new_zoom - old_zoom) < 0.001) {
        return;
    }

    if (new_zoom <= 1.0) {
        app->image_zoom = 1.0;
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;
    } else {
        gdouble rel_px_x = 0.0;
        gdouble rel_px_y = 0.0;
        gboolean has_anchor = image_get_mouse_anchor(app, &rel_px_x, &rel_px_y);
        if (has_anchor) {
            gdouble ratio = new_zoom / old_zoom;
            gdouble content_x = app->image_pan_x + rel_px_x;
            gdouble content_y = app->image_pan_y + rel_px_y;
            app->image_pan_x = content_x * ratio - rel_px_x;
            app->image_pan_y = content_y * ratio - rel_px_y;
        } else {
            app->image_pan_x = 0.0;
            app->image_pan_y = 0.0;
        }
        app->image_zoom = new_zoom;
    }

    app->suppress_full_clear = TRUE;
    app_render_current_image(app);
}

static void book_change_page(PixelTermApp *app, gint delta) {
    if (!app_is_book_mode(app)) {
        return;
    }
    gint new_page = app->book.page + delta;
    if (new_page < 0) new_page = 0;
    if (new_page >= app->book.page_count) {
        new_page = app->book.page_count - 1;
    }
    if (new_page < 0) new_page = 0;
    if (new_page == app->book.page) {
        return;
    }
    app->suppress_full_clear = TRUE;
    if (app_enter_book_page(app, new_page) == ERROR_NONE) {
        app_render_book_page(app);
    }
}

static void handle_mouse_scroll_book_preview(PixelTermApp *app, const InputEvent *event) {
    gint old_scroll = app->book.preview_scroll;
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        app_book_preview_scroll_pages(app, -1);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        app_book_preview_scroll_pages(app, 1);
    }
    if (app->book.preview_scroll != old_scroll) {
        app_render_book_preview(app);
    }
}

static void handle_mouse_scroll_file_manager(PixelTermApp *app, const InputEvent *event) {
    gint old_selected = app->file_manager.selected_entry;
    gint old_scroll = app->file_manager.scroll_offset;
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        app_file_manager_up(app);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        app_file_manager_down(app);
    }
    if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
        app_render_file_manager(app);
    }
}

static void handle_mouse_scroll_single(PixelTermApp *app, const InputEvent *event) {
    if (app_current_is_video(app)) {
        return;
    }
    if (app_current_is_animated_image(app)) {
        return;
    }
    gdouble rel_px_x = 0.0;
    gdouble rel_px_y = 0.0;
    if (!image_get_mouse_anchor(app, &rel_px_x, &rel_px_y)) {
        return;
    }
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        image_adjust_zoom(app, k_image_zoom_step);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        image_adjust_zoom(app, -k_image_zoom_step);
    }
}

static void handle_mouse_scroll_book(PixelTermApp *app, const InputEvent *event) {
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        gint page_step = app_book_use_double_page(app) ? 2 : 1;
        book_change_page(app, -page_step);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        gint page_step = app_book_use_double_page(app) ? 2 : 1;
        book_change_page(app, page_step);
    }
}

static void handle_mouse_scroll_book_toc(PixelTermApp *app, const InputEvent *event) {
    if (!app || !app->book.toc_visible) {
        return;
    }
    gint old_selected = app->book.toc_selected;
    gint old_scroll = app->book.toc_scroll;

    if (event->mouse_button == MOUSE_SCROLL_UP) {
        app_book_toc_move_selection(app, -1);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        app_book_toc_move_selection(app, 1);
    }

    if (app->book.toc_selected != old_selected || app->book.toc_scroll != old_scroll) {
        app_render_book_toc(app);
    }
}

static void handle_mouse_scroll(PixelTermApp *app, const InputEvent *event) {
    if (app->book.toc_visible) {
        handle_mouse_scroll_book_toc(app, event);
    } else if (app_is_book_preview_mode(app)) {
        handle_mouse_scroll_book_preview(app, event);
    } else if (app_is_preview_mode(app)) {
        handle_mouse_scroll_preview(app, event);
    } else if (app_is_file_manager_mode(app)) {
        handle_mouse_scroll_file_manager(app, event);
    } else if (app_is_book_mode(app)) {
        handle_mouse_scroll_book(app, event);
    } else {
        handle_mouse_scroll_single(app, event);
    }
}

static void process_pending_clicks(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (app->book.toc_visible) {
        app->input.pending_single_click = FALSE;
        app->input.pending_grid_single_click = FALSE;
        return;
    }

    // Process pending single click action (Single Image / Book Mode).
    if (app->input.pending_single_click &&
        (app_is_single_mode(app) || app_is_book_mode(app))) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->input.pending_click_time > k_click_threshold_us) {
            app->input.pending_single_click = FALSE;
            if (app_is_book_mode(app)) {
                gint page_step = app_book_use_double_page(app) ? 2 : 1;
                book_change_page(app, page_step);
            } else {
                app_next_image(app);
                if (app->needs_redraw) {
                    app->suppress_full_clear = TRUE;
                    app->async.render_request = TRUE;
                    app_refresh_display(app);
                    app->needs_redraw = FALSE;
                }
            }
        }
    } else if (app->input.pending_single_click) {
        app->input.pending_single_click = FALSE;
    }

    // Process pending single click action (Preview Grid Mode).
    if (app->input.pending_grid_single_click) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->input.pending_grid_click_time > k_click_threshold_us) {
            app->input.pending_grid_single_click = FALSE;
            gboolean redraw_needed = FALSE;
            if (app_is_book_preview_mode(app)) {
                gint old_selected = app->book.preview_selected;
                gint old_scroll = app->book.preview_scroll;
                app_handle_mouse_click_book_preview(app,
                                                    app->input.pending_grid_click_x,
                                                    app->input.pending_grid_click_y,
                                                    &redraw_needed,
                                                    NULL);
                if (redraw_needed) {
                    if (app->book.preview_scroll != old_scroll) {
                        app_render_book_preview(app);
                    } else if (app->book.preview_selected != old_selected) {
                        app_render_book_preview_selection_change(app, old_selected);
                    }
                }
            } else if (app_is_preview_mode(app)) {
                gint old_selected = app->preview.selected;
                gint old_scroll = app->preview.scroll;
                app_handle_mouse_click_preview(app,
                                               app->input.pending_grid_click_x,
                                               app->input.pending_grid_click_y,
                                               &redraw_needed,
                                               NULL);
                if (redraw_needed) {
                    if (app->preview.scroll != old_scroll) {
                        app_render_preview_grid(app);
                    } else if (app->preview.selected != old_selected) {
                        app_render_preview_selection_change(app, old_selected);
                    }
                }
            }
        }
    }

    // Process pending single click action (File Manager Mode).
    if (app_is_file_manager_mode(app) && app->input.pending_file_manager_single_click) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->input.pending_file_manager_click_time > k_click_threshold_us) {
            app->input.pending_file_manager_single_click = FALSE;
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            app_handle_mouse_file_manager(app,
                                          app->input.pending_file_manager_click_x,
                                          app->input.pending_file_manager_click_y);
            if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
                app_render_file_manager(app);
            }
        }
    } else if (!app_is_file_manager_mode(app) && app->input.pending_file_manager_single_click) {
        app->input.pending_file_manager_single_click = FALSE;
    }
}

static void drain_main_context_if_playing(gboolean is_playing) {
    if (!is_playing) {
        return;
    }

    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static void process_animation_events(PixelTermApp *app) {
    if (!app) {
        return;
    }

    drain_main_context_if_playing(app->gif_player && gif_player_is_playing(app->gif_player));
    drain_main_context_if_playing(app->video_player && video_player_is_playing(app->video_player));
}

static void handle_delete_current_image(PixelTermApp *app) {
    if (!app) {
        return;
    }

    ErrorCode err = app_delete_current_image(app);
    if (err != ERROR_NONE) {
        app_render_by_mode(app);
        return;
    }

    if (!app_has_images(app)) {
        app->needs_screen_clear = TRUE;
        if (app_enter_file_manager(app) == ERROR_NONE) {
            app_render_file_manager(app);
        } else {
            app_refresh_display(app);
        }
        return;
    }

    app_render_by_mode(app);
}

static void handle_toggle_video_fps(PixelTermApp *app) {
    if (!app || !app_current_is_video(app) || !app->video_player) {
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
    if (!app || !app_current_is_video(app)) {
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

static void book_jump_start(PixelTermApp *app) {
    if (!app || app->book.jump_active) {
        return;
    }
    gint current = 1;
    if (app_is_book_preview_mode(app)) {
        current = app->book.preview_selected + 1;
    } else if (app_is_book_mode(app)) {
        current = app->book.page + 1;
    }
    if (current < 1) current = 1;
    char current_buf[16];
    g_snprintf(current_buf, sizeof(current_buf), "%d", current);
    app->book.jump_buf[0] = '\0';
    app->book.jump_len = 0;
    app->book.jump_active = TRUE;
    app->book.jump_dirty = FALSE;
    app_book_jump_render_prompt(app);
}

static void book_jump_cancel(PixelTermApp *app) {
    if (!app || !app->book.jump_active) {
        return;
    }
    app->book.jump_active = FALSE;
    app->book.jump_dirty = FALSE;
    app->book.jump_len = 0;
    app->book.jump_buf[0] = '\0';
    app_book_jump_clear_prompt(app);
}

static void book_jump_commit(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (!app->book.jump_active) {
        return;
    }
    if (!app->book.jump_dirty || app->book.jump_len <= 0) {
        book_jump_cancel(app);
        return;
    }
    gint total = app->book.page_count;
    if (total < 1) total = 1;
    gint value = atoi(app->book.jump_buf);
    if (value < 1) value = 1;
    if (value > total) value = total;

    if (app_is_book_preview_mode(app)) {
        gint old_selected = app->book.preview_selected;
        gint old_scroll = app->book.preview_scroll;
        book_jump_cancel(app);
        app_book_preview_jump_to_page(app, value - 1);
        if (app->book.preview_scroll != old_scroll) {
            app_render_book_preview(app);
        } else if (app->book.preview_selected != old_selected) {
            app_render_book_preview_selection_change(app, old_selected);
        }
    } else if (app_is_book_mode(app)) {
        if (value - 1 == app->book.page) {
            book_jump_cancel(app);
            return;
        }
        book_jump_cancel(app);
        if (app_enter_book_page(app, value - 1) == ERROR_NONE) {
            app->suppress_full_clear = TRUE;
            app_render_book_page(app);
        }
    }
}

static gboolean handle_book_jump_input(PixelTermApp *app, const InputEvent *event) {
    if (!app || !app->book.jump_active || !event || event->type != INPUT_KEY_PRESS) {
        return FALSE;
    }

    if (event->key_code == KEY_ESCAPE) {
        book_jump_cancel(app);
        return TRUE;
    }
    if (event->key_code == (KeyCode)'p' || event->key_code == (KeyCode)'P') {
        book_jump_cancel(app);
        return TRUE;
    }
    if (event->key_code == KEY_ENTER || event->key_code == 13) {
        book_jump_commit(app);
        return TRUE;
    }
    if (event->key_code == KEY_BACKSPACE || event->key_code == KEY_DELETE || event->key_code == 127) {
        if (app->book.jump_len > 0) {
            app->book.jump_len--;
            app->book.jump_buf[app->book.jump_len] = '\0';
            app->book.jump_dirty = TRUE;
            app_book_jump_render_prompt(app);
        }
        return TRUE;
    }
    if (event->key_code >= (KeyCode)'0' && event->key_code <= (KeyCode)'9') {
        gint total = app->book.page_count;
        if (total < 1) total = 1;
        char total_text[16];
        g_snprintf(total_text, sizeof(total_text), "%d", total);
        gint total_len = (gint)strlen(total_text);
        gint max_len = total_len > 0 ? total_len : 1;
        if (max_len > k_book_jump_max_digits) max_len = k_book_jump_max_digits;
        if (max_len > (gint)(sizeof(app->book.jump_buf) - 1)) {
            max_len = (gint)(sizeof(app->book.jump_buf) - 1);
        }
        if (app->book.jump_len < max_len) {
            app->book.jump_buf[app->book.jump_len++] = (char)event->key_code;
            app->book.jump_buf[app->book.jump_len] = '\0';
            app->book.jump_dirty = TRUE;
            app_book_jump_render_prompt(app);
        }
        return TRUE;
    }

    return TRUE;
}

static void handle_video_protocol_toggle(PixelTermApp *app) {
    if (!app || !app_current_is_video(app) || !app->video_player || !app->video_player->renderer) {
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
        next_kitty = TRUE;
    } else if (force_kitty) {
        next_iterm2 = TRUE;
    } else if (force_iterm2) {
        next_sixel = TRUE;
    } else if (force_sixel) {
        next_text = TRUE;
    } else {
        next_kitty = TRUE;
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

static gboolean handle_key_press_common(PixelTermApp *app,
                                        InputHandler *input_handler,
                                        const InputEvent *event) {
    switch (event->key_code) {
        case KEY_ESCAPE:
            app->running = FALSE;
            input_handler->should_exit = TRUE;
            return TRUE;
        case (KeyCode)'d':
        case (KeyCode)'D':
            if (!app_is_single_mode(app)) {
                return FALSE;
            }
            app->dither_enabled = !app->dither_enabled;
            if (app->preloader) {
                preloader_stop(app->preloader);
                preloader_cache_clear(app->preloader);
                preloader_initialize(app->preloader, app->dither_enabled, app->render_work_factor,
                                     app->force_text, app->force_sixel, app->force_kitty, app->force_iterm2,
                                     app->gamma);
                preloader_start(app->preloader);
            }
            app_render_by_mode(app);
            return TRUE;
        case (KeyCode)'i':
            if (app_current_is_video(app) || app_is_book_mode(app) || app_is_book_preview_mode(app)) {
                return TRUE;
            }
            if (!app_is_preview_mode(app)) {
                if (app->ui_text_hidden) {
                    return TRUE;
                }
                if (app->info_visible) {
                    app->info_visible = FALSE;
                    app_render_current_image(app);
                } else {
                    app_display_image_info(app);
                }
            }
            return TRUE;
        case (KeyCode)'f':
        case (KeyCode)'F':
            handle_toggle_video_fps(app);
            return TRUE;
        case (KeyCode)'~':
        case (KeyCode)'`':
            if (!app_is_file_manager_mode(app)) {
                gboolean info_was_visible = app->info_visible;
                app->ui_text_hidden = !app->ui_text_hidden;
                if (app->ui_text_hidden) {
                    app->info_visible = FALSE;
                }
                if (app_is_book_preview_mode(app)) {
                    app->suppress_full_clear = TRUE;
                    app->needs_screen_clear = FALSE;
                } else if (app_is_preview_mode(app)) {
                    app->needs_screen_clear = TRUE;
                } else if (!info_was_visible) {
                    app->suppress_full_clear = TRUE;
                }
                app_render_by_mode(app);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

static void handle_key_press_preview(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    switch (event->key_code) {
        case KEY_LEFT:
        case (KeyCode)'h': {
            gint old_selected = app->preview.selected;
            gint old_scroll = app->preview.scroll;
            app_preview_move_selection(app, 0, -1);
            if (app->preview.scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview.selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case KEY_RIGHT:
        case (KeyCode)'l': {
            gint old_selected = app->preview.selected;
            gint old_scroll = app->preview.scroll;
            app_preview_move_selection(app, 0, 1);
            if (app->preview.scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview.selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'k':
        case KEY_UP: {
            gint old_selected = app->preview.selected;
            gint old_scroll = app->preview.scroll;
            app_preview_move_selection(app, -1, 0);
            if (app->preview.scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview.selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case (KeyCode)'j':
        case KEY_DOWN: {
            gint old_selected = app->preview.selected;
            gint old_scroll = app->preview.scroll;
            app_preview_move_selection(app, 1, 0);
            if (app->preview.scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview.selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_PAGE_DOWN: {
            gint old_selected = app->preview.selected;
            gint old_scroll = app->preview.scroll;
            app_preview_page_move(app, 1);
            if (app->preview.scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview.selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        }
        case KEY_PAGE_UP: {
            gint old_selected = app->preview.selected;
            gint old_scroll = app->preview.scroll;
            app_preview_page_move(app, -1);
            if (app->preview.scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview.selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        }
        case (KeyCode)'+':
        case (KeyCode)'=':
            app_preview_change_zoom(app, 1);
            break;
        case (KeyCode)'-':
            app_preview_change_zoom(app, -1);
            break;
        case KEY_TAB:
            if (app->return_to_mode == RETURN_MODE_PREVIEW) {
                app_exit_preview(app, TRUE);
                app_refresh_display(app);
            } else {
                ReturnMode saved_return_mode = app->return_to_mode;
                app->return_to_mode = RETURN_MODE_PREVIEW;
                app_exit_preview(app, TRUE);
                app_enter_file_manager(app);
                if (saved_return_mode == RETURN_MODE_PREVIEW_VIRTUAL && app->file_manager.previous_selected_entry >= 0) {
                    app->file_manager.selected_entry = app->file_manager.previous_selected_entry;
                    app->file_manager.previous_selected_entry = -1;
                }
                app_render_file_manager(app);
            }
            break;
        case KEY_ENTER:
        case 13:
            if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
                app->return_to_mode = RETURN_MODE_PREVIEW;
            }
            app_exit_preview(app, TRUE);
            app_refresh_display(app);
            break;
        default:
            break;
    }
}

static void handle_key_press_book_preview(PixelTermApp *app,
                                          InputHandler *input_handler,
                                          const InputEvent *event) {
    if (app && app->book.toc_visible) {
        handle_key_press_book_toc(app, input_handler, event);
        return;
    }
    if (app && app->book.jump_active) {
        return;
    }
    switch (event->key_code) {
        case KEY_LEFT:
        case (KeyCode)'h':
            {
                gint old_selected = app->book.preview_selected;
                gint old_scroll = app->book.preview_scroll;
                app_book_preview_move_selection(app, 0, -1);
                if (app->book.preview_scroll != old_scroll) {
                    app_render_book_preview(app);
                } else if (app->book.preview_selected != old_selected) {
                    app_render_book_preview_selection_change(app, old_selected);
                }
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        case KEY_RIGHT:
        case (KeyCode)'l':
            {
                gint old_selected = app->book.preview_selected;
                gint old_scroll = app->book.preview_scroll;
                app_book_preview_move_selection(app, 0, 1);
                if (app->book.preview_scroll != old_scroll) {
                    app_render_book_preview(app);
                } else if (app->book.preview_selected != old_selected) {
                    app_render_book_preview_selection_change(app, old_selected);
                }
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        case (KeyCode)'k':
        case KEY_UP:
            {
                gint old_selected = app->book.preview_selected;
                gint old_scroll = app->book.preview_scroll;
                app_book_preview_move_selection(app, -1, 0);
                if (app->book.preview_scroll != old_scroll) {
                    app_render_book_preview(app);
                } else if (app->book.preview_selected != old_selected) {
                    app_render_book_preview_selection_change(app, old_selected);
                }
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        case (KeyCode)'j':
        case KEY_DOWN:
            {
                gint old_selected = app->book.preview_selected;
                gint old_scroll = app->book.preview_scroll;
                app_book_preview_move_selection(app, 1, 0);
                if (app->book.preview_scroll != old_scroll) {
                    app_render_book_preview(app);
                } else if (app->book.preview_selected != old_selected) {
                    app_render_book_preview_selection_change(app, old_selected);
                }
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        case KEY_PAGE_DOWN:
            {
                gint old_selected = app->book.preview_selected;
                gint old_scroll = app->book.preview_scroll;
                app_book_preview_page_move(app, 1);
                if (app->book.preview_scroll != old_scroll) {
                    app_render_book_preview(app);
                } else if (app->book.preview_selected != old_selected) {
                    app_render_book_preview_selection_change(app, old_selected);
                }
            }
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        case KEY_PAGE_UP:
            {
                gint old_selected = app->book.preview_selected;
                gint old_scroll = app->book.preview_scroll;
                app_book_preview_page_move(app, -1);
                if (app->book.preview_scroll != old_scroll) {
                    app_render_book_preview(app);
                } else if (app->book.preview_selected != old_selected) {
                    app_render_book_preview_selection_change(app, old_selected);
                }
            }
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        case (KeyCode)'p':
        case (KeyCode)'P':
            book_jump_start(app);
            break;
        case (KeyCode)'+':
        case (KeyCode)'=':
            app_book_preview_change_zoom(app, 1);
            app_render_book_preview(app);
            break;
        case (KeyCode)'-':
            app_book_preview_change_zoom(app, -1);
            app_render_book_preview(app);
            break;
        case KEY_ENTER:
        case 13:
            if (app_enter_book_page(app, app->book.preview_selected) == ERROR_NONE) {
                app_render_book_page(app);
            } else {
                app_refresh_display(app);
            }
            break;
        case KEY_TAB:
            {
                gchar *book_path = app->book.path ? g_strdup(app->book.path) : NULL;
                app_close_book(app);
                app_enter_file_manager(app);
                if (book_path) {
                    app_file_manager_select_path(app, book_path);
                    g_free(book_path);
                }
                app_render_file_manager(app);
            }
            break;
        case (KeyCode)'t':
        case (KeyCode)'T':
            if (app->book.toc) {
                app->book.toc_visible = !app->book.toc_visible;
                if (app->book.toc_visible) {
                    app_book_toc_sync_to_page(app, app->book.preview_selected);
                    app_render_book_toc(app);
                } else {
                    app_render_book_preview(app);
                }
            } else {
                app->book.toc_visible = FALSE;
                app_render_book_preview(app);
            }
            break;
        default:
            break;
    }
}

static void handle_key_press_book(PixelTermApp *app,
                                  InputHandler *input_handler,
                                  const InputEvent *event) {
    if (app && app->book.toc_visible) {
        handle_key_press_book_toc(app, input_handler, event);
        return;
    }
    if (app && app->book.jump_active) {
        return;
    }
    gint page_step = app_book_use_double_page(app) ? 2 : 1;
    switch (event->key_code) {
        case KEY_LEFT:
        case (KeyCode)'h':
            book_change_page(app, -1);
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        case KEY_RIGHT:
        case (KeyCode)'l':
            book_change_page(app, 1);
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        case KEY_UP:
        case (KeyCode)'k':
            book_change_page(app, -page_step);
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        case KEY_DOWN:
        case (KeyCode)'j':
            book_change_page(app, page_step);
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        case KEY_PAGE_UP: {
            book_change_page(app, -page_step);
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        }
        case KEY_PAGE_DOWN: {
            book_change_page(app, page_step);
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        }
        case (KeyCode)'p':
        case (KeyCode)'P':
            book_jump_start(app);
            break;
        case KEY_ENTER:
        case 13:
            if (app_enter_book_preview(app) == ERROR_NONE) {
                app_render_book_preview(app);
            }
            break;
        case KEY_TAB:
            {
                gchar *book_path = app->book.path ? g_strdup(app->book.path) : NULL;
                app_close_book(app);
                app_enter_file_manager(app);
                if (book_path) {
                    app_file_manager_select_path(app, book_path);
                    g_free(book_path);
                }
                app_render_file_manager(app);
            }
            break;
        case (KeyCode)'t':
        case (KeyCode)'T':
            if (app->book.toc) {
                app->book.toc_visible = !app->book.toc_visible;
                if (app->book.toc_visible) {
                    app_book_toc_sync_to_page(app, app->book.page);
                    app_render_book_toc(app);
                } else {
                    app_render_book_page(app);
                }
            } else {
                app->book.toc_visible = FALSE;
                app_render_book_page(app);
            }
            break;
        default:
            break;
    }
}

static void handle_key_press_book_toc(PixelTermApp *app,
                                      InputHandler *input_handler,
                                      const InputEvent *event) {
    if (!app || !app->book.toc_visible || !app->book.toc) {
        return;
    }

    gint old_selected = app->book.toc_selected;
    gint old_scroll = app->book.toc_scroll;

    switch (event->key_code) {
        case KEY_UP:
        case (KeyCode)'k':
            app_book_toc_move_selection(app, -1);
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        case KEY_DOWN:
        case (KeyCode)'j':
            app_book_toc_move_selection(app, 1);
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        case KEY_PAGE_UP:
            app_book_toc_page_move(app, -1);
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        case KEY_PAGE_DOWN:
            app_book_toc_page_move(app, 1);
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        case KEY_HOME:
            if (app->book.toc->count > 0) {
                app_book_toc_move_selection(app, -app->book.toc_selected);
            }
            break;
        case KEY_END:
            if (app->book.toc->count > 0) {
                gint delta = (app->book.toc->count - 1) - app->book.toc_selected;
                app_book_toc_move_selection(app, delta);
            }
            break;
        case KEY_ENTER:
        case 13: {
            gint page = app_book_toc_get_selected_page(app);
            app->book.toc_visible = FALSE;
            if (page >= 0 && app_enter_book_page(app, page) == ERROR_NONE) {
                app_render_book_page(app);
            } else if (app_is_book_preview_mode(app)) {
                app_render_book_preview(app);
            } else {
                app_render_book_page(app);
            }
            return;
        }
        case KEY_ESCAPE:
        case (KeyCode)'t':
        case (KeyCode)'T':
            app->book.toc_visible = FALSE;
            if (app_is_book_preview_mode(app)) {
                app_render_book_preview(app);
            } else {
                app_render_book_page(app);
            }
            return;
        default:
            break;
    }

    if (app->book.toc_visible &&
        (app->book.toc_selected != old_selected || app->book.toc_scroll != old_scroll)) {
        app_render_book_toc(app);
    }
}

static void handle_key_press_file_manager(PixelTermApp *app,
                                          InputHandler *input_handler,
                                          const InputEvent *event) {
    if ((event->key_code >= 'A' && event->key_code <= 'Z') ||
        (event->key_code >= 'a' && event->key_code <= 'z')) {
        gint old_selected = app->file_manager.selected_entry;
        gint old_scroll = app->file_manager.scroll_offset;
        app_file_manager_jump_to_letter(app, (char)event->key_code);
        if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
            app_render_file_manager(app);
        }
        return;
    }

    switch (event->key_code) {
        case KEY_LEFT:
        case (KeyCode)'h': {
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            GList *old_entries = app->file_manager.entries;
            gchar *old_dir = app->file_manager.directory ? g_strdup(app->file_manager.directory) : NULL;
            ErrorCode err = app_file_manager_left(app);
            gboolean dir_changed = (g_strcmp0(old_dir, app->file_manager.directory) != 0);
            gboolean state_changed = dir_changed ||
                                     (app->file_manager.entries != old_entries) ||
                                     (app->file_manager.selected_entry != old_selected) ||
                                     (app->file_manager.scroll_offset != old_scroll);
            g_free(old_dir);
            if (err == ERROR_NONE && state_changed) {
                app_render_file_manager(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case KEY_RIGHT:
        case (KeyCode)'l': {
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            GList *old_entries = app->file_manager.entries;
            gchar *old_dir = app->file_manager.directory ? g_strdup(app->file_manager.directory) : NULL;
            ErrorCode err = app_file_manager_right(app);
            gboolean dir_changed = (g_strcmp0(old_dir, app->file_manager.directory) != 0);
            gboolean state_changed = dir_changed ||
                                     (app->file_manager.entries != old_entries) ||
                                     (app->file_manager.selected_entry != old_selected) ||
                                     (app->file_manager.scroll_offset != old_scroll);
            g_free(old_dir);
            if (err == ERROR_NONE && app_is_file_manager_mode(app)) {
                if (state_changed) {
                    app_render_file_manager(app);
                }
            } else if (app_is_file_manager_mode(app)) {
                app_render_file_manager(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'k':
        case KEY_UP: {
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            app_file_manager_up(app);
            if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
                app_render_file_manager(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case (KeyCode)'j':
        case KEY_DOWN: {
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            app_file_manager_down(app);
            if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
                app_render_file_manager(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_TAB: {
            gchar *selected_path = NULL;
            if (app->file_manager.selected_entry >= 0 && app->file_manager.selected_entry < g_list_length(app->file_manager.entries)) {
                selected_path = (gchar*)g_list_nth_data(app->file_manager.entries, app->file_manager.selected_entry);
            }
            if (selected_path && is_valid_book_file(selected_path)) {
                ErrorCode book_error = app_open_book(app, selected_path);
                if (book_error == ERROR_NONE) {
                    app_exit_file_manager(app);
                    if (app_enter_book_preview(app) == ERROR_NONE) {
                        app_render_book_preview(app);
                    } else {
                        app_refresh_display(app);
                    }
                } else {
                    app_render_file_manager(app);
                }
                break;
            }

            if (!app_file_manager_has_images(app)) {
                break;
            }

            ErrorCode load_error = app_load_directory(app, app->file_manager.directory);
            if (load_error != ERROR_NONE) {
                app_render_file_manager(app);
                break;
            }

            if (app_file_manager_selection_is_image(app)) {
                app->return_to_mode = RETURN_MODE_PREVIEW;
                gint selected_image_index = app_file_manager_get_selected_image_index(app);
                if (selected_image_index >= 0) {
                    app->current_index = selected_image_index;
                }
                app_exit_file_manager(app);
                if (app_enter_preview(app) == ERROR_NONE) {
                    app_render_preview_grid(app);
                } else {
                    app_refresh_display(app);
                }
            } else {
                app->return_to_mode = RETURN_MODE_PREVIEW_VIRTUAL;
                app->file_manager.previous_selected_entry = app->file_manager.selected_entry;
                app_exit_file_manager(app);
                if (app_enter_preview(app) == ERROR_NONE) {
                    app->preview.selected = 0;
                    app_render_preview_grid(app);
                } else {
                    app_refresh_display(app);
                }
            }
            break;
        }
        case KEY_ENTER:
        case 13:
            {
                ErrorCode error;
                input_flush_buffer(input_handler);
                error = app_file_manager_enter(app);
                if (error != ERROR_NONE) {
                    app_render_file_manager(app);
                } else if (app_is_file_manager_mode(app)) {
                    app_render_file_manager(app);
                }
            }
            break;
        case KEY_BACKSPACE:
        case 8:
            if (app_file_manager_toggle_hidden(app) == ERROR_NONE) {
                app_render_file_manager(app);
            }
            break;
        default:
            break;
    }
}

static void handle_key_press_single(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    switch (event->key_code) {
        case (KeyCode)' ':
            app_toggle_video_playback(app);
            break;
        case (KeyCode)'+':
        case (KeyCode)'=':
            if (app_current_is_video(app)) {
                handle_video_scale_change(app, k_video_scale_step);
            }
            break;
        case (KeyCode)'-':
            if (app_current_is_video(app)) {
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

static void handle_key_press(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    if (handle_delete_request(app, event)) {
        return;
    }
    if (app && app->book.jump_active && (app_is_book_mode(app) || app_is_book_preview_mode(app))) {
        if (handle_book_jump_input(app, event)) {
            return;
        }
    }
    if (handle_key_press_common(app, input_handler, event)) {
        return;
    }
    if (app_is_book_preview_mode(app)) {
        handle_key_press_book_preview(app, input_handler, event);
    } else if (app_is_book_mode(app)) {
        handle_key_press_book(app, input_handler, event);
    } else if (app_is_preview_mode(app)) {
        handle_key_press_preview(app, input_handler, event);
    } else if (app_is_file_manager_mode(app)) {
        handle_key_press_file_manager(app, input_handler, event);
    } else {
        handle_key_press_single(app, input_handler, event);
    }
}

static void handle_input_event(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    if (app && event) {
        if (event->type == INPUT_MOUSE_PRESS ||
            event->type == INPUT_MOUSE_DOUBLE_CLICK ||
            event->type == INPUT_MOUSE_SCROLL) {
            app->input.last_mouse_x = event->mouse_x;
            app->input.last_mouse_y = event->mouse_y;
        }
    }
    switch (event->type) {
        case INPUT_MOUSE_PRESS:
            handle_mouse_press(app, event);
            break;
        case INPUT_MOUSE_DOUBLE_CLICK:
            handle_mouse_double_click(app, event);
            break;
        case INPUT_MOUSE_SCROLL:
            handle_mouse_scroll(app, event);
            break;
        case INPUT_KEY_PRESS:
            handle_key_press(app, input_handler, event);
            break;
        default:
            break;
    }
}

// Main application loop

void input_dispatch_handle_event(PixelTermApp *app,
                                 InputHandler *input_handler,
                                 const InputEvent *event) {
    handle_input_event(app, input_handler, event);
}

void input_dispatch_process_pending(PixelTermApp *app) {
    process_pending_clicks(app);
}

void input_dispatch_process_animations(PixelTermApp *app) {
    process_animation_events(app);
}

void input_dispatch_pause_video_for_resize(PixelTermApp *app) {
    app_pause_video_for_resize(app);
}
