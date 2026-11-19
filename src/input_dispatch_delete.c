#include "input_dispatch_delete_internal.h"

#include <string.h>

#include "input_dispatch_media_internal.h"
#include "text_utils.h"
#include "terminal_output.h"

static ErrorCode record_dispatch_error(PixelTermApp *app, ErrorCode result) {
    if (result == ERROR_OUTPUT_IO) app->last_error = result;
    return result;
}

static void enter_file_manager_or_refresh(PixelTermApp *app) {
    ErrorCode result = record_dispatch_error(app, app_enter_file_manager(app));
    if (result == ERROR_OUTPUT_IO) return;
    record_dispatch_error(app, result == ERROR_NONE ? app_render_file_manager(app)
                                                   : app_refresh_display(app));
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
        gint video_top_row = 0;
        gint video_height = 0;
        if (input_dispatch_current_is_video(app) &&
            video_player_get_last_frame_bounds(app->video_player,
                                               &video_top_row,
                                               &video_height)) {
            row = video_top_row + video_height;
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

static void delete_current_image_and_refresh(PixelTermApp *app);

static ErrorCode app_show_delete_prompt(PixelTermApp *app) {
    if (!app) {
        return ERROR_NONE;
    }

    const char *message = "Press r again to delete";
    gint term_width = app->term_width > 0 ? app->term_width : 80;
    gint row = delete_prompt_row(app);

    gint message_len = delete_prompt_display_width(message);
    gint col = term_width > message_len ? (term_width - message_len) / 2 + 1 : 1;

    char *prompt = g_strdup_printf("\033[%d;1H\033[2K\033[%d;%dH\033[31m%s\033[0m",
                                   row, row, col, message);
    gboolean submitted = terminal_output_write(prompt, strlen(prompt));
    g_free(prompt);
    return record_dispatch_error(app, submitted ? ERROR_NONE : ERROR_OUTPUT_IO);
}

static ErrorCode app_clear_delete_prompt(PixelTermApp *app) {
    if (!app) {
        return ERROR_NONE;
    }

    gint row = delete_prompt_row(app);
    char prompt[32];
    gint length = g_snprintf(prompt, sizeof(prompt), "\033[%d;1H\033[2K", row);
    return record_dispatch_error(app, terminal_output_write(prompt, length)
                                      ? ERROR_NONE : ERROR_OUTPUT_IO);
}

static void handle_delete_current_in_preview(PixelTermApp *app) {
    if (app_has_images(app)) {
        app->current_index = app->preview.selected;
        if (record_dispatch_error(app, app_delete_current_image(app)) == ERROR_OUTPUT_IO) return;
    }

    if (app_has_images(app)) {
        if (app->current_index < 0) app->current_index = 0;
        if (app->current_index >= app->total_images) app->current_index = app->total_images - 1;
        app->preview.selected = app->current_index;
        app->needs_screen_clear = TRUE;
        record_dispatch_error(app, app_render_preview_grid(app));
    } else {
        if (record_dispatch_error(app, app_transition_mode(app, APP_MODE_SINGLE)) != ERROR_NONE) return;
        app->needs_screen_clear = TRUE;
        enter_file_manager_or_refresh(app);
    }
}

gboolean input_dispatch_handle_delete_request(PixelTermApp *app, const InputEvent *event) {
    if (!app || !event || event->type != INPUT_KEY_PRESS) {
        return FALSE;
    }

    if (app_is_file_manager_mode(app) || app_is_book_preview_mode(app) || app_is_book_mode(app)) {
        if (app->delete_pending) {
            app->delete_pending = FALSE;
            if (app_clear_delete_prompt(app) == ERROR_OUTPUT_IO) return TRUE;
        }
        return FALSE;
    }

    if (app->delete_pending) {
        app->delete_pending = FALSE;
        if (event->key_code == (KeyCode)'r') {
            if (app_is_preview_mode(app)) {
                handle_delete_current_in_preview(app);
            } else {
                delete_current_image_and_refresh(app);
            }
            return TRUE;
        }
        if (app_clear_delete_prompt(app) == ERROR_OUTPUT_IO) return TRUE;
        return FALSE;
    }

    if (event->key_code == (KeyCode)'r') {
        app->delete_pending = app_show_delete_prompt(app) == ERROR_NONE;
        return TRUE;
    }

    return FALSE;
}

static void delete_current_image_and_refresh(PixelTermApp *app) {
    if (!app) {
        return;
    }

    ErrorCode err = record_dispatch_error(app, app_delete_current_image(app));
    if (err == ERROR_OUTPUT_IO) return;
    if (err != ERROR_NONE) {
        record_dispatch_error(app, app_render_by_mode(app));
        return;
    }

    if (!app_has_images(app)) {
        app->needs_screen_clear = TRUE;
        enter_file_manager_or_refresh(app);
        return;
    }

    record_dispatch_error(app, app_render_by_mode(app));
}
