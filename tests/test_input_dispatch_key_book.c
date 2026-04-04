#include <glib.h>

#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_test_support.h"

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

    g_free(app.book.path);
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

void register_input_dispatch_key_book_tests(void) {
    g_test_add_func("/input_dispatch_key_book/reader/enter_opens_book_preview",
                    test_enter_opens_book_preview_from_reader);
    g_test_add_func("/input_dispatch_key_book/reader/tab_returns_to_file_manager",
                    test_tab_returns_to_file_manager_from_reader);
    g_test_add_func("/input_dispatch_key_book/preview/tab_opens_reader",
                    test_tab_opens_reader_from_book_preview);
}
