#include <glib.h>

#include <string.h>

#include "app.h"
#include "input_dispatch_test_support.h"
#include "input_dispatch_delete_internal.h"

static InputEvent make_key_event(KeyCode key_code) {
    InputEvent event = {0};
    event.type = INPUT_KEY_PRESS;
    event.key_code = key_code;
    return event;
}

static void test_delete_request_arms_in_supported_single_mode(void) {
    PixelTermApp app = {0};
    InputEvent event = make_key_event((KeyCode)'r');

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_SINGLE;

    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_true(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
}

static void test_delete_request_ignored_in_unsupported_modes(void) {
    InputEvent event = make_key_event((KeyCode)'r');
    AppMode blocked_modes[] = {APP_MODE_FILE_MANAGER, APP_MODE_BOOK, APP_MODE_BOOK_PREVIEW};

    for (guint i = 0; i < G_N_ELEMENTS(blocked_modes); i++) {
        PixelTermApp app = {0};
        input_dispatch_test_reset_stubs();
        app.mode = blocked_modes[i];

        g_assert_false(input_dispatch_handle_delete_request(&app, &event));
        g_assert_false(app.delete_pending);
        g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
    }
}

static void test_delete_request_cancels_pending_on_other_key(void) {
    PixelTermApp app = {0};
    InputEvent arm = make_key_event((KeyCode)'r');
    InputEvent cancel = make_key_event(KEY_ESCAPE);

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_SINGLE;

    g_assert_true(input_dispatch_handle_delete_request(&app, &arm));
    g_assert_true(app.delete_pending);

    g_assert_false(input_dispatch_handle_delete_request(&app, &cancel));
    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
}

static void test_delete_request_executes_single_mode_delete_on_second_r(void) {
    PixelTermApp app = {0};
    InputEvent event = make_key_event((KeyCode)'r');

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_SINGLE;
    app.total_images = 3;
    app.current_index = 1;

    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_true(input_dispatch_handle_delete_request(&app, &event));

    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.render_by_mode_calls, ==, 1);
}

static void test_delete_request_executes_preview_delete_on_second_r(void) {
    PixelTermApp app = {0};
    InputEvent event = make_key_event((KeyCode)'r');

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_PREVIEW;
    app.total_images = 4;
    app.current_index = 0;
    app.preview.selected = 2;

    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_true(input_dispatch_handle_delete_request(&app, &event));

    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 1);
    g_assert_cmpint(app.current_index, ==, 2);
    g_assert_cmpint(app.preview.selected, ==, 2);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, 1);
}

static void test_delete_request_clears_pending_when_mode_becomes_unsupported(void) {
    PixelTermApp app = {0};
    InputEvent arm = make_key_event((KeyCode)'r');
    InputEvent followup = make_key_event(KEY_ESCAPE);

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_SINGLE;

    g_assert_true(input_dispatch_handle_delete_request(&app, &arm));
    g_assert_true(app.delete_pending);

    app.mode = APP_MODE_FILE_MANAGER;
    g_assert_false(input_dispatch_handle_delete_request(&app, &followup));
    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
}

void register_input_dispatch_delete_tests(void) {
    g_test_add_func("/input_dispatch_delete/request/arms_in_supported_single_mode",
                    test_delete_request_arms_in_supported_single_mode);
    g_test_add_func("/input_dispatch_delete/request/ignored_in_unsupported_modes",
                    test_delete_request_ignored_in_unsupported_modes);
    g_test_add_func("/input_dispatch_delete/request/cancels_pending_on_other_key",
                    test_delete_request_cancels_pending_on_other_key);
    g_test_add_func("/input_dispatch_delete/request/executes_single_mode_delete_on_second_r",
                    test_delete_request_executes_single_mode_delete_on_second_r);
    g_test_add_func("/input_dispatch_delete/request/executes_preview_delete_on_second_r",
                    test_delete_request_executes_preview_delete_on_second_r);
    g_test_add_func("/input_dispatch_delete/request/clears_pending_when_mode_becomes_unsupported",
                    test_delete_request_clears_pending_when_mode_becomes_unsupported);
}
