#include <glib.h>
#include <glib/gstdio.h>

#include "app.h"
#include "app_media_session.h"

#include "browser.h"
#include "preload_control.h"

static const guint8 k_png_data[] = {
    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
};

FileBrowser *browser_create(void) {
    return NULL;
}

void browser_destroy(FileBrowser *browser) {
    (void)browser;
}

ErrorCode browser_scan_directory(FileBrowser *browser, const char *directory) {
    (void)browser;
    (void)directory;
    return ERROR_NONE;
}

GList *browser_get_all_files(const FileBrowser *browser) {
    (void)browser;
    return NULL;
}

gint browser_get_total_files(const FileBrowser *browser) {
    (void)browser;
    return 0;
}

BookDocument *book_open(const char *filepath, ErrorCode *out_error) {
    (void)filepath;
    if (out_error) {
        *out_error = ERROR_NONE;
    }
    return NULL;
}

void book_close(BookDocument *doc) {
    (void)doc;
}

gint book_get_page_count(const BookDocument *doc) {
    (void)doc;
    return 0;
}

BookToc *book_load_toc(BookDocument *doc) {
    (void)doc;
    return NULL;
}

void book_toc_free(BookToc *toc) {
    (void)toc;
}

ErrorCode app_preloader_enable(PixelTermApp *app, gboolean queue_tasks) {
    (void)app;
    (void)queue_tasks;
    return ERROR_NONE;
}

void app_preloader_reset(PixelTermApp *app) {
    (void)app;
}

void app_media_stop_inactive_players(PixelTermApp *app, MediaKind active_kind) {
    (void)app;
    (void)active_kind;
}

ErrorCode app_enter_book_preview(PixelTermApp *app) {
    (void)app;
    return ERROR_NONE;
}

ErrorCode app_render_book_preview(PixelTermApp *app) {
    (void)app;
    return ERROR_NONE;
}

ErrorCode app_render_current_image(PixelTermApp *app) {
    (void)app;
    return ERROR_NONE;
}

static void remove_dir_tree(gpointer data) {
    gchar *dir = data;
    if (!dir) {
        return;
    }

    GDir *handle = g_dir_open(dir, 0, NULL);
    if (handle) {
        const gchar *name = NULL;
        while ((name = g_dir_read_name(handle)) != NULL) {
            gchar *path = g_build_filename(dir, name, NULL);
            if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
                remove_dir_tree(path);
            } else {
                g_remove(path);
                g_free(path);
            }
        }
        g_dir_close(handle);
    }

    g_rmdir(dir);
    g_free(dir);
}

static gchar *create_temp_dir(void) {
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp("pixelterm-app-file-manager-XXXXXX", &error);
    if (!dir) {
        g_error("Failed to create temp directory: %s", error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }
    g_test_queue_destroy(remove_dir_tree, g_strdup(dir));
    return dir;
}

static gchar *write_file_in_dir(const gchar *dir, const gchar *name, const guint8 *data, gsize len) {
    gchar *path = g_build_filename(dir, name, NULL);
    GError *error = NULL;
    if (!g_file_set_contents(path, (const gchar *)data, (gssize)len, &error)) {
        g_error("Failed to write test file: %s", error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }
    return path;
}

static gchar *create_dir_in_dir(const gchar *dir, const gchar *name) {
    gchar *path = g_build_filename(dir, name, NULL);
    g_assert_cmpint(g_mkdir(path, 0700), ==, 0);
    return path;
}

static void init_file_manager_app(PixelTermApp *app, const gchar *directory, gint term_height) {
    app->mode = APP_MODE_FILE_MANAGER;
    app->term_width = 80;
    app->term_height = term_height;
    app->return_to_mode = RETURN_MODE_NONE;
    app->file_manager.directory = g_strdup(directory);
    app->file_manager.selected_link_index = -1;
}

static void cleanup_file_manager_app(PixelTermApp *app) {
    if (!app) {
        return;
    }

    if (app->file_manager.entries) {
        g_list_free_full(app->file_manager.entries, (GDestroyNotify)g_free);
        app->file_manager.entries = NULL;
    }
    g_clear_pointer(&app->file_manager.directory, g_free);
}

static const gchar *selected_path(const PixelTermApp *app) {
    if (!app || app->file_manager.selected_entry < 0) {
        return NULL;
    }

    GList *node = g_list_nth(app->file_manager.entries, app->file_manager.selected_entry);
    return node ? (const gchar *)node->data : NULL;
}

static void test_refresh_handles_empty_directory(void) {
    gchar *dir = create_temp_dir();
    PixelTermApp app = {0};
    gchar *basename = NULL;

    init_file_manager_app(&app, dir, 24);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.entries_count, ==, 1);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 0);
    basename = g_path_get_basename(selected_path(&app));
    g_assert_cmpstr(basename, ==, "..");
    g_assert_false(app_file_manager_has_images(&app));
    g_assert_false(app_file_manager_selection_is_image(&app));

    g_assert_cmpint(app_file_manager_up(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 0);
    g_assert_cmpint(app_file_manager_down(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 0);
    g_assert_cmpint(app.file_manager.scroll_offset, ==, 0);

    g_free(basename);
    cleanup_file_manager_app(&app);
    g_free(dir);
}

static void test_refresh_preserves_selected_path_when_entry_still_exists(void) {
    gchar *dir = create_temp_dir();
    gchar *a_png = write_file_in_dir(dir, "a.png", k_png_data, sizeof(k_png_data));
    gchar *b_png = write_file_in_dir(dir, "b.png", k_png_data, sizeof(k_png_data));
    gchar *c_png = write_file_in_dir(dir, "c.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};

    init_file_manager_app(&app, dir, 24);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app_file_manager_select_path(&app, b_png), ==, ERROR_NONE);
    g_assert_cmpstr(selected_path(&app), ==, b_png);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpstr(selected_path(&app), ==, b_png);

    cleanup_file_manager_app(&app);
    g_free(a_png);
    g_free(b_png);
    g_free(c_png);
    g_free(dir);
}

static void test_refresh_recalculates_scroll_for_preserved_selection(void) {
    gchar *dir = create_temp_dir();
    gchar *f_png = NULL;
    PixelTermApp app = {0};

    init_file_manager_app(&app, dir, 10);

    for (gchar name = 'a'; name <= 'f'; name++) {
        gchar filename[] = {name, '.', 'p', 'n', 'g', '\0'};
        gchar *path = write_file_in_dir(dir, filename, k_png_data, sizeof(k_png_data));
        if (name == 'f') {
            f_png = g_strdup(path);
        }
        g_free(path);
    }

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app_file_manager_select_path(&app, f_png), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 6);
    g_assert_cmpint(app.file_manager.scroll_offset, ==, 5);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpstr(selected_path(&app), ==, f_png);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 6);
    g_assert_cmpint(app.file_manager.scroll_offset, ==, 5);

    cleanup_file_manager_app(&app);
    g_free(f_png);
    g_free(dir);
}

static void test_refresh_adjusts_selection_after_selected_file_is_removed(void) {
    gchar *dir = create_temp_dir();
    gchar *a_png = write_file_in_dir(dir, "a.png", k_png_data, sizeof(k_png_data));
    gchar *b_png = write_file_in_dir(dir, "b.png", k_png_data, sizeof(k_png_data));
    gchar *c_png = write_file_in_dir(dir, "c.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};

    init_file_manager_app(&app, dir, 24);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app_file_manager_select_path(&app, c_png), ==, ERROR_NONE);
    g_assert_cmpstr(selected_path(&app), ==, c_png);
    g_assert_cmpint(g_remove(c_png), ==, 0);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpstr(selected_path(&app), ==, b_png);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 2);

    cleanup_file_manager_app(&app);
    g_free(a_png);
    g_free(b_png);
    g_free(c_png);
    g_free(dir);
}

static void test_refresh_preserves_explicit_parent_selection(void) {
    gchar *dir = create_temp_dir();
    gchar *a_png = write_file_in_dir(dir, "a.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};
    gchar *basename = NULL;

    init_file_manager_app(&app, dir, 24);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 1);

    g_assert_cmpint(app_file_manager_up(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 0);
    basename = g_path_get_basename(selected_path(&app));
    g_assert_cmpstr(basename, ==, "..");
    g_free(basename);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 0);
    basename = g_path_get_basename(selected_path(&app));
    g_assert_cmpstr(basename, ==, "..");

    g_free(basename);
    cleanup_file_manager_app(&app);
    g_free(a_png);
    g_free(dir);
}

static void test_toggle_hidden_preserves_visible_selection(void) {
    gchar *dir = create_temp_dir();
    gchar *hidden_png = write_file_in_dir(dir, ".hidden.png", k_png_data, sizeof(k_png_data));
    gchar *visible_png = write_file_in_dir(dir, "visible.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};

    init_file_manager_app(&app, dir, 24);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.entries_count, ==, 2);
    g_assert_cmpint(app_file_manager_select_path(&app, visible_png), ==, ERROR_NONE);

    g_assert_cmpint(app_file_manager_toggle_hidden(&app), ==, ERROR_NONE);
    g_assert_true(app.show_hidden_files);
    g_assert_cmpint(app.file_manager.entries_count, ==, 3);
    g_assert_cmpstr(selected_path(&app), ==, visible_png);

    g_assert_cmpint(app_file_manager_toggle_hidden(&app), ==, ERROR_NONE);
    g_assert_false(app.show_hidden_files);
    g_assert_cmpint(app.file_manager.entries_count, ==, 2);
    g_assert_cmpstr(selected_path(&app), ==, visible_png);

    cleanup_file_manager_app(&app);
    g_free(hidden_png);
    g_free(visible_png);
    g_free(dir);
}

static void test_toggle_hidden_clamps_selection_when_hidden_entry_disappears_after_preview_return(void) {
    gchar *dir = create_temp_dir();
    gchar *a_png = write_file_in_dir(dir, "a.png", k_png_data, sizeof(k_png_data));
    gchar *b_png = write_file_in_dir(dir, "b.png", k_png_data, sizeof(k_png_data));
    gchar *hidden_png = write_file_in_dir(dir, ".hidden.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};

    init_file_manager_app(&app, dir, 24);
    app.show_hidden_files = TRUE;
    app.return_to_mode = RETURN_MODE_PREVIEW;
    app.current_directory = g_strdup(dir);
    app.image_files = g_list_append(app.image_files, g_strdup(a_png));
    app.image_files = g_list_append(app.image_files, g_strdup(b_png));
    app.total_images = 2;
    app.current_index = 0;

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpstr(selected_path(&app), ==, a_png);
    g_assert_cmpint(app_file_manager_select_path(&app, hidden_png), ==, ERROR_NONE);
    g_assert_cmpstr(selected_path(&app), ==, hidden_png);

    g_assert_cmpint(app_file_manager_toggle_hidden(&app), ==, ERROR_NONE);
    g_assert_false(app.show_hidden_files);
    g_assert_cmpint(app.file_manager.entries_count, ==, 3);
    g_assert_cmpstr(selected_path(&app), ==, b_png);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 2);

    cleanup_file_manager_app(&app);
    g_list_free_full(app.image_files, g_free);
    g_free(app.current_directory);
    g_free(a_png);
    g_free(b_png);
    g_free(hidden_png);
    g_free(dir);
}

static void test_navigation_wraps_and_clamps_scroll_at_paging_boundaries(void) {
    gchar *dir = create_temp_dir();
    PixelTermApp app = {0};

    init_file_manager_app(&app, dir, 10);

    for (gchar name = 'a'; name <= 'f'; name++) {
        gchar filename[] = {name, '.', 'p', 'n', 'g', '\0'};
        gchar *path = write_file_in_dir(dir, filename, k_png_data, sizeof(k_png_data));
        g_free(path);
    }

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.entries_count, ==, 7);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 1);
    g_assert_cmpint(app.file_manager.scroll_offset, ==, 0);

    g_assert_cmpint(app_file_manager_up(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 0);
    g_assert_cmpint(app.file_manager.scroll_offset, ==, 0);

    g_assert_cmpint(app_file_manager_up(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 6);
    g_assert_cmpint(app.file_manager.scroll_offset, ==, 5);

    g_assert_cmpint(app_file_manager_down(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 0);
    g_assert_cmpint(app.file_manager.scroll_offset, ==, 0);

    cleanup_file_manager_app(&app);
    g_free(dir);
}

static void test_enter_child_directory_defaults_to_first_real_entry(void) {
    gchar *dir = create_temp_dir();
    gchar *child_dir = create_dir_in_dir(dir, "child");
    gchar *child_file = write_file_in_dir(child_dir, "inside.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};
    gchar *basename = NULL;

    init_file_manager_app(&app, dir, 24);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app_file_manager_select_path(&app, child_dir), ==, ERROR_NONE);

    g_assert_cmpint(app_file_manager_enter(&app), ==, ERROR_NONE);
    g_assert_cmpstr(app.file_manager.directory, ==, child_dir);
    basename = g_path_get_basename(selected_path(&app));
    g_assert_cmpstr(basename, ==, "inside.png");
    g_assert_cmpint(app.file_manager.selected_entry, ==, 1);

    g_free(basename);
    cleanup_file_manager_app(&app);
    g_free(child_dir);
    g_free(child_file);
    g_free(dir);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/app_file_manager/refresh/handles_empty_directory",
                    test_refresh_handles_empty_directory);
    g_test_add_func("/app_file_manager/refresh/preserves_selected_path_when_entry_still_exists",
                    test_refresh_preserves_selected_path_when_entry_still_exists);
    g_test_add_func("/app_file_manager/refresh/recalculates_scroll_for_preserved_selection",
                    test_refresh_recalculates_scroll_for_preserved_selection);
    g_test_add_func("/app_file_manager/refresh/adjusts_selection_after_selected_file_is_removed",
                    test_refresh_adjusts_selection_after_selected_file_is_removed);
    g_test_add_func("/app_file_manager/refresh/preserves_explicit_parent_selection",
                    test_refresh_preserves_explicit_parent_selection);
    g_test_add_func("/app_file_manager/toggle_hidden/preserves_visible_selection",
                    test_toggle_hidden_preserves_visible_selection);
    g_test_add_func("/app_file_manager/toggle_hidden/clamps_selection_when_hidden_entry_disappears_after_preview_return",
                    test_toggle_hidden_clamps_selection_when_hidden_entry_disappears_after_preview_return);
    g_test_add_func("/app_file_manager/navigation/wraps_and_clamps_scroll_at_paging_boundaries",
                    test_navigation_wraps_and_clamps_scroll_at_paging_boundaries);
    g_test_add_func("/app_file_manager/enter/child_directory_defaults_to_first_real_entry",
                    test_enter_child_directory_defaults_to_first_real_entry);

    return g_test_run();
}
