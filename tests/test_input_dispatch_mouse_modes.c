#include <glib.h>

#include "input_dispatch_mouse_modes_internal.h"
#include "input_dispatch_test_support.h"

static InputEvent make_double_click_event(gint mouse_x, gint mouse_y) {
    InputEvent event = {0};
    event.type = INPUT_MOUSE_PRESS;
    event.mouse_x = mouse_x;
    event.mouse_y = mouse_y;
    event.mouse_button = MOUSE_BUTTON_LEFT;
    return event;
}

static void test_file_manager_double_click_rerenders_when_mode_stays_file_manager(void) {
    PixelTermApp app = {0};
    InputEvent event = make_double_click_event(12, 7);

    app.mode = APP_MODE_FILE_MANAGER;
    app.input.file_manager_click.pending = TRUE;

    input_dispatch_test_reset_stubs();

    input_dispatch_handle_mouse_double_click_file_manager(&app, &event);

    g_assert_false(app.input.file_manager_click.pending);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_at_position_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_mouse_x, ==, 12);
    g_assert_cmpint(g_input_dispatch_stub_state.last_mouse_y, ==, 7);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 1);
}

static void test_file_manager_double_click_skips_rerender_after_mode_change(void) {
    PixelTermApp app = {0};
    InputEvent event = make_double_click_event(3, 9);

    app.mode = APP_MODE_FILE_MANAGER;
    app.input.file_manager_click.pending = TRUE;

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.enter_at_position_mode = APP_MODE_BOOK_PREVIEW;

    input_dispatch_handle_mouse_double_click_file_manager(&app, &event);

    g_assert_false(app.input.file_manager_click.pending);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_at_position_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 0);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK_PREVIEW);
}

void register_input_dispatch_mouse_modes_tests(void) {
    g_test_add_func("/input_dispatch_mouse_modes/file_manager_double_click/rerenders_when_mode_stays_file_manager",
                    test_file_manager_double_click_rerenders_when_mode_stays_file_manager);
    g_test_add_func("/input_dispatch_mouse_modes/file_manager_double_click/skips_rerender_after_mode_change",
                    test_file_manager_double_click_skips_rerender_after_mode_change);
}
