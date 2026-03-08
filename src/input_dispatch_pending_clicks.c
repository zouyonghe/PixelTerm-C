#include "input_dispatch_pending_clicks_internal.h"

#include <glib.h>

#include "input_dispatch_book_internal.h"

static const gint64 k_click_threshold_us = 400000;

void input_dispatch_process_pending_clicks(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (app->book.toc_visible) {
        app->input.single_click.pending = FALSE;
        app->input.preview_click.pending = FALSE;
        return;
    }

    if (app->input.single_click.pending &&
        (app_is_single_mode(app) || app_is_book_mode(app))) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->input.single_click.pending_time > k_click_threshold_us) {
            app->input.single_click.pending = FALSE;
            if (app_is_book_mode(app)) {
                gint page_step = app_book_use_double_page(app) ? 2 : 1;
                input_dispatch_book_change_page(app, page_step);
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
    } else if (app->input.single_click.pending) {
        app->input.single_click.pending = FALSE;
    }

    if (app->input.preview_click.pending &&
        (app_is_book_preview_mode(app) || app_is_preview_mode(app))) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->input.preview_click.pending_time > k_click_threshold_us) {
            app->input.preview_click.pending = FALSE;
            gboolean redraw_needed = FALSE;
            if (app_is_book_preview_mode(app)) {
                gint old_selected = app->book.preview_selected;
                gint old_scroll = app->book.preview_scroll;
                app_handle_mouse_click_book_preview(app,
                                                    app->input.preview_click.x,
                                                    app->input.preview_click.y,
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
                                               app->input.preview_click.x,
                                               app->input.preview_click.y,
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
    } else if (app->input.preview_click.pending) {
        app->input.preview_click.pending = FALSE;
    }

    if (app_is_file_manager_mode(app) && app->input.file_manager_click.pending) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->input.file_manager_click.pending_time > k_click_threshold_us) {
            app->input.file_manager_click.pending = FALSE;
            gint old_selected = app->file_manager.selected_entry;
            gint old_scroll = app->file_manager.scroll_offset;
            app_handle_mouse_file_manager(app,
                                          app->input.file_manager_click.x,
                                          app->input.file_manager_click.y);
            if (app->file_manager.selected_entry != old_selected || app->file_manager.scroll_offset != old_scroll) {
                app_render_file_manager(app);
            }
        }
    } else if (!app_is_file_manager_mode(app) && app->input.file_manager_click.pending) {
        app->input.file_manager_click.pending = FALSE;
    }
}
