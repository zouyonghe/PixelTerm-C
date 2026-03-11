#include "input_dispatch_test_support.h"

#include <string.h>

InputDispatchTestStubState g_input_dispatch_stub_state;

void input_dispatch_test_reset_stubs(void) {
    memset(&g_input_dispatch_stub_state, 0, sizeof(g_input_dispatch_stub_state));
    g_input_dispatch_stub_state.delete_result = ERROR_NONE;
    g_input_dispatch_stub_state.enter_file_manager_result = ERROR_NONE;
    g_input_dispatch_stub_state.enter_preview_result = ERROR_NONE;
    g_input_dispatch_stub_state.video_seek_result = ERROR_NONE;
    g_input_dispatch_stub_state.next_image_result = ERROR_NONE;
    g_input_dispatch_stub_state.previous_image_result = ERROR_NONE;
}

gboolean input_dispatch_current_is_video(const PixelTermApp *app) {
    (void)app;
    return g_input_dispatch_stub_state.current_is_video;
}

gboolean input_dispatch_current_is_animated_image(const PixelTermApp *app) {
    (void)app;
    return FALSE;
}

ErrorCode input_dispatch_test_video_seek(VideoPlayer *player, gint64 delta_ms) {
    (void)player;
    g_input_dispatch_stub_state.video_seek_calls++;
    g_input_dispatch_stub_state.last_video_seek_delta_ms = delta_ms;
    g_input_dispatch_stub_state.video_seek_total_delta_ms += delta_ms;
    return g_input_dispatch_stub_state.video_seek_result;
}

gboolean app_book_use_double_page(const PixelTermApp *app) {
    (void)app;
    return FALSE;
}

void input_dispatch_book_change_page(PixelTermApp *app, gint delta) {
    (void)app;
    (void)delta;
    g_input_dispatch_stub_state.book_change_page_calls++;
}

ErrorCode app_next_image(PixelTermApp *app) {
    ErrorCode result = g_input_dispatch_stub_state.next_image_result;
    if (app) {
        app->needs_redraw = FALSE;
        if (result == ERROR_NONE && app->current_index + 1 < app->total_images) {
            app->current_index++;
        }
    }
    g_input_dispatch_stub_state.next_image_calls++;
    return result;
}

ErrorCode app_previous_image(PixelTermApp *app) {
    ErrorCode result = g_input_dispatch_stub_state.previous_image_result;
    if (app) {
        app->needs_redraw = FALSE;
        if (result == ERROR_NONE && app->current_index > 0) {
            app->current_index--;
        }
    }
    g_input_dispatch_stub_state.previous_image_calls++;
    return result;
}

ErrorCode app_refresh_display(PixelTermApp *app) {
    (void)app;
    g_input_dispatch_stub_state.refresh_display_calls++;
    return ERROR_NONE;
}

ErrorCode app_handle_mouse_click_book_preview(PixelTermApp *app,
                                              gint mouse_x,
                                              gint mouse_y,
                                              gboolean *redraw_needed,
                                              gboolean *out_hit) {
    (void)app;
    (void)mouse_x;
    (void)mouse_y;
    if (redraw_needed) {
        *redraw_needed = FALSE;
    }
    if (out_hit) {
        *out_hit = TRUE;
    }
    g_input_dispatch_stub_state.book_preview_click_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_book_preview(PixelTermApp *app) {
    (void)app;
    g_input_dispatch_stub_state.book_preview_render_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_book_preview_selection_change(PixelTermApp *app, gint old_index) {
    (void)app;
    (void)old_index;
    g_input_dispatch_stub_state.book_preview_selection_render_calls++;
    return ERROR_NONE;
}

ErrorCode app_handle_mouse_click_preview(PixelTermApp *app,
                                         gint mouse_x,
                                         gint mouse_y,
                                         gboolean *redraw_needed,
                                         gboolean *out_hit) {
    (void)app;
    (void)mouse_x;
    (void)mouse_y;
    if (redraw_needed) {
        *redraw_needed = FALSE;
    }
    if (out_hit) {
        *out_hit = TRUE;
    }
    g_input_dispatch_stub_state.preview_click_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_preview_grid(PixelTermApp *app) {
    (void)app;
    g_input_dispatch_stub_state.preview_render_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_preview_selection_change(PixelTermApp *app, gint old_index) {
    (void)app;
    (void)old_index;
    g_input_dispatch_stub_state.preview_selection_render_calls++;
    return ERROR_NONE;
}

ErrorCode app_handle_mouse_file_manager(PixelTermApp *app, gint mouse_x, gint mouse_y) {
    (void)app;
    (void)mouse_x;
    (void)mouse_y;
    g_input_dispatch_stub_state.file_manager_click_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_file_manager(PixelTermApp *app) {
    (void)app;
    g_input_dispatch_stub_state.file_manager_render_calls++;
    return ERROR_NONE;
}

ErrorCode app_delete_current_image(PixelTermApp *app) {
    g_input_dispatch_stub_state.delete_calls++;
    if (g_input_dispatch_stub_state.delete_result == ERROR_NONE && app && app->total_images > 0) {
        app->total_images--;
        if (app->total_images <= 0) {
            app->total_images = 0;
            app->image_files = NULL;
        }
        if (app->current_index >= app->total_images && app->current_index > 0) {
            app->current_index--;
        }
    }
    return g_input_dispatch_stub_state.delete_result;
}

gboolean app_has_images(const PixelTermApp *app) {
    return app && app->total_images > 0;
}

ErrorCode app_render_by_mode(PixelTermApp *app) {
    (void)app;
    g_input_dispatch_stub_state.render_by_mode_calls++;
    return ERROR_NONE;
}

ErrorCode app_enter_file_manager(PixelTermApp *app) {
    (void)app;
    g_input_dispatch_stub_state.enter_file_manager_calls++;
    return g_input_dispatch_stub_state.enter_file_manager_result;
}

ErrorCode app_enter_preview(PixelTermApp *app) {
    (void)app;
    g_input_dispatch_stub_state.enter_preview_calls++;
    return g_input_dispatch_stub_state.enter_preview_result;
}
