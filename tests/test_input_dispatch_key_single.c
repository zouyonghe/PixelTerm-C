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

static void assert_previous_navigation(KeyCode key_code) {
    PixelTermApp app = make_single_app(NULL);
    InputEvent event = make_key_event(key_code);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = FALSE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(app.current_index, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 1);
    g_assert_true(app.suppress_full_clear);
    g_assert_true(app.async.render_request);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void assert_next_navigation(KeyCode key_code) {
    PixelTermApp app = make_single_app(NULL);
    InputEvent event = make_key_event(key_code);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = FALSE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(app.current_index, ==, 2);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 1);
    g_assert_true(app.suppress_full_clear);
    g_assert_true(app.async.render_request);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_left_matches_h_navigation(void) {
    assert_previous_navigation(KEY_LEFT);
    assert_previous_navigation((KeyCode)'h');
}

static void test_right_matches_l_navigation(void) {
    assert_next_navigation(KEY_RIGHT);
    assert_next_navigation((KeyCode)'l');
}

static void test_video_left_does_not_switch_media(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event(KEY_LEFT);
    gint64 seek_step_ms = input_dispatch_key_single_get_video_seek_step_ms_for_test();

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, -seek_step_ms);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_right_does_not_switch_media(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event(KEY_RIGHT);
    gint64 seek_step_ms = input_dispatch_key_single_get_video_seek_step_ms_for_test();

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, seek_step_ms);
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

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_navigation_failure_does_not_refresh_or_advance_queue(void) {
    PixelTermApp app = make_single_app(NULL);
    InputEvent event = make_key_event(KEY_RIGHT);
    InputEvent queued = make_key_event(KEY_RIGHT);
    InputHandler input_handler = {0};

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = FALSE;
    g_input_dispatch_stub_state.next_image_result = ERROR_INVALID_IMAGE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);
    input_unget_event(&input_handler, &queued);

    input_dispatch_handle_key_press_single(&app, &input_handler, &event);

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    g_assert_false(app.suppress_full_clear);
    g_assert_false(app.async.render_request);
    InputEvent preserved = {0};
    g_assert_cmpint(input_get_event(&input_handler, &preserved), ==, ERROR_NONE);
    g_assert_cmpint(preserved.key_code, ==, KEY_RIGHT);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

void register_input_dispatch_key_single_tests(void) {
    g_test_add_func("/input_dispatch_key_single/navigation/left_matches_h",
                    test_left_matches_h_navigation);
    g_test_add_func("/input_dispatch_key_single/navigation/right_matches_l",
                    test_right_matches_l_navigation);
    g_test_add_func("/input_dispatch_key_single/video/left_does_not_switch_media",
                    test_video_left_does_not_switch_media);
    g_test_add_func("/input_dispatch_key_single/video/right_does_not_switch_media",
                    test_video_right_does_not_switch_media);
    g_test_add_func("/input_dispatch_key_single/video/up_and_down_keep_media_switching",
                    test_video_up_and_down_keep_media_switching);
    g_test_add_func("/input_dispatch_key_single/navigation/failure_does_not_refresh_or_advance_queue",
                    test_navigation_failure_does_not_refresh_or_advance_queue);
}
