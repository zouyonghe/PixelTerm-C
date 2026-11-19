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

static void test_preview_click_processes_new_pending_after_mode_round_trip(void) {
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
    app.input.preview_click.pending = TRUE;
    app.input.preview_click.pending_time = g_get_monotonic_time() - 500000;
    input_dispatch_process_pending_clicks(&app);

    g_assert_false(app.input.preview_click.pending);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_click_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_selection_render_calls, ==, 0);
}

static void test_pending_preview_render_output_failure(void) {
    PixelTermApp app = {.mode = APP_MODE_PREVIEW};
    input_dispatch_test_reset_stubs();
    app.input.preview_click.pending = TRUE;
    app.input.preview_click.pending_time = g_get_monotonic_time() - 500000;
    app.input.file_manager_click.pending = TRUE;
    app.input.file_manager_click.pending_time = app.input.preview_click.pending_time;
    app.file_manager.selected_entry = 7;
    g_input_dispatch_stub_state.preview_click_changes_selection = TRUE;
    g_input_dispatch_stub_state.preview_selection_render_result = ERROR_OUTPUT_IO;
    input_dispatch_process_pending_clicks(&app);
    g_assert_false(app.input.preview_click.pending);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_click_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_selection_render_calls, ==, 1);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_true(app.input.file_manager_click.pending);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 7);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_click_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 0);
}

static void test_pending_preview_validation_remains_nonfatal(void) {
    PixelTermApp app = {.mode = APP_MODE_PREVIEW};
    input_dispatch_test_reset_stubs();
    app.input.preview_click.pending = TRUE;
    app.input.preview_click.pending_time = g_get_monotonic_time() - 500000;
    app.input.file_manager_click.pending = TRUE;
    g_input_dispatch_stub_state.preview_click_changes_selection = TRUE;
    g_input_dispatch_stub_state.preview_selection_render_result = ERROR_INVALID_IMAGE;
    input_dispatch_process_pending_clicks(&app);
    g_assert_cmpint(app.last_error, ==, ERROR_NONE);
    g_assert_false(app.input.file_manager_click.pending);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_selection_render_calls, ==, 1);
}

static void test_pending_single_output_failure_preserves_other_clicks(void) {
    PixelTermApp app = {.mode = APP_MODE_SINGLE, .total_images = 2};
    input_dispatch_test_reset_stubs();
    app.input.single_click.pending = TRUE;
    app.input.single_click.pending_time = g_get_monotonic_time() - 500000;
    app.input.preview_click.pending = TRUE;
    app.input.file_manager_click.pending = TRUE;
    g_input_dispatch_stub_state.next_image_result = ERROR_OUTPUT_IO;
    input_dispatch_process_pending_clicks(&app);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_false(app.input.single_click.pending);
    g_assert_true(app.input.preview_click.pending);
    g_assert_true(app.input.file_manager_click.pending);
    g_assert_cmpint(app.current_index, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_click_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_click_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
}

static void test_pending_book_current_result(gconstpointer data) {
    ErrorCode result = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = APP_MODE_BOOK,
                       .last_error = result == ERROR_NONE ? ERROR_OUTPUT_IO : ERROR_NONE};
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.book_change_page_result = result;
    app.input.single_click.pending = TRUE;
    app.input.single_click.pending_time = g_get_monotonic_time() - 500000;
    app.input.preview_click.pending = TRUE;
    app.input.file_manager_click.pending = TRUE;
    input_dispatch_process_pending_clicks(&app);
    g_assert_cmpint(g_input_dispatch_stub_state.book_change_page_calls, ==, 1);
    g_assert_false(app.input.single_click.pending);
    g_assert_cmpint(app.input.preview_click.pending, ==, result == ERROR_OUTPUT_IO);
    g_assert_cmpint(app.input.file_manager_click.pending, ==, result == ERROR_OUTPUT_IO);
    g_assert_cmpint(app.last_error, ==,
                    result == ERROR_INVALID_IMAGE ? ERROR_NONE : ERROR_OUTPUT_IO);
}

void register_input_dispatch_pending_clicks_tests(void) {
    g_test_add_data_func("/input_dispatch_pending_clicks/book/prior_output_error", GINT_TO_POINTER(ERROR_NONE), test_pending_book_current_result);
    g_test_add_data_func("/input_dispatch_pending_clicks/book/current_output_error", GINT_TO_POINTER(ERROR_OUTPUT_IO), test_pending_book_current_result);
    g_test_add_data_func("/input_dispatch_pending_clicks/book/current_validation_error", GINT_TO_POINTER(ERROR_INVALID_IMAGE), test_pending_book_current_result);
    g_test_add_func("/input_dispatch_pending_clicks/single_click/output_failure_preserves_others", test_pending_single_output_failure_preserves_other_clicks);
    g_test_add_func("/input_dispatch_pending_clicks/preview_click/validation_nonfatal", test_pending_preview_validation_remains_nonfatal);
    g_test_add_func("/input_dispatch_pending_clicks/preview_click/render_output_failure", test_pending_preview_render_output_failure);
    g_test_add_func("/input_dispatch_pending_clicks/preview_click/clears_when_mode_changes_without_hooks",
                    test_preview_click_clears_when_mode_changes_without_hooks);
    g_test_add_func("/input_dispatch_pending_clicks/preview_click/processes_new_pending_after_mode_round_trip",
                    test_preview_click_processes_new_pending_after_mode_round_trip);
}
