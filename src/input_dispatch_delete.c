#include "input_dispatch_delete_internal.h"

#include <stdio.h>

#include "input_dispatch_media_internal.h"
#include "text_utils.h"

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
        if (input_dispatch_current_is_video(app) && app->video_player && app->video_player->last_frame_height > 0) {
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

static void delete_current_image_and_refresh(PixelTermApp *app);

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
        (void)app_transition_mode(app, APP_MODE_SINGLE);
        app->needs_screen_clear = TRUE;
        if (app_enter_file_manager(app) == ERROR_NONE) {
            app_render_file_manager(app);
        } else {
            app_refresh_display(app);
        }
    }
}

gboolean input_dispatch_handle_delete_request(PixelTermApp *app, const InputEvent *event) {
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
                delete_current_image_and_refresh(app);
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

static void delete_current_image_and_refresh(PixelTermApp *app) {
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
