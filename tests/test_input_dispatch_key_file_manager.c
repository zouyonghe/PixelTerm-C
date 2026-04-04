#include <glib.h>
#include <glib/gstdio.h>

#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_test_support.h"

static InputEvent make_key_event(KeyCode key_code) {
    InputEvent event = {0};
    event.type = INPUT_KEY_PRESS;
    event.key_code = key_code;
    return event;
}

static gchar *create_temp_dir(void) {
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("pixelterm-input-dispatch-fm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(dir);
    return dir;
}

static gchar *write_book_fixture(const gchar *dir) {
    gchar *path = g_build_filename(dir, "sample.pdf", NULL);
    GError *error = NULL;
    g_assert_true(g_file_set_contents(path, "not-empty", -1, &error));
    g_assert_no_error(error);
    return path;
}

static PixelTermApp make_file_manager_app(const gchar *selected_path) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_FILE_MANAGER;
    app.file_manager.entries = g_list_append(NULL, g_strdup(selected_path));
    app.file_manager.entries_count = 1;
    app.file_manager.selected_entry = 0;
    app.file_manager.selected_link_index = -1;
    app.file_manager.directory = g_path_get_dirname(selected_path);
    return app;
}

static void cleanup_file_manager_app(PixelTermApp *app) {
    if (!app) {
        return;
    }
    g_list_free_full(app->file_manager.entries, g_free);
    g_free(app->file_manager.directory);
}

static void test_tab_on_book_selection_enters_book_preview(void) {
    gchar *dir = create_temp_dir();
    gchar *book_path = write_book_fixture(dir);
    PixelTermApp app = make_file_manager_app(book_path);
    InputEvent event = make_key_event(KEY_TAB);

    input_dispatch_test_reset_stubs();

    input_dispatch_handle_key_press_file_manager(&app, NULL, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.open_book_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.exit_file_manager_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_preview_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.book_preview_render_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_preview_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);

    cleanup_file_manager_app(&app);
    g_free(book_path);
    g_rmdir(dir);
    g_free(dir);
}

void register_input_dispatch_key_file_manager_tests(void) {
    g_test_add_func("/input_dispatch_key_file_manager/tab/book_selection_enters_book_preview",
                    test_tab_on_book_selection_enters_book_preview);
}
