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
    g_free(app->book.path);
}

static PixelTermApp make_file_manager_list_app(void) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_FILE_MANAGER;
    app.file_manager.entries = g_list_append(app.file_manager.entries, g_strdup("alpha.jpg"));
    app.file_manager.entries = g_list_append(app.file_manager.entries, g_strdup("bravo.jpg"));
    app.file_manager.entries_count = 2;
    app.file_manager.selected_entry = 1;
    app.file_manager.selected_link_index = -1;
    app.file_manager.directory = g_strdup("/");
    return app;
}

static void test_letter_keys_jump_before_vim_navigation(void) {
    PixelTermApp app = make_file_manager_list_app();
    InputEvent event = make_key_event((KeyCode)'k');

    input_dispatch_test_reset_stubs();

    input_dispatch_handle_key_press_file_manager(&app, NULL, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_jump_to_letter_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_file_manager_jump_letter, ==, 'k');
    g_assert_cmpint(app.file_manager.selected_entry, ==, 1);

    cleanup_file_manager_app(&app);
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
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK_PREVIEW);

    g_free(app.current_directory);
    cleanup_file_manager_app(&app);
    g_remove(book_path);
    g_free(book_path);
    g_rmdir(dir);
    g_free(dir);
}

static void test_enter_error_boundary(gconstpointer data) {
    ErrorCode result = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = APP_MODE_FILE_MANAGER};
    InputEvent event = make_key_event(KEY_ENTER);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.file_manager_enter_result = result;
    input_dispatch_handle_key_press_file_manager(&app, NULL, &event);
    g_assert_cmpint(app.last_error, ==, result == ERROR_OUTPUT_IO ? ERROR_OUTPUT_IO : ERROR_NONE);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 0);
    g_assert_cmpint(app.mode, ==, APP_MODE_FILE_MANAGER);
}

static void test_enter_error_redraw_once(gconstpointer data) {
    KeyCode key = GPOINTER_TO_INT(data);
    InputEvent event = make_key_event(key);
    PixelTermApp app = {.mode = APP_MODE_FILE_MANAGER};
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.file_manager_enter_result = ERROR_INVALID_IMAGE;
    g_input_dispatch_stub_state.file_manager_enter_renders_error = TRUE;
    input_dispatch_handle_key_press_file_manager(&app, NULL, &event);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 1);
    g_assert_cmpint(app.mode, ==, APP_MODE_FILE_MANAGER);
    g_assert_cmpint(app.last_error, ==, ERROR_NONE);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.file_manager_enter_renders_error = TRUE;
    g_input_dispatch_stub_state.file_manager_render_result = ERROR_OUTPUT_IO;
    input_dispatch_handle_key_press_file_manager(&app, NULL, &event);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 1);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
}

static void test_tab_preserves_source_until_destination_entry(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    gint kind = scenario & 3;
    gboolean render_failure = scenario & 4;
    gboolean redraw_failure = scenario & 8;
    gchar *dir = create_temp_dir();
    gchar *path = kind == 0 ? write_book_fixture(dir) :
                  kind == 1 ? g_build_filename(dir, "image.jpg", NULL) : g_strdup(dir);
    if (kind == 1) g_assert_true(g_file_set_contents(path, "image", -1, NULL));
    ErrorCode results[] = {ERROR_OUTPUT_IO, ERROR_INVALID_IMAGE, ERROR_MEMORY_ALLOC, ERROR_NONE};
    for (guint i = 0; i < G_N_ELEMENTS(results); i++) {
        PixelTermApp app = make_file_manager_app(path);
        app.current_directory = g_strdup("/original");
        app.info_visible = app.help_visible = TRUE;
        if (kind == 0) {
            app.book = (BookState){.doc = (BookDocument *)0x1,
                .path = g_strdup("/original/old.pdf"), .toc = (BookToc *)0x1,
                .page = 3, .page_count = 20, .preview_selected = 7, .preview_scroll = 2,
                .preview_zoom = 40, .toc_selected = 4, .toc_scroll = 1, .toc_visible = TRUE,
                .jump_active = TRUE, .jump_dirty = TRUE, .jump_len = 2, .jump_buf = "12"};
        }
        BookState old_book = app.book;
        gchar *old_directory = app.current_directory;
        if (kind != 0) {
            app.return_to_mode = RETURN_MODE_NONE;
            app.current_index = 1;
            app.file_manager.previous_selected_entry = 9;
            app.image_files = g_list_append(NULL, g_strdup(path));
            app.image_files = g_list_append(app.image_files, g_strdup("/original/second.jpg"));
            app.total_images = 2;
            app.preview.selected = 1;
            app.preview.selected_link = app.image_files->next;
            app.preview.selected_link_index = 1;
        }
        GList *entries = app.file_manager.entries;
        InputEvent event = make_key_event(KEY_TAB);
        input_dispatch_test_reset_stubs();
        g_input_dispatch_stub_state.file_manager_has_images = TRUE;
        g_input_dispatch_stub_state.load_directory_replaces_media = kind != 0;
        g_input_dispatch_stub_state.enter_book_preview_result = render_failure ? ERROR_NONE : results[i];
        g_input_dispatch_stub_state.enter_preview_result = render_failure ? ERROR_NONE : results[i];
        g_input_dispatch_stub_state.book_preview_render_result = render_failure ? results[i] : ERROR_NONE;
        g_input_dispatch_stub_state.preview_render_result = render_failure ? results[i] : ERROR_NONE;
        g_input_dispatch_stub_state.file_manager_render_result = redraw_failure ? ERROR_OUTPUT_IO : ERROR_NONE;
        input_dispatch_handle_key_press_file_manager(&app, NULL, &event);
        gboolean success = results[i] == ERROR_NONE;
        g_assert_cmpint(g_input_dispatch_stub_state.exit_file_manager_calls, ==, success);
        g_assert_cmpint(app.mode, ==, success ? (kind == 0 ? APP_MODE_BOOK_PREVIEW : APP_MODE_PREVIEW) : APP_MODE_FILE_MANAGER);
        g_assert_cmpint(g_input_dispatch_stub_state.book_preview_render_calls, ==, (success || render_failure) && kind == 0);
        g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, (success || render_failure) && kind != 0);
        g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, !success && results[i] != ERROR_OUTPUT_IO);
        g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
        if (kind == 0) {
            g_assert_cmpint(g_input_dispatch_stub_state.close_book_calls, ==, 1);
            if (!success) {
                g_assert_cmpmem(&app.book, sizeof(app.book), &old_book, sizeof(old_book));
                g_assert_true(app.current_directory == old_directory);
            } else {
                g_assert_true(app.book.doc == (BookDocument *)0x2);
                g_assert_cmpstr(app.book.path, ==, path);
            }
        }
        if (!success) {
            g_assert_true(app.needs_redraw);
            g_assert_true(app.info_visible);
            g_assert_true(app.help_visible);
            g_assert_true(app.file_manager.entries == entries);
            g_assert_cmpstr(app.file_manager.entries->data, ==, path);
            if (kind != 0) {
                g_assert_cmpint(app.return_to_mode, ==, RETURN_MODE_NONE);
                g_assert_cmpint(app.current_index, ==, 1);
                g_assert_cmpint(app.file_manager.previous_selected_entry, ==, 9);
                g_assert_cmpint(app.total_images, ==, 2);
                g_assert_cmpstr(app.image_files->data, ==, path);
                g_assert_cmpstr(app.image_files->next->data, ==, "/original/second.jpg");
                g_assert_cmpint(app.preview.selected, ==, 1);
                g_assert_true(app.preview.selected_link == app.image_files->next);
                g_assert_cmpint(app.preview.selected_link_index, ==, 1);
            }
            g_assert_cmpstr(app.current_directory, ==, "/original");
        }
        g_assert_cmpint(app.last_error, ==, results[i] == ERROR_OUTPUT_IO || (redraw_failure && !success) ? ERROR_OUTPUT_IO : ERROR_NONE);
        g_list_free_full(app.image_files, g_free);
        g_free(app.current_directory);
        cleanup_file_manager_app(&app);
    }
    if (kind != 2) g_remove(path);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

static void test_navigation_render_result_controls_queue(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    KeyCode key = scenario & 1 ? KEY_DOWN : KEY_UP;
    gboolean failure = (scenario & 2) != 0;
    PixelTermApp app = make_file_manager_list_app();
    app.file_manager.selected_entry = key == KEY_DOWN ? 0 : 1;
    app.last_error = ERROR_OUTPUT_IO;
    InputHandler handler = {0};
    InputEvent event = make_key_event(key);
    input_unget_event(&handler, &event);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.file_manager_render_result = failure ? ERROR_OUTPUT_IO : ERROR_NONE;
    input_dispatch_handle_key_press_file_manager(&app, &handler, &event);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 1);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(handler.has_pending_event, ==, failure);
    if (failure) g_assert_cmpint(handler.pending_event.key_code, ==, key);
    cleanup_file_manager_app(&app);
}

static void test_tab_book_open_failure_keeps_existing_book(gconstpointer data) {
    ErrorCode result = GPOINTER_TO_INT(data);
    gchar *dir = create_temp_dir();
    gchar *path = write_book_fixture(dir);
    PixelTermApp app = make_file_manager_app(path);
    app.current_directory = g_strdup("/original");
    app.book.doc = (BookDocument *)0x1;
    app.book.path = g_strdup("/original/old.pdf");
    GList *entries = app.file_manager.entries;
    InputEvent event = make_key_event(KEY_TAB);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.open_book_result = result;
    input_dispatch_handle_key_press_file_manager(&app, NULL, &event);
    g_assert_cmpint(g_input_dispatch_stub_state.open_book_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.close_book_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_book_preview_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.exit_file_manager_calls, ==, 0);
    g_assert_true(app.book.doc == (BookDocument *)0x1);
    g_assert_cmpstr(app.book.path, ==, "/original/old.pdf");
    g_assert_cmpstr(app.current_directory, ==, "/original");
    g_assert_true(app.file_manager.entries == entries);
    g_assert_cmpint(app.mode, ==, APP_MODE_FILE_MANAGER);
    g_assert_cmpint(app.last_error, ==, result == ERROR_OUTPUT_IO ? ERROR_OUTPUT_IO : ERROR_NONE);
    g_free(app.current_directory);
    cleanup_file_manager_app(&app);
    g_remove(path);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

void register_input_dispatch_key_file_manager_tests(void) {
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/book_render_preserves_source", GINT_TO_POINTER(4), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/media_render_preserves_source", GINT_TO_POINTER(5), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/virtual_render_preserves_source", GINT_TO_POINTER(6), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/book_render_recovery_failure", GINT_TO_POINTER(12), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/media_render_recovery_failure", GINT_TO_POINTER(13), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/virtual_render_recovery_failure", GINT_TO_POINTER(14), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/book_open_output_keeps_existing_book", GINT_TO_POINTER(ERROR_OUTPUT_IO), test_tab_book_open_failure_keeps_existing_book);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/book_open_invalid_keeps_existing_book", GINT_TO_POINTER(ERROR_INVALID_IMAGE), test_tab_book_open_failure_keeps_existing_book);
    g_test_add_data_func("/input_dispatch_key_file_manager/navigation/up_render_failure", GINT_TO_POINTER(2), test_navigation_render_result_controls_queue);
    g_test_add_data_func("/input_dispatch_key_file_manager/navigation/down_render_failure", GINT_TO_POINTER(3), test_navigation_render_result_controls_queue);
    g_test_add_data_func("/input_dispatch_key_file_manager/navigation/up_previous_error", GINT_TO_POINTER(0), test_navigation_render_result_controls_queue);
    g_test_add_data_func("/input_dispatch_key_file_manager/navigation/down_previous_error", GINT_TO_POINTER(1), test_navigation_render_result_controls_queue);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/book_entry_preserves_source", GINT_TO_POINTER(0), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/media_entry_preserves_source", GINT_TO_POINTER(1), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/tab/virtual_entry_preserves_source", GINT_TO_POINTER(2), test_tab_preserves_source_until_destination_entry);
    g_test_add_data_func("/input_dispatch_key_file_manager/errors/enter_redraw_once", GINT_TO_POINTER(KEY_ENTER), test_enter_error_redraw_once);
    g_test_add_data_func("/input_dispatch_key_file_manager/errors/right_redraw_once", GINT_TO_POINTER(KEY_RIGHT), test_enter_error_redraw_once);
    g_test_add_data_func("/input_dispatch_key_file_manager/errors/enter_output", GINT_TO_POINTER(ERROR_OUTPUT_IO), test_enter_error_boundary);
    g_test_add_data_func("/input_dispatch_key_file_manager/errors/enter_validation", GINT_TO_POINTER(ERROR_INVALID_IMAGE), test_enter_error_boundary);
    g_test_add_func("/input_dispatch_key_file_manager/tab/book_selection_enters_book_preview",
                    test_tab_on_book_selection_enters_book_preview);
    g_test_add_func("/input_dispatch_key_file_manager/letter_keys/jump_before_vim_navigation",
                    test_letter_keys_jump_before_vim_navigation);
}
