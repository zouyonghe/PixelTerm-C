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
    g_assert_cmpint(g_input_dispatch_stub_state.display_image_info_calls, ==, 1);
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
    input_handler.last_click_button = MOUSE_BUTTON_MIDDLE;
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

static void test_pause_video_for_resize_records_playing_state(void) {
    PixelTermApp app = {0};
    VideoPlayer player = {0};

    app.mode = APP_MODE_SINGLE;
    app.video_player = &player;
    g_mutex_init(&player.state_mutex);
    player.has_video = TRUE;
    player.is_playing = TRUE;

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;

    input_dispatch_core_pause_video_for_resize(&app);

    g_assert_false(player.is_playing);
    g_assert_true(app.video_was_playing_before_resize);

    g_mutex_clear(&player.state_mutex);
}

static void test_pause_video_for_resize_does_not_record_paused_video(void) {
    PixelTermApp app = {0};
    VideoPlayer player = {0};

    app.mode = APP_MODE_SINGLE;
    app.video_player = &player;
    g_mutex_init(&player.state_mutex);
    player.has_video = TRUE;
    player.is_playing = FALSE;

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;

    input_dispatch_core_pause_video_for_resize(&app);

    g_assert_false(player.is_playing);
    g_assert_false(app.video_was_playing_before_resize);

    g_mutex_clear(&player.state_mutex);
}

static void test_pause_video_for_resize_keeps_playing_state_across_repeated_resize_events(void) {
    PixelTermApp app = {0};
    VideoPlayer player = {0};

    app.mode = APP_MODE_SINGLE;
    app.video_player = &player;
    g_mutex_init(&player.state_mutex);
    player.has_video = TRUE;
    player.is_playing = TRUE;

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;

    input_dispatch_core_pause_video_for_resize(&app);
    input_dispatch_core_pause_video_for_resize(&app);

    g_assert_false(player.is_playing);
    g_assert_true(app.video_was_playing_before_resize);

    g_mutex_clear(&player.state_mutex);
}


static void test_toc_double_click_entry_failure_aborts_render(void) {
    PixelTermApp app = {.mode = APP_MODE_BOOK, .book.toc_visible = TRUE};
    InputHandler handler = {0};
    InputEvent event = make_mouse_event(INPUT_MOUSE_DOUBLE_CLICK, MOUSE_BUTTON_LEFT);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.enter_book_page_result = ERROR_OUTPUT_IO;
    input_dispatch_core_handle_event(&app, &handler, &event);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_page_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.book_page_render_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.book_preview_render_calls, ==, 0);
}

static void test_common_key_render_error(gconstpointer data) {
    gint key = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = APP_MODE_SINGLE};
    InputHandler handler = {0};
    InputEvent event = make_key_event(key == 'I' ? (KeyCode)'i' : (KeyCode)key);
    app.help_visible = key == 'x';
    app.info_visible = key == 'I';
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.render_by_mode_result = ERROR_OUTPUT_IO;
    g_input_dispatch_stub_state.display_help_result = ERROR_OUTPUT_IO;
    g_input_dispatch_stub_state.display_image_info_result = ERROR_OUTPUT_IO;
    input_dispatch_core_handle_event(&app, &handler, &event);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    if (key == 'I') {
        g_assert_cmpint(g_input_dispatch_stub_state.display_image_info_calls, ==, 1);
        g_assert_true(app.info_visible);
    }
    if (key == 'x') g_assert_true(app.help_visible);
    if (key == '?') g_assert_false(app.help_visible);

    /* Successful redraws must not erase an earlier failure. */
    input_dispatch_test_reset_stubs();
    event = make_key_event((KeyCode)'d');
    app.help_visible = FALSE;
    input_dispatch_core_handle_event(&app, &handler, &event);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
}

static void test_preview_key_output_errors(gconstpointer data) {
    gint path = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = APP_MODE_PREVIEW};
    InputEvent event = make_key_event(path < 2 ? KEY_RIGHT : path == 2 ? KEY_ENTER : path == 3 ? KEY_TAB : (KeyCode)'+');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.preview_move_scroll = path == 0;
    g_input_dispatch_stub_state.preview_render_result = ERROR_OUTPUT_IO;
    g_input_dispatch_stub_state.preview_selection_render_result = ERROR_OUTPUT_IO;
    g_input_dispatch_stub_state.refresh_display_result = ERROR_OUTPUT_IO;
    g_input_dispatch_stub_state.enter_file_manager_result = ERROR_OUTPUT_IO;
    g_input_dispatch_stub_state.preview_zoom_result = ERROR_OUTPUT_IO;
    input_dispatch_core_handle_event(&app, NULL, &event);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    if (path == 0) g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, 1);
    if (path == 1) g_assert_cmpint(g_input_dispatch_stub_state.preview_selection_render_calls, ==, 1);
    if (path == 2) g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 1);
    if (path == 3) {
        g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 0);
        g_assert_cmpint(g_input_dispatch_stub_state.exit_preview_calls, ==, 0);
    }
    if (path == 2) {
        g_assert_cmpint(g_input_dispatch_stub_state.exit_preview_calls, ==, 1);
        g_assert_true(g_input_dispatch_stub_state.last_exit_preview_open_selected);
    }
    if (path == 4) {
        g_assert_cmpint(g_input_dispatch_stub_state.preview_zoom_calls, ==, 1);
        g_assert_cmpint(g_input_dispatch_stub_state.last_preview_zoom_delta, ==, 1);
    }
}

static void test_preview_navigation_queue(gconstpointer data) {
    static const KeyCode keys[] = {
        KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_PAGE_UP, KEY_PAGE_DOWN
    };
    gint scenario = GPOINTER_TO_INT(data);
    gboolean grid = (scenario / G_N_ELEMENTS(keys)) % 2 == 0;
    gboolean failure = scenario / (2 * G_N_ELEMENTS(keys)) == 0;
    PixelTermApp app = {.mode = APP_MODE_PREVIEW,
                        .last_error = failure ? ERROR_NONE : ERROR_OUTPUT_IO};
    InputHandler handler = {.has_pending_byte = TRUE, .pending_byte = 'q'};
    InputEvent event = make_key_event(keys[scenario % G_N_ELEMENTS(keys)]);
    InputEvent queued = make_key_event(KEY_RIGHT);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.preview_move_scroll = grid;
    g_input_dispatch_stub_state.preview_render_result = failure ? ERROR_OUTPUT_IO : ERROR_NONE;
    g_input_dispatch_stub_state.preview_selection_render_result = failure ? ERROR_OUTPUT_IO : ERROR_NONE;
    g_assert_cmpint(input_unget_event(&handler, &queued), ==, ERROR_NONE);

    input_dispatch_core_handle_event(&app, &handler, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, grid ? 1 : 0);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_selection_render_calls, ==, grid ? 0 : 1);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_true(handler.has_pending_event);
    g_assert_cmpint(handler.has_pending_byte, ==, failure);
    InputEvent remaining = {0};
    g_assert_cmpint(input_get_event(&handler, &remaining), ==, ERROR_NONE);
    g_assert_cmpint(remaining.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(remaining.key_code, ==, failure ? queued.key_code : (KeyCode)'q');
}

void register_input_dispatch_core_tests(void) {
    static const gchar *keys[] = {"left", "right", "up", "down", "page_up", "page_down"};
    for (guint result = 0; result < 2; result++) {
        for (guint render = 0; render < 2; render++) {
            for (guint key = 0; key < G_N_ELEMENTS(keys); key++) {
                gchar *path = g_strdup_printf("/input_dispatch_core/preview_queue/%s/%s/%s",
                                              result == 0 ? "failure" : "sticky_success",
                                              render == 0 ? "grid" : "selection", keys[key]);
                g_test_add_data_func(path, GUINT_TO_POINTER((result * 2 + render) * G_N_ELEMENTS(keys) + key),
                                     test_preview_navigation_queue);
                g_free(path);
            }
        }
    }
    g_test_add_data_func("/input_dispatch_core/preview_errors/grid", GINT_TO_POINTER(0), test_preview_key_output_errors);
    g_test_add_data_func("/input_dispatch_core/preview_errors/selection", GINT_TO_POINTER(1), test_preview_key_output_errors);
    g_test_add_data_func("/input_dispatch_core/preview_errors/refresh", GINT_TO_POINTER(2), test_preview_key_output_errors);
    g_test_add_data_func("/input_dispatch_core/preview_errors/entry", GINT_TO_POINTER(3), test_preview_key_output_errors);
    g_test_add_data_func("/input_dispatch_core/preview_errors/zoom", GINT_TO_POINTER(4), test_preview_key_output_errors);
    g_test_add_func("/input_dispatch_core/errors/toc_double_click_entry", test_toc_double_click_entry_failure_aborts_render);
    g_test_add_data_func("/input_dispatch_core/errors/dither", GINT_TO_POINTER('d'), test_common_key_render_error);
    g_test_add_data_func("/input_dispatch_core/errors/zen", GINT_TO_POINTER('~'), test_common_key_render_error);
    g_test_add_data_func("/input_dispatch_core/errors/help_open", GINT_TO_POINTER('?'), test_common_key_render_error);
    g_test_add_data_func("/input_dispatch_core/errors/info_open", GINT_TO_POINTER('i'), test_common_key_render_error);
    g_test_add_data_func("/input_dispatch_core/errors/help_close", GINT_TO_POINTER('x'), test_common_key_render_error);
    g_test_add_data_func("/input_dispatch_core/errors/info_close", GINT_TO_POINTER('I'), test_common_key_render_error);
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
    g_test_add_func("/input_dispatch_core/resize/pause_video_records_playing_state",
                    test_pause_video_for_resize_records_playing_state);
    g_test_add_func("/input_dispatch_core/resize/pause_video_does_not_record_paused_video",
                    test_pause_video_for_resize_does_not_record_paused_video);
    g_test_add_func("/input_dispatch_core/resize/pause_video_keeps_playing_state_across_repeated_resize_events",
                    test_pause_video_for_resize_keeps_playing_state_across_repeated_resize_events);
}
