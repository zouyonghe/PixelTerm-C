#include "input_dispatch_key_modes_internal.h"

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

void input_dispatch_handle_key_press_preview(PixelTermApp *app,
                                             InputHandler *input_handler,
                                             const InputEvent *event) {
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
