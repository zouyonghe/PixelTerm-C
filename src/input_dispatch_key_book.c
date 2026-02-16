#include "input_dispatch_key_modes_internal.h"

#include <stdlib.h>
#include <string.h>

#include "input_dispatch_book_internal.h"

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

gboolean input_dispatch_key_modes_handle_book_jump_input(PixelTermApp *app, const InputEvent *event) {
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
    if (event->key_code == KEY_BACKSPACE || event->key_code == KEY_DELETE) {
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

static void handle_key_press_book_toc(PixelTermApp *app,
                                      InputHandler *input_handler,
                                      const InputEvent *event) {
    if (!app || !app->book.toc || !event) {
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

void input_dispatch_handle_key_press_book_preview(PixelTermApp *app,
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
        case (KeyCode)'h': {
            gint old_selected = app->book.preview_selected;
            gint old_scroll = app->book.preview_scroll;
            app_book_preview_move_selection(app, 0, -1);
            if (app->book.preview_scroll != old_scroll) {
                app_render_book_preview(app);
            } else if (app->book.preview_selected != old_selected) {
                app_render_book_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case KEY_RIGHT:
        case (KeyCode)'l': {
            gint old_selected = app->book.preview_selected;
            gint old_scroll = app->book.preview_scroll;
            app_book_preview_move_selection(app, 0, 1);
            if (app->book.preview_scroll != old_scroll) {
                app_render_book_preview(app);
            } else if (app->book.preview_selected != old_selected) {
                app_render_book_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'k':
        case KEY_UP: {
            gint old_selected = app->book.preview_selected;
            gint old_scroll = app->book.preview_scroll;
            app_book_preview_move_selection(app, -1, 0);
            if (app->book.preview_scroll != old_scroll) {
                app_render_book_preview(app);
            } else if (app->book.preview_selected != old_selected) {
                app_render_book_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case (KeyCode)'j':
        case KEY_DOWN: {
            gint old_selected = app->book.preview_selected;
            gint old_scroll = app->book.preview_scroll;
            app_book_preview_move_selection(app, 1, 0);
            if (app->book.preview_scroll != old_scroll) {
                app_render_book_preview(app);
            } else if (app->book.preview_selected != old_selected) {
                app_render_book_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_PAGE_DOWN: {
            gint old_selected = app->book.preview_selected;
            gint old_scroll = app->book.preview_scroll;
            app_book_preview_page_move(app, 1);
            if (app->book.preview_scroll != old_scroll) {
                app_render_book_preview(app);
            } else if (app->book.preview_selected != old_selected) {
                app_render_book_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        }
        case KEY_PAGE_UP: {
            gint old_selected = app->book.preview_selected;
            gint old_scroll = app->book.preview_scroll;
            app_book_preview_page_move(app, -1);
            if (app->book.preview_scroll != old_scroll) {
                app_render_book_preview(app);
            } else if (app->book.preview_selected != old_selected) {
                app_render_book_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        }
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
        case KEY_TAB: {
            gchar *book_path = app->book.path ? g_strdup(app->book.path) : NULL;
            app_close_book(app);
            app_enter_file_manager(app);
            if (book_path) {
                app_file_manager_select_path(app, book_path);
                g_free(book_path);
            }
            app_render_file_manager(app);
            break;
        }
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

void input_dispatch_handle_key_press_book(PixelTermApp *app,
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
            input_dispatch_book_change_page(app, -1);
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        case KEY_RIGHT:
        case (KeyCode)'l':
            input_dispatch_book_change_page(app, 1);
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        case KEY_UP:
        case (KeyCode)'k':
            input_dispatch_book_change_page(app, -page_step);
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        case KEY_DOWN:
        case (KeyCode)'j':
            input_dispatch_book_change_page(app, page_step);
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        case KEY_PAGE_UP:
            input_dispatch_book_change_page(app, -page_step * 10);
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        case KEY_PAGE_DOWN:
            input_dispatch_book_change_page(app, page_step * 10);
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        case (KeyCode)'p':
        case (KeyCode)'P':
            book_jump_start(app);
            break;
        case KEY_TAB:
            if (app_enter_book_preview(app) == ERROR_NONE) {
                app_render_book_preview(app);
            } else {
                app_refresh_display(app);
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
