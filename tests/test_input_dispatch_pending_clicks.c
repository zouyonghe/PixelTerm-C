#include <glib.h>

#include <string.h>

#include "app.h"
#include "input_dispatch_test_support.h"
#include "input_dispatch_pending_clicks_internal.h"

static void test_preview_click_clears_when_mode_changes_without_hooks(void) {
    PixelTermApp app = {0};

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_PREVIEW;
    app.input.preview_click.pending = TRUE;
    app.input.preview_click.pending_time = g_get_monotonic_time();
    app.input.preview_click.x = 12;
    app.input.preview_click.y = 7;

    app_set_mode(&app, APP_MODE_SINGLE);
    g_assert_true(app.input.preview_click.pending);

    input_dispatch_process_pending_clicks(&app);

    g_assert_false(app.input.preview_click.pending);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_click_calls, ==, 0);
}

static void test_preview_click_does_not_replay_after_mode_round_trip(void) {
    PixelTermApp app = {0};

    input_dispatch_test_reset_stubs();
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
    g_assert_cmpint(g_input_dispatch_stub_state.preview_click_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_selection_render_calls, ==, 0);
}

void register_input_dispatch_pending_clicks_tests(void) {
    g_test_add_func("/input_dispatch_pending_clicks/preview_click/clears_when_mode_changes_without_hooks",
                    test_preview_click_clears_when_mode_changes_without_hooks);
    g_test_add_func("/input_dispatch_pending_clicks/preview_click/does_not_replay_after_mode_round_trip",
                    test_preview_click_does_not_replay_after_mode_round_trip);
}
