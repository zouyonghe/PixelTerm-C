#include <glib.h>

#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_mouse_modes_internal.h"
#include "input_dispatch_test_support.h"

static PixelTermApp make_single_app(VideoPlayer *player) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_SINGLE;
    app.video_player = player;
    return app;
}

static InputEvent make_scroll_event(MouseButton button) {
    InputEvent event = {0};
    event.type = INPUT_MOUSE_SCROLL;
    event.mouse_button = button;
    return event;
}

static void test_video_scroll_up_seeks_forward(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_scroll_event(MOUSE_SCROLL_UP);
    gint64 seek_step_ms = input_dispatch_key_single_get_video_seek_step_ms_for_test();

    input_dispatch_test_reset_stubs();
    player.has_video = TRUE;
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_mouse_scroll_single(&app, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, seek_step_ms);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_scroll_down_seeks_backward(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_scroll_event(MOUSE_SCROLL_DOWN);
    gint64 seek_step_ms = input_dispatch_key_single_get_video_seek_step_ms_for_test();

    input_dispatch_test_reset_stubs();
    player.has_video = TRUE;
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_mouse_scroll_single(&app, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, -seek_step_ms);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_scroll_ignores_non_scroll_event_type(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = {0};

    event.type = INPUT_MOUSE_PRESS;
    event.mouse_button = MOUSE_SCROLL_UP;

    input_dispatch_test_reset_stubs();
    player.has_video = TRUE;
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_mouse_scroll_single(&app, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

void register_input_dispatch_mouse_modes_tests(void) {
    g_test_add_func("/input_dispatch_mouse_modes/video/scroll_up_seeks_forward",
                    test_video_scroll_up_seeks_forward);
    g_test_add_func("/input_dispatch_mouse_modes/video/scroll_down_seeks_backward",
                    test_video_scroll_down_seeks_backward);
    g_test_add_func("/input_dispatch_mouse_modes/video/ignores_non_scroll_event_type",
                    test_video_scroll_ignores_non_scroll_event_type);
}
