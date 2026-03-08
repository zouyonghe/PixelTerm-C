#include <glib.h>

#include <string.h>

#include "app.h"
#include "input_dispatch_pending_clicks_internal.h"

typedef struct {
    gint preview_click_calls;
    gint preview_render_calls;
    gint preview_selection_render_calls;
    gint book_preview_click_calls;
    gint book_preview_render_calls;
    gint book_preview_selection_render_calls;
    gint file_manager_click_calls;
    gint file_manager_render_calls;
    gint next_image_calls;
    gint refresh_display_calls;
    gint book_change_page_calls;
} PendingClickStubState;

static PendingClickStubState g_stub_state;

static void reset_stub_state(void) {
    memset(&g_stub_state, 0, sizeof(g_stub_state));
}

gboolean app_book_use_double_page(const PixelTermApp *app) {
    (void)app;
    return FALSE;
}

void input_dispatch_book_change_page(PixelTermApp *app, gint delta) {
    (void)app;
    (void)delta;
    g_stub_state.book_change_page_calls++;
}

ErrorCode app_next_image(PixelTermApp *app) {
    if (app) {
        app->needs_redraw = FALSE;
    }
    g_stub_state.next_image_calls++;
    return ERROR_NONE;
}

ErrorCode app_refresh_display(PixelTermApp *app) {
    (void)app;
    g_stub_state.refresh_display_calls++;
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
    g_stub_state.book_preview_click_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_book_preview(PixelTermApp *app) {
    (void)app;
    g_stub_state.book_preview_render_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_book_preview_selection_change(PixelTermApp *app, gint old_index) {
    (void)app;
    (void)old_index;
    g_stub_state.book_preview_selection_render_calls++;
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
    g_stub_state.preview_click_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_preview_grid(PixelTermApp *app) {
    (void)app;
    g_stub_state.preview_render_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_preview_selection_change(PixelTermApp *app, gint old_index) {
    (void)app;
    (void)old_index;
    g_stub_state.preview_selection_render_calls++;
    return ERROR_NONE;
}

ErrorCode app_handle_mouse_file_manager(PixelTermApp *app, gint mouse_x, gint mouse_y) {
    (void)app;
    (void)mouse_x;
    (void)mouse_y;
    g_stub_state.file_manager_click_calls++;
    return ERROR_NONE;
}

ErrorCode app_render_file_manager(PixelTermApp *app) {
    (void)app;
    g_stub_state.file_manager_render_calls++;
    return ERROR_NONE;
}

static void test_preview_click_clears_when_mode_changes_without_hooks(void) {
    PixelTermApp app = {0};

    reset_stub_state();
    app.mode = APP_MODE_PREVIEW;
    app.input.preview_click.pending = TRUE;
    app.input.preview_click.pending_time = g_get_monotonic_time();
    app.input.preview_click.x = 12;
    app.input.preview_click.y = 7;

    app_set_mode(&app, APP_MODE_SINGLE);
    g_assert_true(app.input.preview_click.pending);

    input_dispatch_process_pending_clicks(&app);

    g_assert_false(app.input.preview_click.pending);
    g_assert_cmpint(g_stub_state.preview_click_calls, ==, 0);
}

static void test_preview_click_does_not_replay_after_mode_round_trip(void) {
    PixelTermApp app = {0};

    reset_stub_state();
    app.mode = APP_MODE_PREVIEW;
    app.input.preview_click.pending = TRUE;
    app.input.preview_click.pending_time = g_get_monotonic_time();
    app.input.preview_click.x = 3;
    app.input.preview_click.y = 9;

    app_set_mode(&app, APP_MODE_SINGLE);
    input_dispatch_process_pending_clicks(&app);

    app_set_mode(&app, APP_MODE_PREVIEW);
    app.input.preview_click.pending_time = g_get_monotonic_time() - 500000;
    input_dispatch_process_pending_clicks(&app);

    g_assert_false(app.input.preview_click.pending);
    g_assert_cmpint(g_stub_state.preview_click_calls, ==, 0);
    g_assert_cmpint(g_stub_state.preview_render_calls, ==, 0);
    g_assert_cmpint(g_stub_state.preview_selection_render_calls, ==, 0);
}

void register_input_dispatch_pending_clicks_tests(void) {
    g_test_add_func("/input_dispatch_pending_clicks/preview_click/clears_when_mode_changes_without_hooks",
                    test_preview_click_clears_when_mode_changes_without_hooks);
    g_test_add_func("/input_dispatch_pending_clicks/preview_click/does_not_replay_after_mode_round_trip",
                    test_preview_click_does_not_replay_after_mode_round_trip);
}
