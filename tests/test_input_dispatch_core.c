#include <glib.h>

#include "app.h"
#include "input_dispatch_core.h"
#include "input_dispatch_test_support.h"

typedef struct {
    gint *order;
    gint value;
    gint *index;
} OrderMarker;

static gboolean requeue_idle_source(gpointer user_data) {
    gint *call_count = (gint *)user_data;
    *call_count += 1;
    if (*call_count < 3) {
        g_idle_add(requeue_idle_source, user_data);
    }
    return G_SOURCE_REMOVE;
}

static void drain_default_main_context(void) {
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static gboolean ordered_idle_source(gpointer user_data) {
    OrderMarker *marker = (OrderMarker *)user_data;
    marker->order[*(marker->index)] = marker->value;
    *(marker->index) += 1;
    return G_SOURCE_REMOVE;
}

static InputEvent make_key_event(KeyCode key_code) {
    InputEvent event = {0};
    event.type = INPUT_KEY_PRESS;
    event.key_code = key_code;
    return event;
}

static InputEvent make_mouse_event(InputEventType type, MouseButton button) {
    InputEvent event = {0};
    event.type = type;
    event.mouse_button = button;
    event.mouse_x = 12;
    event.mouse_y = 6;
    return event;
}

static void test_process_animations_only_runs_one_iteration_per_call(void) {
    PixelTermApp app = {0};
    GifPlayer gif = {0};
    gint call_count = 0;

    drain_default_main_context();

    gif.is_playing = TRUE;
    app.gif_player = &gif;
    g_idle_add(requeue_idle_source, &call_count);

    input_dispatch_core_process_animations(&app);

    g_assert_cmpint(call_count, ==, 1);

    drain_default_main_context();
}

static void test_process_animations_skips_when_nothing_is_playing(void) {
    PixelTermApp app = {0};
    GifPlayer gif = {0};
    gint call_count = 0;

    drain_default_main_context();

    gif.is_playing = FALSE;
    app.gif_player = &gif;
    g_idle_add(requeue_idle_source, &call_count);

    input_dispatch_core_process_animations(&app);

    g_assert_cmpint(call_count, ==, 0);

    drain_default_main_context();
}

static void test_process_animations_defers_when_higher_priority_sources_are_pending(void) {
    PixelTermApp app = {0};
    GifPlayer gif = {0};
    gint order[2] = {0, 0};
    gint index = 0;
    OrderMarker animation = { .order = order, .value = 1, .index = &index };
    OrderMarker higher = { .order = order, .value = 2, .index = &index };

    drain_default_main_context();

    gif.is_playing = TRUE;
    app.gif_player = &gif;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, ordered_idle_source, &animation, NULL);
    g_idle_add_full(G_PRIORITY_DEFAULT, ordered_idle_source, &higher, NULL);

    input_dispatch_core_process_animations(&app);

    g_assert_cmpint(index, ==, 1);
    g_assert_cmpint(order[0], ==, 2);

    drain_default_main_context();
}

static void test_info_key_opens_info_overlay_in_single_mode(void) {
    PixelTermApp app = {0};
    InputHandler input_handler = {0};
    InputEvent event = make_key_event((KeyCode)'i');

    app.mode = APP_MODE_SINGLE;
    app.total_images = 1;

    input_dispatch_test_reset_stubs();
    input_dispatch_core_handle_event(&app, &input_handler, &event);

    g_assert_true(app.info_visible);
    g_assert_cmpint(g_input_dispatch_stub_state.display_image_info_calls, ==, 1);
}

static void test_info_key_hides_info_overlay_on_second_press(void) {
    PixelTermApp app = {0};
    InputHandler input_handler = {0};
    InputEvent event = make_key_event((KeyCode)'i');

    app.mode = APP_MODE_SINGLE;
    app.total_images = 1;
    app.info_visible = TRUE;

    input_dispatch_test_reset_stubs();
    input_dispatch_core_handle_event(&app, &input_handler, &event);

    g_assert_false(app.info_visible);
    g_assert_cmpint(g_input_dispatch_stub_state.display_image_info_calls, ==, 0);
}

static void test_info_key_opens_info_overlay_when_ui_text_hidden(void) {
    PixelTermApp app = {0};
    InputHandler input_handler = {0};
    InputEvent event = make_key_event((KeyCode)'i');

    app.mode = APP_MODE_SINGLE;
    app.total_images = 1;
    app.ui_text_hidden = TRUE;

    input_dispatch_test_reset_stubs();
    input_dispatch_core_handle_event(&app, &input_handler, &event);

    g_assert_true(app.info_visible);
    g_assert_cmpint(g_input_dispatch_stub_state.display_image_info_calls, ==, 1);
}

static void test_info_key_opens_info_overlay_for_video(void) {
    PixelTermApp app = {0};
    InputHandler input_handler = {0};
    InputEvent event = make_key_event((KeyCode)'i');

    app.mode = APP_MODE_SINGLE;
    app.total_images = 1;

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_core_handle_event(&app, &input_handler, &event);

    g_assert_true(app.info_visible);
    g_assert_cmpint(g_input_dispatch_stub_state.display_image_info_calls, ==, 1);
}

static void test_question_key_opens_help_overlay(void) {
    PixelTermApp app = {0};
    InputHandler input_handler = {0};
    InputEvent event = make_key_event((KeyCode)'?');

    app.mode = APP_MODE_SINGLE;
    app.total_images = 1;

    input_dispatch_test_reset_stubs();
    input_dispatch_core_handle_event(&app, &input_handler, &event);

    g_assert_true(app.help_visible);
    g_assert_cmpint(g_input_dispatch_stub_state.display_help_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.display_image_info_calls, ==, 0);
}

static void test_any_key_closes_help_overlay_without_mode_action(void) {
    PixelTermApp app = {0};
    InputHandler input_handler = {0};
    InputEvent event = make_key_event((KeyCode)'i');

    app.mode = APP_MODE_SINGLE;
    app.total_images = 1;
    app.help_visible = TRUE;

    input_dispatch_test_reset_stubs();
    input_dispatch_core_handle_event(&app, &input_handler, &event);

    g_assert_false(app.help_visible);
    g_assert_cmpint(g_input_dispatch_stub_state.display_help_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.display_image_info_calls, ==, 0);
}

static void test_mouse_closing_help_resets_input_timing_state(void) {
    PixelTermApp app = {0};
    InputHandler input_handler = {0};
    InputEvent event = make_mouse_event(INPUT_MOUSE_SCROLL, MOUSE_SCROLL_DOWN);

    app.mode = APP_MODE_SINGLE;
    app.total_images = 1;
    app.help_visible = TRUE;
    input_handler.last_click_time.tv_sec = 100;
    input_handler.last_click_time.tv_usec = 200;
    input_handler.last_click_x = 12;
    input_handler.last_click_y = 6;
    input_handler.last_click_button = MOUSE_BUTTON_LEFT;
    input_handler.last_scroll_time.tv_sec = 300;
    input_handler.last_scroll_time.tv_usec = 400;
    input_handler.last_scroll_button = MOUSE_SCROLL_DOWN;
    input_handler.last_scroll_x = 12;
    input_handler.last_scroll_y = 6;
    input_handler.ignore_input_until_us = 500;

    input_dispatch_test_reset_stubs();
    input_dispatch_core_handle_event(&app, &input_handler, &event);

    g_assert_false(app.help_visible);
    g_assert_cmpint(g_input_dispatch_stub_state.display_help_calls, ==, 1);
    g_assert_cmpint(input_handler.last_click_time.tv_sec, ==, 0);
    g_assert_cmpint(input_handler.last_click_time.tv_usec, ==, 0);
    g_assert_cmpint(input_handler.last_click_x, ==, 0);
    g_assert_cmpint(input_handler.last_click_y, ==, 0);
    g_assert_cmpint(input_handler.last_click_button, ==, 0);
    g_assert_cmpint(input_handler.last_scroll_time.tv_sec, ==, 0);
    g_assert_cmpint(input_handler.last_scroll_time.tv_usec, ==, 0);
    g_assert_cmpint(input_handler.last_scroll_button, ==, 0);
    g_assert_cmpint(input_handler.last_scroll_x, ==, 0);
    g_assert_cmpint(input_handler.last_scroll_y, ==, 0);
    g_assert_cmpint(input_handler.ignore_input_until_us, ==, 0);
}

void register_input_dispatch_core_tests(void) {
    g_test_add_func("/input_dispatch_core/process_animations/only_runs_one_iteration_per_call",
                    test_process_animations_only_runs_one_iteration_per_call);
    g_test_add_func("/input_dispatch_core/process_animations/skips_when_nothing_is_playing",
                    test_process_animations_skips_when_nothing_is_playing);
    g_test_add_func("/input_dispatch_core/process_animations/defers_when_higher_priority_sources_are_pending",
                    test_process_animations_defers_when_higher_priority_sources_are_pending);
    g_test_add_func("/input_dispatch_core/info_key/opens_info_overlay_in_single_mode",
                    test_info_key_opens_info_overlay_in_single_mode);
    g_test_add_func("/input_dispatch_core/info_key/hides_info_overlay_on_second_press",
                    test_info_key_hides_info_overlay_on_second_press);
    g_test_add_func("/input_dispatch_core/info_key/opens_info_overlay_when_ui_text_hidden",
                    test_info_key_opens_info_overlay_when_ui_text_hidden);
    g_test_add_func("/input_dispatch_core/info_key/opens_info_overlay_for_video",
                    test_info_key_opens_info_overlay_for_video);
    g_test_add_func("/input_dispatch_core/help_key/question_opens_help_overlay",
                    test_question_key_opens_help_overlay);
    g_test_add_func("/input_dispatch_core/help_key/any_key_closes_without_mode_action",
                    test_any_key_closes_help_overlay_without_mode_action);
    g_test_add_func("/input_dispatch_core/help_key/mouse_closing_help_resets_input_timing_state",
                    test_mouse_closing_help_resets_input_timing_state);
}
