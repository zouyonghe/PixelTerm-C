#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <unistd.h>

#include "app.h"
#include "app_media_session.h"

#include "browser.h"
#include "preload_control.h"
#include "terminal_output.h"
#include "test_output_stream.h"

static const guint8 k_png_data[] = {
    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
};
static GList *g_browser_test_files = NULL;
static gboolean g_book_open_success;
static ErrorCode g_media_stop_error;
static gint g_media_stop_calls;
static ErrorCode g_book_entry_error;
static ErrorCode g_render_error;
static gint g_book_entry_calls;
static gint g_render_calls;
static gint g_book_open_calls;
static gint g_book_close_calls;
static gint g_old_book_close_calls;
static gint g_old_toc_free_calls;
static gint g_browser_scan_calls;
static ErrorCode g_browser_scan_error;
static gboolean g_browser_create_failure;
static gint g_preloader_reset_calls;
static gint g_live_preloader_resets, g_preloader_enables;

typedef void (*RenderCaptureFunc)(gpointer user_data);

static gchar *capture_render_output(RenderCaptureFunc render_func, gpointer user_data) {
    gchar *template = g_strdup_printf("%s/pixelterm-file-manager-render-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    g_assert_cmpint(fd, >=, 0);

    int saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);

    fflush(stdout);
    g_assert_cmpint(dup2(fd, STDOUT_FILENO), >=, 0);
    close(fd);

    render_func(user_data);

    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);

    gchar *output = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(template, &output, NULL, &error));
    g_assert_no_error(error);
    g_remove(template);
    g_free(template);
    return output;
}

static void render_file_manager_capture(gpointer user_data) {
    PixelTermApp *app = (PixelTermApp *)user_data;
    g_assert_cmpint(app_render_file_manager(app), ==, ERROR_NONE);
}

FileBrowser *browser_create(void) {
    return g_browser_create_failure ? NULL : (FileBrowser *)0x1;
}

void browser_destroy(FileBrowser *browser) {
    (void)browser;
}

ErrorCode browser_scan_directory(FileBrowser *browser, const char *directory) {
    (void)browser;
    (void)directory;
    g_browser_scan_calls++;
    return g_browser_scan_error;
}

GList *browser_get_all_files(const FileBrowser *browser) {
    (void)browser;
    return g_browser_test_files;
}

gint browser_get_total_files(const FileBrowser *browser) {
    (void)browser;
    return g_list_length(g_browser_test_files);
}

BookDocument *book_open(const char *filepath, ErrorCode *out_error) {
    g_book_open_calls++;
    (void)filepath;
    if (out_error) {
        *out_error = ERROR_NONE;
    }
    return g_book_open_success ? (BookDocument *)0x1 : NULL;
}

void book_close(BookDocument *doc) {
    if (doc == (BookDocument *)0x2) g_old_book_close_calls++;
    g_book_close_calls++;
}

gint book_get_page_count(const BookDocument *doc) {
    (void)doc;
    return 4;
}

BookToc *book_load_toc(BookDocument *doc) {
    (void)doc;
    return NULL;
}

void book_toc_free(BookToc *toc) {
    if (toc == (BookToc *)0x2) g_old_toc_free_calls++;
}

static BookState existing_book(void) {
    return (BookState){.doc = (BookDocument *)0x2, .path = g_strdup("old.pdf"),
        .toc = (BookToc *)0x2, .page = 3, .page_count = 20, .preview_selected = 7,
        .preview_scroll = 2, .preview_zoom = 40, .toc_selected = 4, .toc_scroll = 1,
        .toc_visible = TRUE, .jump_active = TRUE, .jump_dirty = TRUE,
        .jump_len = 2, .jump_buf = "12"};
}

static void test_open_book_failure_preserves_book(void) {
    PixelTermApp app = {.book = existing_book(), .current_directory = g_strdup("/old"),
                        .mode = APP_MODE_BOOK_PREVIEW};
    BookState old_book = app.book;
    gchar *directory = app.current_directory;
    g_book_open_success = FALSE;
    g_book_close_calls = 0;
    g_assert_cmpint(app_open_book(&app, "replacement.pdf"), ==, ERROR_INVALID_IMAGE);
    g_assert_cmpmem(&app.book, sizeof(app.book), &old_book, sizeof(old_book));
    g_assert_true(app.current_directory == directory);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK_PREVIEW);
    g_assert_cmpint(g_book_close_calls, ==, 0);
    app_close_book(&app);
    g_free(app.current_directory);
}

ErrorCode app_preloader_enable(PixelTermApp *app, gboolean queue_tasks) {
    if (app->preload_enabled) g_preloader_enables++;
    (void)queue_tasks;
    return ERROR_NONE;
}

void app_preloader_reset(PixelTermApp *app) {
    if (app->preloader) g_live_preloader_resets++;
    app->preloader = NULL;
    g_preloader_reset_calls++;
}

void app_preloader_clear_queue(PixelTermApp *app) {
    (void)app;
}

ErrorCode app_media_stop_inactive_players(PixelTermApp *app, MediaKind active_kind) {
    (void)app;
    (void)active_kind;
    g_media_stop_calls++;
    return g_media_stop_error;
}

ErrorCode app_enter_book_preview(PixelTermApp *app) {
    g_book_entry_calls++;
    if (g_book_entry_error != ERROR_NONE) return g_book_entry_error;
    return app_transition_mode(app, APP_MODE_BOOK_PREVIEW);
}

ErrorCode app_render_book_preview(PixelTermApp *app) {
    g_assert_nonnull(app->file_manager.entries);
    g_assert_cmpint(g_old_book_close_calls, ==, 0);
    g_assert_cmpint(g_old_toc_free_calls, ==, 0);
    g_render_calls++;
    if (g_render_error == ERROR_OUTPUT_IO) terminal_output_write("partial-render", 14);
    return g_render_error;
}

ErrorCode app_render_current_image(PixelTermApp *app) {
    g_assert_nonnull(app->file_manager.entries);
    g_assert_null(app->preloader);
    g_assert_false(app->preload_enabled);
    g_assert_cmpint(g_live_preloader_resets, ==, 0);
    g_assert_cmpint(g_preloader_enables, ==, 0);
    g_render_calls++;
    if (g_render_error == ERROR_OUTPUT_IO) terminal_output_write("partial-render", 14);
    return g_render_error;
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

static gchar *write_png_file_in_dir(const gchar *dir, const gchar *name) {
    static const gchar k_png_base64[] =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR4nGNg"
        "YAAAAAMAASsJTYQAAAAASUVORK5CYII=";
    gsize len = 0;
    guchar *data = g_base64_decode(k_png_base64, &len);
    if (!data || len == 0) {
        g_error("Failed to decode PNG data");
    }

    gchar *path = write_file_in_dir(dir, name, data, len);
    g_free(data);
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

static void clear_browser_test_files(gpointer data) {
    (void)data;
    g_list_free_full(g_browser_test_files, g_free);
    g_browser_test_files = NULL;
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

static void test_refresh_failure_preserves_previous_directory_and_entries(void) {
    gchar *dir = create_temp_dir();
    gchar *a_png = write_file_in_dir(dir, "a.png", k_png_data, sizeof(k_png_data));
    gchar *missing_dir = g_build_filename(dir, "missing", NULL);
    PixelTermApp app = {0};
    gchar *old_directory = NULL;
    GList *old_entries = NULL;
    gint old_entries_count = 0;

    init_file_manager_app(&app, dir, 24);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app_file_manager_select_path(&app, a_png), ==, ERROR_NONE);
    old_directory = g_strdup(app.file_manager.directory);
    old_entries = app.file_manager.entries;
    old_entries_count = app.file_manager.entries_count;

    g_free(app.file_manager.directory);
    app.file_manager.directory = g_strdup(missing_dir);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_FILE_NOT_FOUND);
    g_assert_cmpstr(app.file_manager.directory, ==, old_directory);
    g_assert_true(app.file_manager.entries == old_entries);
    g_assert_cmpint(app.file_manager.entries_count, ==, old_entries_count);
    g_assert_cmpstr(selected_path(&app), ==, a_png);

    g_free(old_directory);
    g_free(missing_dir);
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

static void test_enter_parent_entry_selects_directory_just_left(void) {
    gchar *dir = create_temp_dir();
    gchar *child_dir = create_dir_in_dir(dir, "child");
    gchar *sibling_dir = create_dir_in_dir(dir, "aardvark");
    gchar *child_file = write_file_in_dir(child_dir, "inside.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};

    init_file_manager_app(&app, child_dir, 24);

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 1);
    g_assert_cmpint(app_file_manager_up(&app), ==, ERROR_NONE);
    g_assert_cmpstr(selected_path(&app), !=, child_dir);

    g_assert_cmpint(app_file_manager_enter(&app), ==, ERROR_NONE);

    g_assert_cmpstr(app.file_manager.directory, ==, dir);
    g_assert_cmpstr(selected_path(&app), ==, child_dir);

    cleanup_file_manager_app(&app);
    g_free(child_dir);
    g_free(sibling_dir);
    g_free(child_file);
    g_free(dir);
}

static void test_load_single_file_matches_full_canonical_path_before_basename(void) {
    gchar *target_dir = create_temp_dir();
    gchar *other_dir = create_temp_dir();
    gchar *target_png = write_png_file_in_dir(target_dir, "same.png");
    gchar *other_png = write_png_file_in_dir(other_dir, "same.png");
    PixelTermApp app = {0};

    clear_browser_test_files(NULL);
    g_test_queue_destroy(clear_browser_test_files, NULL);
    g_browser_test_files = g_list_append(g_browser_test_files, g_strdup(other_png));
    g_browser_test_files = g_list_append(g_browser_test_files, g_strdup(target_png));

    app.preload_enabled = FALSE;
    app.preview.selected_link_index = -1;

    g_assert_cmpint(app_load_single_file(&app, target_png), ==, ERROR_NONE);
    g_assert_cmpstr((const gchar *)g_list_nth_data(app.image_files, app.current_index), ==, target_png);

    g_list_free_full(app.image_files, g_free);
    g_free(app.current_directory);
    g_free(target_png);
    g_free(other_png);
    g_free(target_dir);
    g_free(other_dir);
}

static void test_render_selected_row_uses_full_width_highlight_with_size(void) {
    gchar *dir = create_temp_dir();
    gchar *a_png = write_file_in_dir(dir, "a.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};

    init_file_manager_app(&app, dir, 24);
    app.term_width = 80;

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app_file_manager_select_path(&app, a_png), ==, ERROR_NONE);

    gchar *output = capture_render_output(render_file_manager_capture, &app);

    g_assert_nonnull(g_strstr_len(output, -1, "\033[47;30m[IMG]  a.png"));
    g_assert_nonnull(g_strstr_len(output, -1, "8 B"));
    g_assert_nonnull(g_strstr_len(output, -1, "8 B   \033[0m"));

    g_free(output);
    cleanup_file_manager_app(&app);
    g_free(a_png);
    g_free(dir);
}

static void test_render_marks_directory_containing_media(void) {
    gchar *dir = create_temp_dir();
    gchar *child_dir = create_dir_in_dir(dir, "child");
    gchar *child_png = write_file_in_dir(child_dir, "inside.png", k_png_data, sizeof(k_png_data));
    gchar *a_png = write_file_in_dir(dir, "a.png", k_png_data, sizeof(k_png_data));
    PixelTermApp app = {0};

    init_file_manager_app(&app, dir, 24);
    app.term_width = 80;

    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app_file_manager_select_path(&app, a_png), ==, ERROR_NONE);

    gchar *output = capture_render_output(render_file_manager_capture, &app);

    g_assert_nonnull(g_strstr_len(output, -1, "\033[33m[DIR]  child/"));

    g_free(output);
    cleanup_file_manager_app(&app);
    g_free(child_dir);
    g_free(child_png);
    g_free(a_png);
    g_free(dir);
}

typedef struct {
    gboolean fail;
    gboolean fail_after_clear;
    GString *attempted;
    gsize first_length;
} FileManagerOutput;

static ssize_t file_manager_output_write(void *data, const char *bytes, size_t length) {
    FileManagerOutput *sink = data;
    if (sink->attempted->len == 0) sink->first_length = length;
    g_string_append_len(sink->attempted, bytes, length);
    if (sink->fail || (sink->fail_after_clear && sink->attempted->len > sink->first_length)) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)length;
}

static void test_enter_output_failure_preserves_state(void) {
    gchar *dir = create_temp_dir();
    PixelTermApp app = {0};
    init_file_manager_app(&app, dir, 24);
    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    app.mode = APP_MODE_PREVIEW;
    app.info_visible = TRUE;
    app.help_visible = TRUE;
    app.show_hidden_files = TRUE;
    app.file_manager.selected_entry = 7;
    app.file_manager.scroll_offset = 3;
    gchar *directory = app.file_manager.directory;
    GList *entries = app.file_manager.entries;
    gint entries_count = app.file_manager.entries_count;

    FileManagerOutput sink = {.fail = TRUE, .attempted = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, file_manager_output_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    g_media_stop_calls = 0;
    ErrorCode result = app_enter_file_manager(&app);
    gchar *attempted = g_strndup(sink.attempted->str, sink.first_length);
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;

    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(recovered);
    g_assert_cmpint(app.mode, ==, APP_MODE_PREVIEW);
    g_assert_true(app.info_visible);
    g_assert_true(app.help_visible);
    g_assert_cmpint(g_media_stop_calls, ==, 1);
    g_assert_cmpstr(attempted, ==, "\033[2J\033[H\033[0m");
    g_free(attempted);
    g_assert_true(app.show_hidden_files);
    g_assert_true(app.file_manager.directory == directory);
    g_assert_true(app.file_manager.entries == entries);
    g_assert_cmpint(app.file_manager.entries_count, ==, entries_count);
    g_assert_cmpint(app.file_manager.selected_entry, ==, 7);
    g_assert_cmpint(app.file_manager.scroll_offset, ==, 3);
    g_assert_true(app.needs_redraw);
    g_assert_true(app.needs_screen_clear);
    g_string_free(sink.attempted, TRUE);
    cleanup_file_manager_app(&app);
    g_free(dir);
}

static void test_enter_stop_failure_keeps_display(void) {
    PixelTermApp app = {0};
    init_file_manager_app(&app, "/", 24);
    app.mode = APP_MODE_SINGLE;
    gchar *directory = app.file_manager.directory;
    FileManagerOutput sink = {.attempted = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, file_manager_output_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    g_media_stop_error = ERROR_OUTPUT_IO;
    g_media_stop_calls = 0;
    ErrorCode result = app_enter_file_manager(&app);
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    g_media_stop_error = ERROR_NONE;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpstr(sink.attempted->str, ==, "");
    g_assert_cmpint(g_media_stop_calls, ==, 1);
    g_assert_cmpint(app.mode, ==, APP_MODE_SINGLE);
    g_assert_true(app.file_manager.directory == directory);
    g_string_free(sink.attempted, TRUE);
    cleanup_file_manager_app(&app);
}

static void test_enter_with_previous_error(void) {
    gchar *dir = create_temp_dir();
    PixelTermApp app = {0};
    init_file_manager_app(&app, dir, 24);
    app.mode = APP_MODE_SINGLE;
    app.current_directory = g_strdup(dir);
    app.last_error = ERROR_OUTPUT_IO;
    g_assert_cmpint(app_enter_file_manager(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_FILE_MANAGER);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    cleanup_file_manager_app(&app);
    g_free(app.current_directory);
    g_free(dir);
}

static void test_enter_missing_directory_redraw(gconstpointer data) {
    gboolean redraw_failure = GPOINTER_TO_INT(data);
    gchar *dir = create_temp_dir();
    PixelTermApp app = {0};
    app.mode = APP_MODE_SINGLE;
    app.current_directory = g_build_filename(dir, "missing\033[31m", NULL);
    app.info_visible = TRUE;
    app.help_visible = TRUE;
    FileManagerOutput sink = {.fail_after_clear = redraw_failure, .attempted = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, file_manager_output_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = app_enter_file_manager(&app);
    sink.fail_after_clear = FALSE;
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;

    g_assert_cmpint(result, ==, redraw_failure ? ERROR_OUTPUT_IO : ERROR_FILE_NOT_FOUND);
    g_assert_cmpint(app.mode, ==, APP_MODE_FILE_MANAGER);
    g_assert_null(app.file_manager.entries);
    g_assert_cmpint(app.file_manager.entries_count, ==, 0);
    g_assert_nonnull(strstr(sink.attempted->str, "PixelTerm File Manager"));
    g_assert_nonnull(strstr(sink.attempted->str, "No items"));
    g_assert_null(strstr(sink.attempted->str, "missing\033[31m"));
    g_assert_nonnull(strstr(sink.attempted->str, "missing"));
    if (redraw_failure) {
        g_assert_true(app.needs_redraw);
        g_assert_true(app.needs_screen_clear);
    }
    g_string_free(sink.attempted, TRUE);
    cleanup_file_manager_app(&app);
    g_free(app.current_directory);
    g_free(dir);
}

static void test_enter_unreadable_directory_preserves_selection(void) {
    gchar *dir = create_temp_dir();
    gchar *child = create_dir_in_dir(dir, "child");
    PixelTermApp app = {0};
    init_file_manager_app(&app, dir, 24);
    g_assert_cmpint(app_file_manager_refresh(&app), ==, ERROR_NONE);
    g_assert_cmpint(app_file_manager_select_path(&app, child), ==, ERROR_NONE);
    gint selection = app.file_manager.selected_entry;
    g_assert_cmpint(selection, >, 0);
    app.file_manager.scroll_offset = 3;
    GList *entries = app.file_manager.entries;
    GList *selected = app.file_manager.selected_link;
    gchar *directory = app.file_manager.directory;
    g_assert_cmpint(g_chmod(child, 0000), ==, 0);
    GDir *readable = g_dir_open(child, 0, NULL);
    if (readable) {
        g_dir_close(readable);
        g_test_skip("Directory permissions do not prevent reads for this user");
    } else {
        ErrorCode result = app_file_manager_enter(&app);
        g_assert_cmpint(g_chmod(child, 0700), ==, 0);
        g_assert_cmpint(result, ==, ERROR_FILE_NOT_FOUND);
        g_assert_cmpstr(app.file_manager.directory, ==, dir);
        g_assert_true(app.file_manager.directory == directory);
        g_assert_true(app.file_manager.entries == entries);
        g_assert_cmpint(app.file_manager.selected_entry, ==, selection);
        g_assert_cmpint(app.file_manager.scroll_offset, ==, 3);
        g_assert_true(app.file_manager.selected_link == selected);
        g_assert_cmpint(app.file_manager.selected_link_index, ==, selection);
        g_assert_cmpstr(selected_path(&app), ==, child);
    }
    g_assert_cmpint(g_chmod(child, 0700), ==, 0);
    cleanup_file_manager_app(&app);
    g_free(child);
    g_free(dir);
}

static void test_load_single_file_missing_from_scan_preserves_media(void) {
    gchar *dir = create_temp_dir();
    gchar *path = write_png_file_in_dir(dir, "selected.png");
    PixelTermApp app = {0};
    app.current_directory = g_strdup("/old");
    app.image_files = g_list_append(NULL, g_strdup("/old/one.png"));
    app.image_files = g_list_append(app.image_files, g_strdup("/old/two.png"));
    app.total_images = 2;
    app.current_index = 1;
    app.preview.selected_link = app.image_files->next;
    app.preview.selected_link_index = 1;
    app.return_to_mode = RETURN_MODE_PREVIEW;
    app.image_zoom = 2.0;
    GList *images = app.image_files;
    gchar *directory = app.current_directory;
    clear_browser_test_files(NULL);
    g_browser_test_files = g_list_append(NULL, g_build_filename(dir, "other.png", NULL));
    g_preloader_reset_calls = 0;
    g_assert_cmpint(app_load_single_file(&app, path), ==, ERROR_FILE_NOT_FOUND);
    g_assert_true(app.image_files == images);
    g_assert_true(app.current_directory == directory);
    g_assert_cmpint(g_preloader_reset_calls, ==, 0);
    g_assert_cmpint(app.total_images, ==, 2);
    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_true(app.preview.selected_link == images->next);
    g_assert_cmpint(app.preview.selected_link_index, ==, 1);
    g_assert_cmpint(app.return_to_mode, ==, RETURN_MODE_PREVIEW);
    g_assert_cmpfloat(app.image_zoom, ==, 2.0);
    g_assert_cmpstr(app_get_current_filepath(&app), ==, "/old/two.png");
    clear_browser_test_files(NULL);
    g_list_free_full(app.image_files, g_free);
    g_free(app.current_directory);
    g_free(path);
    g_free(dir);
}

static void test_load_directory_atomic(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    PixelTermApp app = {0};
    app.current_directory = g_strdup("/old");
    app.image_files = g_list_append(NULL, g_strdup("/old/one.png"));
    app.image_files = g_list_append(app.image_files, g_strdup("/old/two.png"));
    app.total_images = 2;
    app.current_index = 1;
    app.preview.selected_link = app.image_files->next;
    app.preview.selected_link_index = 1;
    GList *old_images = app.image_files;
    GList *old_selection = app.preview.selected_link;
    gchar *old_directory = app.current_directory;
    clear_browser_test_files(NULL);
    if (scenario != 3) {
        g_browser_test_files = g_list_append(NULL, g_strdup("/old/ten.png"));
        g_browser_test_files = g_list_append(g_browser_test_files, g_strdup("/old/two.png"));
    }
    g_browser_create_failure = scenario == 0;
    g_browser_scan_error = scenario == 1 ? ERROR_FILE_NOT_FOUND : ERROR_NONE;
    g_preloader_reset_calls = g_browser_scan_calls = 0;

    ErrorCode result = app_load_directory(&app, app.current_directory);

    g_assert_cmpint(result, ==, scenario == 0 ? ERROR_MEMORY_ALLOC
                              : scenario == 1 ? ERROR_FILE_NOT_FOUND : ERROR_NONE);
    g_assert_cmpint(g_browser_scan_calls, ==, scenario == 0 ? 0 : 1);
    g_assert_cmpint(g_preloader_reset_calls, ==, scenario < 2 ? 0 : 1);
    if (scenario < 2) {
        g_assert_true(app.image_files == old_images);
        g_assert_true(app.current_directory == old_directory);
        g_assert_cmpstr(app.current_directory, ==, "/old");
        g_assert_cmpint(app.total_images, ==, 2);
        g_assert_cmpint(app.current_index, ==, 1);
        g_assert_true(app.preview.selected_link == old_selection);
        g_assert_cmpint(app.preview.selected_link_index, ==, 1);
        g_assert_cmpstr(app_get_current_filepath(&app), ==, "/old/two.png");
    } else {
        g_assert_cmpstr(app.current_directory, ==, "/old");
        g_assert_cmpint(app.total_images, ==, scenario == 3 ? 0 : 2);
        g_assert_cmpint(app.current_index, ==, 0);
        g_assert_true(app.preview.selected_link == app.image_files);
        g_assert_cmpint(app.preview.selected_link_index, ==, scenario == 3 ? -1 : 0);
        if (scenario == 3) {
            g_assert_null(app.image_files);
        } else {
            clear_browser_test_files(NULL);
            g_assert_cmpstr(app.image_files->data, ==, "/old/ten.png");
            g_assert_cmpstr(app.image_files->next->data, ==, "/old/two.png");
        }
    }
    g_browser_create_failure = FALSE;
    g_browser_scan_error = ERROR_NONE;
    clear_browser_test_files(NULL);
    g_list_free_full(app.image_files, g_free);
    g_free(app.current_directory);
}

static void test_open_loaded_file_output(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    gboolean book = scenario & 1;
    gboolean clear_failure = scenario & 2;
    gboolean entry_failure = scenario & 4;
    gboolean render_failure = scenario & 8;
    gboolean preparation_failure = scenario & 16;
    gboolean load_failure = scenario & 32;
    gboolean redraw_failure = scenario & 64;
    gboolean invalid_render = scenario & 128;
    gchar *dir = create_temp_dir();
    gchar *path = book ? write_file_in_dir(dir, "book.pdf", (const guint8 *)"%PDF", 4)
                       : write_png_file_in_dir(dir, "image.png");
    PixelTermApp app = {0};
    init_file_manager_app(&app, dir, 24);
    app.file_manager.entries = g_list_append(NULL, g_strdup(path));
    app.file_manager.entries_count = 1;
    GList *entries = app.file_manager.entries;
    app.file_manager.selected_link = entries;
    app.file_manager.selected_link_index = 0;
    gchar *listing_directory = app.file_manager.directory;
    app.last_error = ERROR_OUTPUT_IO;
    app.info_visible = TRUE;
    app.help_visible = TRUE;
    app.image_files = g_list_append(NULL, g_strdup("old.png"));
    app.return_to_mode = RETURN_MODE_PREVIEW_VIRTUAL;
    app.image_zoom = 2.0;
    app.image_pan_x = 3.0;
    app.image_pan_y = 4.0;
    app.preloader = (ImagePreloader *)0x1;
    app.preload_enabled = TRUE;
    app.async.image_pending = TRUE;
    app.async.image_path = g_strdup("old.png");
    gchar *old_async_path = app.async.image_path;
    app.total_images = 1;
    GList *old_images = app.image_files;
    app.preview.selected_link = old_images;
    app.preview.selected_link_index = 0;
    app.current_directory = g_strdup("/old");
    gchar *old_directory = app.current_directory;
    app.book = existing_book();
    BookState old_book = app.book;
    clear_browser_test_files(NULL);
    g_browser_test_files = g_list_append(NULL, g_strdup(path));
    g_book_open_success = !load_failure;
    g_browser_scan_error = load_failure ? ERROR_FILE_NOT_FOUND : ERROR_NONE;
    g_book_entry_error = entry_failure ? ERROR_OUTPUT_IO : ERROR_NONE;
    g_render_error = render_failure ? (invalid_render ? (scenario & 256 ? ERROR_MEMORY_ALLOC : ERROR_INVALID_IMAGE) : ERROR_OUTPUT_IO) : ERROR_NONE;
    g_media_stop_error = preparation_failure ? ERROR_OUTPUT_IO : ERROR_NONE;
    g_book_entry_calls = g_render_calls = 0;
    g_book_open_calls = g_book_close_calls = g_browser_scan_calls = 0;
    g_old_book_close_calls = g_old_toc_free_calls = 0;
    g_live_preloader_resets = g_preloader_enables = 0;
    FileManagerOutput sink = {.fail = clear_failure, .fail_after_clear = redraw_failure,
                             .attempted = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, file_manager_output_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = app_file_manager_enter(&app);
    gchar *attempted = g_strndup(sink.attempted->str, sink.first_length);
    sink.fail = FALSE;
    sink.fail_after_clear = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    ErrorCode expected = (clear_failure || entry_failure || (render_failure && !invalid_render) || preparation_failure || redraw_failure)
                         ? ERROR_OUTPUT_IO : load_failure
                         ? (book ? ERROR_INVALID_IMAGE : ERROR_FILE_NOT_FOUND) : invalid_render ? g_render_error : ERROR_NONE;
    g_assert_cmpint(result, ==, expected);
    g_assert_true(recovered);
    if (clear_failure || preparation_failure) {
        g_assert_true(app.image_files == old_images);
        g_assert_cmpstr(app.image_files->data, ==, "old.png");
        g_assert_true(app.book.doc == (BookDocument *)0x2);
        g_assert_cmpstr(app.book.path, ==, "old.pdf");
        g_assert_true(app.info_visible);
        g_assert_true(app.help_visible);
    } else if (load_failure) {
        if (book) {
            g_assert_cmpint(g_book_close_calls, ==, 0);
            g_assert_cmpmem(&app.book, sizeof(app.book), &old_book, sizeof(old_book));
            g_assert_true(app.current_directory == old_directory);
            g_assert_true(app.image_files == old_images);
            g_assert_cmpstr(app.image_files->data, ==, "old.png");
            g_assert_cmpint(app.total_images, ==, 1);
        } else {
            g_assert_cmpint(g_book_close_calls, ==, 0);
            g_assert_true(app.image_files == old_images);
            g_assert_cmpstr(app.image_files->data, ==, "old.png");
            g_assert_cmpint(app.total_images, ==, 1);
            g_assert_true(app.current_directory == old_directory);
            g_assert_cmpstr(app.current_directory, ==, "/old");
            g_assert_cmpint(app.current_index, ==, 0);
            g_assert_true(app.preview.selected_link == old_images);
            g_assert_cmpint(app.preview.selected_link_index, ==, 0);
            g_assert_cmpstr(app_get_current_filepath(&app), ==, "old.png");
            g_assert_true(app.book.doc == (BookDocument *)0x2);
            g_assert_cmpstr(app.book.path, ==, "old.pdf");
        }
        g_assert_nonnull(strstr(sink.attempted->str, "PixelTerm File Manager"));
        g_assert_nonnull(strstr(sink.attempted->str, book ? "book.pdf" : "image.png"));
    } else if (!book && render_failure) {
        g_assert_true(app.image_files == old_images);
        g_assert_true(app.current_directory == old_directory);
        g_assert_true(app.preview.selected_link == old_images);
        g_assert_cmpint(app.preview.selected_link_index, ==, 0);
        g_assert_cmpint(app.total_images, ==, 1);
        g_assert_cmpstr(app_get_current_filepath(&app), ==, "old.png");
        g_assert_cmpint(app.return_to_mode, ==, RETURN_MODE_PREVIEW_VIRTUAL);
        g_assert_cmpfloat(app.image_zoom, ==, 2.0);
        g_assert_cmpfloat(app.image_pan_x, ==, 3.0);
        g_assert_cmpfloat(app.image_pan_y, ==, 4.0);
    } else if (book && (entry_failure || render_failure)) {
        g_assert_cmpmem(&app.book, sizeof(app.book), &old_book, sizeof(old_book));
        g_assert_cmpint(g_book_close_calls, ==, 1);
        g_assert_true(app.current_directory == old_directory);
        g_assert_cmpstr(app.current_directory, ==, "/old");
        g_assert_true(app.image_files == old_images);
    } else if (!load_failure && book) {
        g_assert_nonnull(app.book.doc);
        g_assert_cmpstr(app.book.path, ==, path);
        g_assert_cmpint(g_book_close_calls, ==, 1);
    } else if (!load_failure) {
        g_assert_cmpint(app.total_images, ==, 1);
        g_assert_cmpstr(app.image_files->data, ==, path);
    }
    g_assert_cmpint(g_book_open_calls, ==, book && !clear_failure && !preparation_failure ? 1 : 0);
    gboolean replaced_book = book && !clear_failure && !preparation_failure && !load_failure && !entry_failure && !render_failure;
    g_assert_cmpint(g_old_book_close_calls, ==, replaced_book);
    g_assert_cmpint(g_old_toc_free_calls, ==, replaced_book);
    g_assert_cmpint(g_browser_scan_calls, ==, !book && !clear_failure && !preparation_failure ? 1 : 0);
    g_assert_cmpint(g_book_entry_calls, ==, book && !clear_failure && !preparation_failure && !load_failure ? 1 : 0);
    g_assert_cmpint(g_render_calls, ==, !clear_failure && !entry_failure && !preparation_failure && !load_failure ? 1 : 0);
    if (clear_failure || preparation_failure || load_failure || entry_failure || render_failure) {
        g_assert_true(app.preloader == (ImagePreloader *)0x1);
        g_assert_true(app.preload_enabled);
        g_assert_cmpint(g_live_preloader_resets, ==, 0);
        g_assert_cmpint(g_preloader_enables, ==, 0);
        g_assert_true(app.async.image_path == old_async_path);
        g_assert_true(app.async.image_pending);
        g_assert_true(app.info_visible);
        g_assert_true(app.help_visible);
        g_assert_true(app.file_manager.entries == entries);
        g_assert_true(app.file_manager.directory == listing_directory);
        g_assert_cmpint(app.file_manager.entries_count, ==, 1);
        g_assert_cmpint(app.file_manager.selected_entry, ==, 0);
        g_assert_cmpint(app.file_manager.scroll_offset, ==, 0);
        g_assert_true(app.file_manager.selected_link == entries);
        g_assert_cmpint(app.file_manager.selected_link_index, ==, 0);
        g_assert_cmpstr(selected_path(&app), ==, path);
        g_assert_cmpint(app.mode, ==, APP_MODE_FILE_MANAGER);
        g_assert_cmpstr(attempted, ==, preparation_failure ? "" : "\033[2J\033[H\033[0m");
        if (load_failure || entry_failure || render_failure) {
            g_assert_true(app.needs_redraw);
            g_assert_true(app.needs_screen_clear);
        }
        if (invalid_render) g_assert_nonnull(strstr(sink.attempted->str, "PixelTerm File Manager"));
        if (render_failure && !invalid_render) g_assert_cmpuint(sink.attempted->len, ==, sink.first_length + 14);
    } else {
        g_assert_null(app.file_manager.entries);
        g_assert_null(app.file_manager.directory);
        g_assert_cmpint(app.mode, ==, book && !entry_failure && !preparation_failure ? APP_MODE_BOOK_PREVIEW : APP_MODE_SINGLE);
    }
    g_free(attempted);
    g_string_free(sink.attempted, TRUE);
    g_book_open_success = FALSE;
    g_book_entry_error = g_render_error = ERROR_NONE;
    g_browser_scan_error = ERROR_NONE;
    g_media_stop_error = ERROR_NONE;
    app_close_book(&app);
    cleanup_file_manager_app(&app);
    g_list_free_full(app.image_files, g_free);
    g_free(app.current_directory);
    g_free(app.async.image_path);
    clear_browser_test_files(NULL);
    g_free(path);
    g_free(dir);
}

static void test_render_output_failure(gconstpointer data) {
    gboolean pending = GPOINTER_TO_INT(data);
    PixelTermApp app = {0};
    init_file_manager_app(&app, "/", 24);
    app.file_manager.scroll_offset = 7;
    FileManagerOutput sink = {.fail = TRUE, .attempted = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, file_manager_output_write);
    g_assert_nonnull(stream);
    char buffer[65536];
    g_assert_cmpint(setvbuf(stream, buffer, _IOFBF, sizeof(buffer)), ==, 0);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    if (pending) terminal_output_defer("pending-control", TERMINAL_OUTPUT_RESTORE);
    ErrorCode result = app_render_file_manager(&app);
    gchar *attempted = g_strdup(sink.attempted->str);
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;

    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(recovered);
    g_assert_true(app.needs_redraw);
    g_assert_true(app.needs_screen_clear);
    if (pending) {
        g_assert_cmpstr(attempted, ==, "\033\\pending-control");
        g_assert_cmpint(app.file_manager.scroll_offset, ==, 7);
    } else {
        g_assert_nonnull(strstr(attempted, "PixelTerm File Manager"));
    }
    g_free(attempted);
    g_string_free(sink.attempted, TRUE);
    cleanup_file_manager_app(&app);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/app_file_manager/enter/unreadable_preserves_selection", test_enter_unreadable_directory_preserves_selection);
    g_test_add_func("/app_core/load_single_file/missing_from_scan_preserves_media", test_load_single_file_missing_from_scan_preserves_media);
    g_test_add_data_func("/app_file_manager/load_directory/create_failure", GINT_TO_POINTER(0), test_load_directory_atomic);
    g_test_add_data_func("/app_file_manager/load_directory/scan_failure", GINT_TO_POINTER(1), test_load_directory_atomic);
    g_test_add_data_func("/app_file_manager/load_directory/replacement", GINT_TO_POINTER(2), test_load_directory_atomic);
    g_test_add_data_func("/app_file_manager/load_directory/empty", GINT_TO_POINTER(3), test_load_directory_atomic);
    g_test_add_data_func("/app_file_manager/entry/missing_directory_redraw", GINT_TO_POINTER(FALSE), test_enter_missing_directory_redraw);
    g_test_add_data_func("/app_file_manager/entry/missing_directory_redraw_failure", GINT_TO_POINTER(TRUE), test_enter_missing_directory_redraw);
    g_test_add_func("/app_core/open_book/failure_preserves_book", test_open_book_failure_preserves_book);
    g_test_add_data_func("/app_file_manager/open/image_invalid_render", GINT_TO_POINTER(136), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_invalid_render", GINT_TO_POINTER(137), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/image_invalid_render_redraw_failure", GINT_TO_POINTER(200), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_invalid_render_redraw_failure", GINT_TO_POINTER(201), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/image_renderer_setup_failure", GINT_TO_POINTER(392), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_renderer_setup_failure", GINT_TO_POINTER(393), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/image_load_redraw_failure", GINT_TO_POINTER(96), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_load_redraw_failure", GINT_TO_POINTER(97), test_open_loaded_file_output);
    g_test_add_func("/app_file_manager/entry/previous_error", test_enter_with_previous_error);
    g_test_add_data_func("/app_file_manager/open/image_load_failure", GINT_TO_POINTER(32), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_load_failure", GINT_TO_POINTER(33), test_open_loaded_file_output);
    g_test_add_func("/app_file_manager/entry/stop_failure_keeps_display", test_enter_stop_failure_keeps_display);
    g_test_add_data_func("/app_file_manager/open/image_success", GINT_TO_POINTER(0), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_success", GINT_TO_POINTER(1), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/image_clear_failure", GINT_TO_POINTER(2), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_clear_failure", GINT_TO_POINTER(3), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_entry_failure", GINT_TO_POINTER(5), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/image_render_failure", GINT_TO_POINTER(8), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_render_failure", GINT_TO_POINTER(9), test_open_loaded_file_output);
    g_test_add_data_func("/app_file_manager/open/book_preparation_failure", GINT_TO_POINTER(17), test_open_loaded_file_output);

    g_test_add_func("/app_file_manager/entry/output_failure_preserves_state",
                    test_enter_output_failure_preserves_state);
    g_test_add_data_func("/app_file_manager/render/final_flush_failure", GINT_TO_POINTER(FALSE),
                         test_render_output_failure);
    g_test_add_data_func("/app_file_manager/render/pending_output_failure", GINT_TO_POINTER(TRUE),
                         test_render_output_failure);

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
    g_test_add_func("/app_file_manager/refresh/failure_preserves_previous_directory_and_entries",
                    test_refresh_failure_preserves_previous_directory_and_entries);
    g_test_add_func("/app_file_manager/toggle_hidden/preserves_visible_selection",
                    test_toggle_hidden_preserves_visible_selection);
    g_test_add_func("/app_file_manager/toggle_hidden/clamps_selection_when_hidden_entry_disappears_after_preview_return",
                    test_toggle_hidden_clamps_selection_when_hidden_entry_disappears_after_preview_return);
    g_test_add_func("/app_file_manager/navigation/wraps_and_clamps_scroll_at_paging_boundaries",
                    test_navigation_wraps_and_clamps_scroll_at_paging_boundaries);
    g_test_add_func("/app_file_manager/enter/child_directory_defaults_to_first_real_entry",
                    test_enter_child_directory_defaults_to_first_real_entry);
    g_test_add_func("/app_file_manager/enter/parent_entry_selects_directory_just_left",
                    test_enter_parent_entry_selects_directory_just_left);
    g_test_add_func("/app_core/load_single_file/matches_full_canonical_path_before_basename",
                    test_load_single_file_matches_full_canonical_path_before_basename);
    g_test_add_func("/app_file_manager/render/selected_row_uses_full_width_highlight_with_size",
                    test_render_selected_row_uses_full_width_highlight_with_size);
    g_test_add_func("/app_file_manager/render/marks_directory_containing_media",
                    test_render_marks_directory_containing_media);

    return g_test_run();
}
