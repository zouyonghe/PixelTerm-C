#include "input_dispatch_key_modes_internal.h"

#include "input_dispatch_media_internal.h"
#include "app_book_transaction_internal.h"
#include "preload_control.h"

static ErrorCode record_output_error(PixelTermApp *app, ErrorCode result) {
    if (result == ERROR_OUTPUT_IO) app->last_error = result;
    return result;
}

static gpointer copy_media_path(gconstpointer path, gpointer user_data) {
    (void)user_data;
    return g_strdup(path);
}

static const KeyCode g_nav_keys_ud[] = {
    KEY_UP, KEY_DOWN, KEY_PAGE_UP, KEY_PAGE_DOWN
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

static const gchar* file_manager_selected_path(PixelTermApp *app) {
    if (!app || !app->file_manager.entries || app->file_manager.selected_entry < 0) {
        return NULL;
    }
    if (app->file_manager.entries_count > 0 &&
        app->file_manager.selected_entry >= app->file_manager.entries_count) {
        return NULL;
    }

    GList *cursor = app->file_manager.selected_link;
    gint idx = app->file_manager.selected_link_index;
    if (!cursor || idx < 0) {
        cursor = app->file_manager.entries;
        idx = 0;
    }

    while (cursor && idx < app->file_manager.selected_entry) {
        cursor = cursor->next;
        idx++;
    }
    while (cursor && idx > app->file_manager.selected_entry) {
        cursor = cursor->prev;
        idx--;
    }
    if (!cursor || idx != app->file_manager.selected_entry) {
        cursor = app->file_manager.entries;
        idx = 0;
        while (cursor && idx < app->file_manager.selected_entry) {
            cursor = cursor->next;
            idx++;
        }
    }
    if (!cursor || idx != app->file_manager.selected_entry) {
        return NULL;
    }

    app->file_manager.selected_link = cursor;
    app->file_manager.selected_link_index = idx;
    return (const gchar*)cursor->data;
}

void input_dispatch_handle_key_press_file_manager(PixelTermApp *app,
                                                  InputHandler *input_handler,
                                                  const InputEvent *event) {
    switch (event->key_code) {
        case KEY_LEFT: {
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            GList *old_entries = app->file_manager.entries;
            gchar *old_dir = app->file_manager.directory ? g_strdup(app->file_manager.directory) : NULL;
            ErrorCode err = record_output_error(app, app_file_manager_left(app));
            gboolean dir_changed = (g_strcmp0(old_dir, app->file_manager.directory) != 0);
            gboolean state_changed = dir_changed ||
                                     (app->file_manager.entries != old_entries) ||
                                     (app->file_manager.selected_entry != old_selected) ||
                                     (app->file_manager.scroll_offset != old_scroll);
            g_free(old_dir);
            if (err == ERROR_NONE && state_changed) {
                record_output_error(app, app_render_file_manager(app));
            }
            break;
        }
        case KEY_RIGHT: {
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            GList *old_entries = app->file_manager.entries;
            gchar *old_dir = app->file_manager.directory ? g_strdup(app->file_manager.directory) : NULL;
            ErrorCode err = record_output_error(app, app_file_manager_right(app));
            if (err == ERROR_OUTPUT_IO) {
                g_free(old_dir);
                break;
            }
            gboolean dir_changed = (g_strcmp0(old_dir, app->file_manager.directory) != 0);
            gboolean state_changed = dir_changed ||
                                     (app->file_manager.entries != old_entries) ||
                                     (app->file_manager.selected_entry != old_selected) ||
                                     (app->file_manager.scroll_offset != old_scroll);
            g_free(old_dir);
            if (err == ERROR_NONE && app_is_file_manager_mode(app)) {
                if (state_changed) {
                    record_output_error(app, app_render_file_manager(app));
                }
            }
            break;
        }
        case KEY_UP: {
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            if (record_output_error(app, app_file_manager_up(app)) == ERROR_OUTPUT_IO) break;
            if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
                if (record_output_error(app, app_render_file_manager(app)) == ERROR_OUTPUT_IO) break;
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_DOWN: {
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            if (record_output_error(app, app_file_manager_down(app)) == ERROR_OUTPUT_IO) break;
            if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
                if (record_output_error(app, app_render_file_manager(app)) == ERROR_OUTPUT_IO) break;
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_TAB: {
            const gchar *selected_path = file_manager_selected_path(app);
            if (selected_path && is_valid_book_file(selected_path)) {
                ErrorCode book_error = record_output_error(app, app_open_book_preview(app, selected_path));
                if (book_error == ERROR_NONE) {
                    app_file_manager_clear_listing(app);
                } else {
                    app->needs_redraw = TRUE;
                    app->needs_screen_clear = TRUE;
                    if (book_error != ERROR_OUTPUT_IO)
                        record_output_error(app, app_render_file_manager(app));
                }
                break;
            }

            if (!app_file_manager_has_images(app)) {
                break;
            }

            PixelTermApp staged = *app;
            staged.image_files = g_list_copy_deep(app->image_files, copy_media_path, NULL);
            staged.current_directory = g_strdup(app->current_directory);
            staged.preview.selected_link = NULL;
            staged.preview.selected_link_index = -1;
            /* Directory loading destroys its preloader; keep live work until render commits. */
            staged.preloader = NULL;
            staged.preload_enabled = FALSE;
            ErrorCode load_error = record_output_error(app, app_load_directory(&staged, app->file_manager.directory));
            if (load_error != ERROR_NONE) {
                g_list_free_full(staged.image_files, g_free);
                g_free(staged.current_directory);
                if (load_error != ERROR_OUTPUT_IO) record_output_error(app, app_render_file_manager(app));
                break;
            }

            gboolean selection_is_media = selected_path &&
                                          g_file_test(selected_path, G_FILE_TEST_IS_REGULAR) &&
                                          is_valid_media_file(selected_path);
            if (selection_is_media) {
                staged.return_to_mode = RETURN_MODE_PREVIEW;
                gint selected_image_index = -1;
                gint idx = 0;
                for (GList *cur = staged.image_files; cur; cur = cur->next, idx++) {
                    const gchar *image_path = (const gchar*)cur->data;
                    if (g_strcmp0(selected_path, image_path) == 0) {
                        selected_image_index = idx;
                        break;
                    }
                }
                if (selected_image_index >= 0) {
                    staged.current_index = selected_image_index;
                }
            } else {
                staged.return_to_mode = RETURN_MODE_PREVIEW_VIRTUAL;
                staged.file_manager.previous_selected_entry = app->file_manager.selected_entry;
            }
            ErrorCode entered = record_output_error(app, app_enter_preview(&staged));
            if (entered == ERROR_NONE) {
                if (!selection_is_media) staged.preview.selected = 0;
                entered = record_output_error(app, app_render_preview_grid(&staged));
            }
            if (entered != ERROR_NONE) {
                g_list_free_full(staged.image_files, g_free);
                g_free(staged.current_directory);
                app->needs_redraw = TRUE;
                app->needs_screen_clear = TRUE;
                if (entered != ERROR_OUTPUT_IO)
                    record_output_error(app, app_render_file_manager(app));
                break;
            }
            app_preloader_reset(app);
            g_list_free_full(app->image_files, g_free);
            g_free(app->current_directory);
            staged.preloader = app->preloader;
            staged.preload_enabled = app->preload_enabled;
            *app = staged;
            if (app->preload_enabled) (void)app_preloader_enable(app, TRUE);
            app_file_manager_clear_listing(app);
            break;
        }
        case KEY_ENTER:
        case 13:
            {
                ErrorCode error;
                input_flush_buffer(input_handler);
                error = record_output_error(app, app_file_manager_enter(app));
                if (error == ERROR_OUTPUT_IO) break;
                if (error == ERROR_NONE && app_is_file_manager_mode(app)) {
                    record_output_error(app, app_render_file_manager(app));
                }
            }
            break;
        case KEY_BACKSPACE:
        case 8:
            if (record_output_error(app, app_file_manager_toggle_hidden(app)) == ERROR_NONE) {
                record_output_error(app, app_render_file_manager(app));
            }
            break;
        default:
            if ((event->key_code >= 'A' && event->key_code <= 'Z') ||
                (event->key_code >= 'a' && event->key_code <= 'z')) {
                gint old_selected = app->file_manager.selected_entry;
                gint old_scroll = app->file_manager.scroll_offset;
                if (record_output_error(app, app_file_manager_jump_to_letter(app, (char)event->key_code)) == ERROR_OUTPUT_IO) break;
                if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
                    record_output_error(app, app_render_file_manager(app));
                }
            }
            break;
    }
}
