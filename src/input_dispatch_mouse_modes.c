#include "input_dispatch_mouse_modes_internal.h"

#include <math.h>

#include "input_dispatch_book_internal.h"
#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_media_internal.h"

static const gdouble k_image_zoom_step = 0.2;

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
    if (input_dispatch_current_is_video(app) || input_dispatch_current_is_animated_image(app)) {
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

void input_dispatch_handle_mouse_press_single(PixelTermApp *app, const InputEvent *event) {
    if (event->mouse_button == MOUSE_BUTTON_LEFT && input_dispatch_current_is_video(app)) {
        input_dispatch_key_modes_toggle_video_playback(app);
        app->input.single_click.pending = FALSE;
        return;
    }
    app->input.single_click.pending = TRUE;
    app->input.single_click.pending_time = g_get_monotonic_time();
}

void input_dispatch_handle_mouse_press_preview(PixelTermApp *app, const InputEvent *event) {
    app->input.preview_click.pending = TRUE;
    app->input.preview_click.pending_time = g_get_monotonic_time();
    app->input.preview_click.x = event->mouse_x;
    app->input.preview_click.y = event->mouse_y;
}

void input_dispatch_handle_mouse_press_file_manager(PixelTermApp *app, const InputEvent *event) {
    app->input.file_manager_click.pending = TRUE;
    app->input.file_manager_click.pending_time = g_get_monotonic_time();
    app->input.file_manager_click.x = event->mouse_x;
    app->input.file_manager_click.y = event->mouse_y;
}

void input_dispatch_handle_mouse_press_book(PixelTermApp *app, const InputEvent *event) {
    (void)event;
    app->input.single_click.pending = TRUE;
    app->input.single_click.pending_time = g_get_monotonic_time();
}

void input_dispatch_handle_mouse_double_click_single(PixelTermApp *app, const InputEvent *event) {
    (void)event;
    app->input.single_click.pending = FALSE;

    if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
        app->return_to_mode = RETURN_MODE_PREVIEW;
    }
    if (app_enter_preview(app) == ERROR_NONE) {
        app_render_preview_grid(app);
    }
}

void input_dispatch_handle_mouse_double_click_preview(PixelTermApp *app, const InputEvent *event) {
    app->input.preview_click.pending = FALSE;

    gboolean redraw_needed = FALSE;
    gboolean hit = FALSE;
    app_handle_mouse_click_preview(app, event->mouse_x, event->mouse_y, &redraw_needed, &hit);
    if (!hit) {
        return;
    }

    if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
        app->return_to_mode = RETURN_MODE_PREVIEW;
    }
    (void)app_transition_mode(app, APP_MODE_SINGLE);
    app_render_current_image(app);
}

void input_dispatch_handle_mouse_double_click_book_preview(PixelTermApp *app, const InputEvent *event) {
    app->input.preview_click.pending = FALSE;
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

void input_dispatch_handle_mouse_double_click_file_manager(PixelTermApp *app, const InputEvent *event) {
    app->input.file_manager_click.pending = FALSE;
    ErrorCode err = app_file_manager_enter_at_position(app, event->mouse_x, event->mouse_y);
    if (err == ERROR_NONE && app_is_file_manager_mode(app)) {
        app_render_file_manager(app);
    }
}

void input_dispatch_handle_mouse_double_click_book(PixelTermApp *app, const InputEvent *event) {
    (void)event;
    app->input.single_click.pending = FALSE;
    if (app_enter_book_preview(app) == ERROR_NONE) {
        app_render_book_preview(app);
    }
}

void input_dispatch_handle_mouse_scroll_single(PixelTermApp *app, const InputEvent *event) {
    if (input_dispatch_current_is_video(app)) {
        return;
    }
    if (input_dispatch_current_is_animated_image(app)) {
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

void input_dispatch_handle_mouse_scroll_preview(PixelTermApp *app, const InputEvent *event) {
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

void input_dispatch_handle_mouse_scroll_book_preview(PixelTermApp *app, const InputEvent *event) {
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

void input_dispatch_handle_mouse_scroll_file_manager(PixelTermApp *app, const InputEvent *event) {
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

void input_dispatch_handle_mouse_scroll_book(PixelTermApp *app, const InputEvent *event) {
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        gint page_step = app_book_use_double_page(app) ? 2 : 1;
        input_dispatch_book_change_page(app, -page_step);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        gint page_step = app_book_use_double_page(app) ? 2 : 1;
        input_dispatch_book_change_page(app, page_step);
    }
}
