#include <glib.h>

#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_test_support.h"

#define input_dispatch_book_change_page real_book_change_page
#include "../src/input_dispatch_book.c"
#undef input_dispatch_book_change_page

static InputEvent make_key_event(KeyCode key_code) {
    InputEvent event = {0};
    event.type = INPUT_KEY_PRESS;
    event.key_code = key_code;
    return event;
}

static PixelTermApp make_book_app(void) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_BOOK;
    app.book.page = 4;
    app.book.page_count = 12;
    return app;
}

static PixelTermApp make_book_preview_app(void) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_BOOK_PREVIEW;
    app.book.preview_selected = 6;
    app.book.page_count = 12;
    return app;
}

static void test_enter_opens_book_preview_from_reader(void) {
    PixelTermApp app = make_book_app();
    InputEvent event = make_key_event(KEY_ENTER);

    input_dispatch_test_reset_stubs();

    input_dispatch_handle_key_press_book(&app, NULL, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_preview_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.book_preview_render_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_page_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
}

static void test_tab_returns_to_file_manager_from_reader(void) {
    PixelTermApp app = make_book_app();
    InputEvent event = make_key_event(KEY_TAB);

    app.book.path = g_strdup("/tmp/book.epub");

    input_dispatch_test_reset_stubs();

    input_dispatch_handle_key_press_book(&app, NULL, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.close_book_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_file_manager_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_select_path_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_preview_calls, ==, 0);
    g_assert_cmpint(app.mode, ==, APP_MODE_FILE_MANAGER);

    g_free(app.book.path);
}

static void test_file_manager_entry_failure_preserves_book(gconstpointer data) {
    ErrorCode result = GPOINTER_TO_INT(data);
    PixelTermApp app = make_book_app();
    app.book.path = g_strdup("/tmp/book.epub");
    app.book.doc = (BookDocument *)&app;
    app.suppress_full_clear = TRUE;
    InputEvent event = make_key_event(KEY_TAB);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.enter_file_manager_result = result;
    input_dispatch_handle_key_press_book(&app, NULL, &event);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK);
    g_assert_true(app.book.doc == (BookDocument *)&app);
    g_assert_cmpstr(app.book.path, ==, "/tmp/book.epub");
    g_assert_cmpint(app.book.page, ==, 4);
    g_assert_true(app.needs_redraw);
    g_assert_cmpint(g_input_dispatch_stub_state.close_book_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    g_assert_cmpint(app.last_error, ==, result == ERROR_OUTPUT_IO ? result : ERROR_NONE);
    g_assert_cmpint(g_input_dispatch_stub_state.book_page_render_calls, ==, result != ERROR_OUTPUT_IO);
    g_assert_cmpint(app.suppress_full_clear, ==, result == ERROR_OUTPUT_IO);
    g_free(app.book.path);
}

static void test_preview_to_file_manager_entry(gconstpointer data) {
    ErrorCode result = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = APP_MODE_PREVIEW, .return_to_mode = RETURN_MODE_PREVIEW_VIRTUAL};
    app.total_images = 8;
    app.current_index = 2;
    app.preview.selected = 4;
    app.file_manager.previous_selected_entry = 5;
    InputEvent event = make_key_event(KEY_TAB);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.enter_file_manager_result = result;
    input_dispatch_handle_key_press_preview(&app, NULL, &event);
    g_assert_cmpint(g_input_dispatch_stub_state.last_enter_file_manager_index, ==, 4);
    g_assert_cmpint(app.current_index, ==, result == ERROR_NONE ? 4 : 2);
    g_assert_cmpint(app.mode, ==, result == ERROR_NONE ? APP_MODE_FILE_MANAGER : APP_MODE_PREVIEW);
    g_assert_cmpint(g_input_dispatch_stub_state.exit_preview_calls, ==, result == ERROR_NONE);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, result == ERROR_NONE);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    g_assert_cmpint(app.return_to_mode, ==, result == ERROR_NONE ? RETURN_MODE_PREVIEW : RETURN_MODE_PREVIEW_VIRTUAL);
    g_assert_cmpint(app.file_manager.previous_selected_entry, ==, result == ERROR_NONE ? -1 : 5);
    if (result != ERROR_NONE) g_assert_true(app.needs_redraw);
}

static void test_tab_opens_reader_from_book_preview(void) {
    PixelTermApp app = make_book_preview_app();
    InputEvent event = make_key_event(KEY_TAB);

    input_dispatch_test_reset_stubs();

    input_dispatch_handle_key_press_book_preview(&app, NULL, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_page_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.book_page_render_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_file_manager_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.close_book_calls, ==, 0);
    g_assert_cmpint(app.book.page, ==, 6);
}

static void test_preview_entry_error_boundary(gconstpointer data) {
    ErrorCode result = GPOINTER_TO_INT(data);
    PixelTermApp app = make_book_preview_app();
    InputEvent event = make_key_event(KEY_ENTER);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.enter_book_page_result = result;
    input_dispatch_handle_key_press_book_preview(&app, NULL, &event);
    g_assert_cmpint(app.last_error, ==, result == ERROR_OUTPUT_IO ? ERROR_OUTPUT_IO : ERROR_NONE);
    g_assert_cmpint(g_input_dispatch_stub_state.book_page_render_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, result != ERROR_OUTPUT_IO);
}

static void test_real_page_entry_error(gconstpointer data) {
    ErrorCode result = GPOINTER_TO_INT(data);
    PixelTermApp app = make_book_app();
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.enter_book_page_result = result;
    g_assert_cmpint(real_book_change_page(&app, 1), ==, result);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_page_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.book_page_render_calls, ==, 0);
    g_assert_cmpint(app.last_error, ==, result == ERROR_OUTPUT_IO ? ERROR_OUTPUT_IO : ERROR_NONE);
}

static void test_real_page_render_error(void) {
    PixelTermApp app = make_book_app();
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.book_page_render_result = ERROR_OUTPUT_IO;
    g_assert_cmpint(real_book_change_page(&app, 1), ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(app.book.page, ==, 5);
    g_assert_cmpint(g_input_dispatch_stub_state.book_page_render_calls, ==, 1);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
}

static void test_jump_prompt_failure_preserves_state(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    PixelTermApp app = scenario & 8 ? make_book_preview_app() : make_book_app();
    gint operation = scenario & 7;
    input_dispatch_test_reset_stubs();
    app.book.jump_active = operation != 0;
    app.book.jump_dirty = TRUE;
    app.book.jump_len = 1;
    g_strlcpy(app.book.jump_buf, "2", sizeof(app.book.jump_buf));
    BookState previous = app.book;
    g_input_dispatch_stub_state.book_jump_prompt_result = ERROR_OUTPUT_IO;
    KeyCode keys[] = {(KeyCode)'p', (KeyCode)'3', KEY_BACKSPACE, KEY_ESCAPE, KEY_ENTER};
    InputEvent event = make_key_event(keys[operation]);
    if (operation == 0) {
        if (scenario & 8) input_dispatch_handle_key_press_book_preview(&app, NULL, &event);
        else input_dispatch_handle_key_press_book(&app, NULL, &event);
    } else {
        g_assert_true(input_dispatch_key_modes_handle_book_jump_input(&app, &event));
    }
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(app.book.jump_active, ==, previous.jump_active);
    g_assert_cmpint(app.book.jump_dirty, ==, previous.jump_dirty);
    g_assert_cmpint(app.book.jump_len, ==, previous.jump_len);
    g_assert_cmpstr(app.book.jump_buf, ==, previous.jump_buf);
    g_assert_cmpint(app.book.page, ==, previous.page);
    g_assert_cmpint(app.book.preview_selected, ==, previous.preview_selected);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_page_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.book_page_render_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.book_preview_render_calls, ==, 0);
}

static void test_real_page_success_keeps_prior_error(void) {
    PixelTermApp app = make_book_app();
    app.last_error = ERROR_OUTPUT_IO;
    input_dispatch_test_reset_stubs();
    g_assert_cmpint(real_book_change_page(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.page, ==, 5);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(real_book_change_page(&app, 0), ==, ERROR_NONE);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_page_calls, ==, 1);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
}

static void test_file_manager_recovery_render_failure(void) {
    PixelTermApp app = make_book_app();
    InputEvent event = make_key_event(KEY_TAB);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.enter_file_manager_result = ERROR_INVALID_IMAGE;
    g_input_dispatch_stub_state.book_page_render_result = ERROR_OUTPUT_IO;
    input_dispatch_handle_key_press_book(&app, NULL, &event);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(g_input_dispatch_stub_state.book_page_render_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.close_book_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 0);
}

void register_input_dispatch_key_book_tests(void) {
    g_test_add_func("/input_dispatch_key_book/errors/page_success_keeps_prior_error", test_real_page_success_keeps_prior_error);
    g_test_add_func("/input_dispatch_key_book/errors/file_manager_recovery_render_failure", test_file_manager_recovery_render_failure);
    g_test_add_data_func("/input_dispatch_key_book/errors/file_manager_output_preserves_book", GINT_TO_POINTER(ERROR_OUTPUT_IO), test_file_manager_entry_failure_preserves_book);
    g_test_add_data_func("/input_dispatch_key_book/errors/file_manager_validation_preserves_book", GINT_TO_POINTER(ERROR_INVALID_IMAGE), test_file_manager_entry_failure_preserves_book);
    g_test_add_data_func("/input_dispatch_key_book/preview_to_file_manager/success", GINT_TO_POINTER(ERROR_NONE), test_preview_to_file_manager_entry);
    g_test_add_data_func("/input_dispatch_key_book/preview_to_file_manager/output_failure", GINT_TO_POINTER(ERROR_OUTPUT_IO), test_preview_to_file_manager_entry);
    g_test_add_data_func("/input_dispatch_key_book/preview_to_file_manager/validation_failure", GINT_TO_POINTER(ERROR_INVALID_IMAGE), test_preview_to_file_manager_entry);
    g_test_add_data_func("/input_dispatch_key_book/jump/start_failure", GINT_TO_POINTER(0), test_jump_prompt_failure_preserves_state);
    g_test_add_data_func("/input_dispatch_key_book/jump/digit_failure", GINT_TO_POINTER(1), test_jump_prompt_failure_preserves_state);
    g_test_add_data_func("/input_dispatch_key_book/jump/backspace_failure", GINT_TO_POINTER(2), test_jump_prompt_failure_preserves_state);
    g_test_add_data_func("/input_dispatch_key_book/jump/cancel_failure", GINT_TO_POINTER(3), test_jump_prompt_failure_preserves_state);
    g_test_add_data_func("/input_dispatch_key_book/jump/commit_failure", GINT_TO_POINTER(4), test_jump_prompt_failure_preserves_state);
    g_test_add_data_func("/input_dispatch_key_book/jump/preview_start_failure", GINT_TO_POINTER(8), test_jump_prompt_failure_preserves_state);
    g_test_add_data_func("/input_dispatch_key_book/jump/preview_commit_failure", GINT_TO_POINTER(12), test_jump_prompt_failure_preserves_state);
    g_test_add_data_func("/input_dispatch_key_book/errors/page_entry_output", GINT_TO_POINTER(ERROR_OUTPUT_IO), test_real_page_entry_error);
    g_test_add_data_func("/input_dispatch_key_book/errors/page_entry_validation", GINT_TO_POINTER(ERROR_INVALID_IMAGE), test_real_page_entry_error);
    g_test_add_func("/input_dispatch_key_book/errors/page_render_output", test_real_page_render_error);
    g_test_add_data_func("/input_dispatch_key_book/errors/preview_output", GINT_TO_POINTER(ERROR_OUTPUT_IO), test_preview_entry_error_boundary);
    g_test_add_data_func("/input_dispatch_key_book/errors/preview_validation", GINT_TO_POINTER(ERROR_INVALID_IMAGE), test_preview_entry_error_boundary);
    g_test_add_func("/input_dispatch_key_book/reader/enter_opens_book_preview",
                    test_enter_opens_book_preview_from_reader);
    g_test_add_func("/input_dispatch_key_book/reader/tab_returns_to_file_manager",
                    test_tab_returns_to_file_manager_from_reader);
    g_test_add_func("/input_dispatch_key_book/preview/tab_opens_reader",
                    test_tab_opens_reader_from_book_preview);
}
