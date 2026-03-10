#include <glib.h>

#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_test_support.h"

static PixelTermApp make_single_app(VideoPlayer *player) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_SINGLE;
    app.current_index = 1;
    app.total_images = 3;
    if (player) {
        player->has_video = TRUE;
    }
    app.video_player = player;
    return app;
}

static InputEvent make_key_event(KeyCode key_code) {
    InputEvent event = {0};
    event.type = INPUT_KEY_PRESS;
    event.key_code = key_code;
    return event;
}

static void test_video_left_does_not_switch_media(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event(KEY_LEFT);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, -5000);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_right_does_not_switch_media(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event(KEY_RIGHT);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, 5000);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_up_and_down_keep_media_switching(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent up_event = make_key_event(KEY_UP);
    InputEvent down_event = make_key_event(KEY_DOWN);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &up_event);
    input_dispatch_handle_key_press_single(&app, NULL, &down_event);

    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_non_video_left_and_right_keep_media_switching(void) {
    PixelTermApp app = make_single_app(NULL);
    InputEvent left_event = make_key_event(KEY_LEFT);
    InputEvent right_event = make_key_event(KEY_RIGHT);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = FALSE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &left_event);
    input_dispatch_handle_key_press_single(&app, NULL, &right_event);

    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

void register_input_dispatch_key_single_tests(void) {
    g_test_add_func("/input_dispatch_key_single/video/left_does_not_switch_media",
                    test_video_left_does_not_switch_media);
    g_test_add_func("/input_dispatch_key_single/video/right_does_not_switch_media",
                    test_video_right_does_not_switch_media);
    g_test_add_func("/input_dispatch_key_single/video/up_and_down_keep_media_switching",
                    test_video_up_and_down_keep_media_switching);
    g_test_add_func("/input_dispatch_key_single/non_video/left_and_right_keep_media_switching",
                    test_non_video_left_and_right_keep_media_switching);
}
