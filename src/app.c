#define _GNU_SOURCE

#include "app.h"
#include "browser.h"
#include "renderer.h"
#include "input.h"
#include "preloader.h"
#include "book.h"
#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static const gdouble k_book_spread_ratio = 1.0;
static const gint k_book_spread_min_cols = 120;
static const gint k_book_spread_min_rows = 24;
static const gint k_book_spread_min_page_cols = 60;
static const gint k_book_spread_gutter_cols = 2;
static const gdouble k_book_cell_aspect = 0.5;

static void app_get_image_target_dimensions(const PixelTermApp *app, gint *max_width, gint *max_height);
static GdkPixbuf* app_load_pixbuf_from_stream(const char *filepath, GError **error);

// Create a new application instance
PixelTermApp* app_create(void) {
    PixelTermApp *app = g_new0(PixelTermApp, 1);
    if (!app) {
        return NULL;
    }

    // Initialize all pointers to NULL
    app->canvas = NULL;
    app->canvas_config = NULL;
    app->term_info = NULL;
    app->image_files = NULL;
    app->current_directory = NULL;
    app->preloader = NULL;
    app->gif_player = NULL;
    app->gerror = NULL;

    // Set default state
    app->current_index = 0;
    app->total_images = 0;
    app->running = TRUE;
    app->info_visible = FALSE;
    app->ui_text_hidden = FALSE;
    app->show_fps = FALSE;
    app->video_scale = 1.0;
    app->clear_workaround_enabled = FALSE;
    app->preload_enabled = TRUE;
    app->dither_enabled = FALSE;
    app->render_work_factor = 9;
    app->gamma = 1.0;
    app->force_text = FALSE;
    app->force_sixel = FALSE;
    app->force_kitty = FALSE;
    app->force_iterm2 = FALSE;
    app->needs_redraw = TRUE;
    app->file_manager_mode = FALSE;
    app->show_hidden_files = FALSE;
    app->preview_mode = FALSE;
    app->preview_zoom = 0; // 0 indicates uninitialized target cell width
    app->book_mode = FALSE;
    app->book_preview_mode = FALSE;
    app->return_to_mode = RETURN_MODE_NONE;
    app->delete_pending = FALSE;
    app->async_render_request = FALSE;
    app->async_image_pending = FALSE;
    app->async_render_force_sync = FALSE;
    app->async_image_index = -1;
    app->async_image_path = NULL;
    app->last_render_top_row = 0;
    app->last_render_height = 0;
    app->image_zoom = 1.0;
    app->image_pan_x = 0.0;
    app->image_pan_y = 0.0;
    app->image_view_left_col = 0;
    app->image_view_top_row = 0;
    app->image_view_width = 0;
    app->image_view_height = 0;
    app->image_viewport_px_w = 0;
    app->image_viewport_px_h = 0;
    app->term_width = 80;
    app->term_height = 24;
    app->last_error = ERROR_NONE;
    app->file_manager_directory = NULL;
    app->directory_entries = NULL;
    app->selected_entry = 0;
    app->scroll_offset = 0;
    app->preview_selected = 0;
    app->preview_scroll = 0;
    app->needs_screen_clear = FALSE;
    app->book_doc = NULL;
    app->book_path = NULL;
    app->book_page = 0;
    app->book_page_count = 0;
    app->book_preview_selected = 0;
    app->book_preview_scroll = 0;
    app->book_preview_zoom = 0;
    app->book_jump_active = FALSE;
    app->book_jump_dirty = FALSE;
    app->book_jump_len = 0;
    app->book_jump_buf[0] = '\0';
    app->book_toc = NULL;
    app->book_toc_selected = 0;
    app->book_toc_scroll = 0;
    app->book_toc_visible = FALSE;
    app->pending_single_click = FALSE;
    app->pending_click_time = 0;
    app->pending_grid_single_click = FALSE;
    app->pending_grid_click_time = 0;
    app->pending_grid_click_x = 0;
    app->pending_grid_click_y = 0;
    app->pending_file_manager_single_click = FALSE;
    app->pending_file_manager_click_time = 0;
    app->pending_file_manager_click_x = 0;
    app->pending_file_manager_click_y = 0;
    app->last_mouse_x = 0;
    app->last_mouse_y = 0;
    
    return app;
}

gboolean app_book_use_double_page(const PixelTermApp *app) {
    if (!app || !app->book_mode) {
        return FALSE;
    }
    gint width = 0;
    gint height = 0;
    app_get_image_target_dimensions(app, &width, &height);
    gint term_width = app->term_width > 0 ? app->term_width : width;
    gint term_height = app->term_height > 0 ? app->term_height : height;
    if (width <= 0) {
        width = app->term_width > 0 ? app->term_width : 80;
    }
    if (height <= 0) {
        height = app->term_height > 0 ? app->term_height : 24;
    }
    if (term_width < k_book_spread_min_cols || term_height < k_book_spread_min_rows) {
        return FALSE;
    }
    if (width < k_book_spread_min_page_cols * 2 + k_book_spread_gutter_cols) {
        return FALSE;
    }
    gdouble ratio = (gdouble)term_width / (gdouble)term_height;
    ratio *= k_book_cell_aspect;
    return ratio >= k_book_spread_ratio;
}

static GdkPixbuf* app_load_pixbuf_from_stream(const char *filepath, GError **error) {
    if (!filepath) {
        return NULL;
    }
    GFile *file = g_file_new_for_path(filepath);
    if (!file) {
        return NULL;
    }
    GFileInputStream *stream = g_file_read(file, NULL, error);
    g_object_unref(file);
    if (!stream) {
        return NULL;
    }
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream(G_INPUT_STREAM(stream), NULL, error);
    g_object_unref(stream);
    return pixbuf;
}

// Replace control characters to avoid terminal escape injection when printing paths
static gchar* sanitize_for_terminal(const gchar *text) {
    if (!text) {
        return g_strdup("");
    }

    gchar *safe = g_strdup(text);
    for (gchar *p = safe; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f || c == '\033') {
            *p = '?';
        }
    }
    return safe;
}

// Calculate display width of a UTF-8 string for proper centering
static gint utf8_display_width(const gchar *text) {
    if (!text) {
        return 0;
    }

    gint width = 0;
    const gchar *p = text;
    while (*p) {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
            // Invalid sequence: treat current byte as a single-width placeholder
            width++;
            p++;
            continue;
        }

        if (!g_unichar_iszerowidth(ch)) {
            width += g_unichar_iswide(ch) ? 2 : 1;
        }
        p = g_utf8_next_char(p);
    }

    return width;
}

static gchar* utf8_prefix_by_width(const gchar *text, gint max_width) {
    if (!text || max_width <= 0) {
        return g_strdup("");
    }

    gint width = 0;
    const gchar *p = text;
    const gchar *end = text;
    while (*p) {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        gint char_width = 1;
        const gchar *next = p + 1;
        if (ch != (gunichar)-1 && ch != (gunichar)-2) {
            if (g_unichar_iszerowidth(ch)) {
                char_width = 0;
            } else {
                char_width = g_unichar_iswide(ch) ? 2 : 1;
            }
            next = g_utf8_next_char(p);
        }
        if (width + char_width > max_width) {
            break;
        }
        width += char_width;
        end = next;
        p = next;
    }

    return g_strndup(text, end - text);
}

static gchar* utf8_suffix_by_width(const gchar *text, gint max_width) {
    if (!text || max_width <= 0) {
        return g_strdup("");
    }

    const gchar *end = text + strlen(text);
    const gchar *p = end;
    const gchar *start = end;
    gint width = 0;
    while (p > text) {
        const gchar *prev = g_utf8_prev_char(p);
        gunichar ch = g_utf8_get_char_validated(prev, -1);
        gint char_width = 1;
        if (ch != (gunichar)-1 && ch != (gunichar)-2) {
            if (g_unichar_iszerowidth(ch)) {
                char_width = 0;
            } else {
                char_width = g_unichar_iswide(ch) ? 2 : 1;
            }
        }
        if (width + char_width > max_width) {
            break;
        }
        width += char_width;
        start = prev;
        p = prev;
    }

    return g_strndup(start, end - start);
}

// Truncate UTF-8 text to a given display width, appending "..." when needed.
static gchar* truncate_utf8_for_display(const gchar *text, gint max_width) {
    if (!text || max_width <= 0) {
        return g_strdup("");
    }

    gint width = utf8_display_width(text);
    if (width <= max_width) {
        return g_strdup(text);
    }

    if (max_width <= 3) {
        gchar *dots = g_malloc((gsize)max_width + 1);
        memset(dots, '.', (gsize)max_width);
        dots[max_width] = '\0';
        return dots;
    }

    gint target_width = max_width - 3;
    gint current_width = 0;
    const gchar *p = text;
    const gchar *end = text;
    while (*p) {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        gint char_width = 1;
        const gchar *next = p + 1;
        if (ch != (gunichar)-1 && ch != (gunichar)-2) {
            if (g_unichar_iszerowidth(ch)) {
                char_width = 0;
            } else {
                char_width = g_unichar_iswide(ch) ? 2 : 1;
            }
            next = g_utf8_next_char(p);
        }
        if (current_width + char_width > target_width) {
            break;
        }
        current_width += char_width;
        end = next;
        p = next;
    }

    gchar *prefix = g_strndup(text, end - text);
    gchar *result = g_strdup_printf("%s...", prefix);
    g_free(prefix);
    return result;
}

static gchar* truncate_utf8_middle_keep_suffix(const gchar *text, gint max_width) {
    if (!text || max_width <= 0) {
        return g_strdup("");
    }

    gint width = utf8_display_width(text);
    if (width <= max_width) {
        return g_strdup(text);
    }

    if (max_width <= 3) {
        return truncate_utf8_for_display(text, max_width);
    }

    const gchar *ext = strrchr(text, '.');
    gint ext_width = 0;
    if (ext && ext != text && *(ext + 1) != '\0') {
        ext_width = utf8_display_width(ext);
    }

    gint suffix_width = max_width / 3;
    if (suffix_width < ext_width) {
        suffix_width = ext_width;
    }
    gint max_suffix = max_width - 4;
    if (max_suffix < 1) {
        max_suffix = 1;
    }
    if (suffix_width > max_suffix) {
        suffix_width = max_suffix;
    }
    gint prefix_width = max_width - 3 - suffix_width;
    if (prefix_width < 1) {
        prefix_width = 1;
        suffix_width = max_width - 4;
        if (suffix_width < ext_width && ext_width <= max_width - 3) {
            prefix_width = max_width - 3 - ext_width;
            if (prefix_width < 0) {
                prefix_width = 0;
            }
            suffix_width = max_width - 3 - prefix_width;
        }
    }

    if (prefix_width <= 0) {
        return truncate_utf8_for_display(text, max_width);
    }

    gchar *prefix = utf8_prefix_by_width(text, prefix_width);
    gchar *suffix = utf8_suffix_by_width(text, suffix_width);
    gchar *result = g_strdup_printf("%s...%s", prefix, suffix);
    g_free(prefix);
    g_free(suffix);
    return result;
}

static gint app_filename_max_width(const PixelTermApp *app) {
    if (!app || app->term_width <= 0) {
        return 0;
    }
    gint limit = (app->term_width * 4) / 5;
    if (limit < 1) {
        limit = app->term_width;
    }
    if (limit < 1) {
        limit = 1;
    }
    return limit;
}

typedef struct {
    const char *key;
    const char *label;
} HelpSegment;

static gint help_segments_visible_width(const HelpSegment *segments, gsize n) {
    if (!segments || n == 0) {
        return 0;
    }
    gint width = 0;
    for (gsize i = 0; i < n; i++) {
        width += utf8_display_width(segments[i].key);
        width += 1;
        width += utf8_display_width(segments[i].label);
        if (i + 1 < n) {
            width += 2;
        }
    }
    return width;
}

static void print_centered_help_line(gint row, gint term_width, const HelpSegment *segments, gsize n) {
    if (term_width <= 0 || row <= 0) {
        return;
    }
    printf("\033[%d;1H\033[2K", row);

    gint help_w = help_segments_visible_width(segments, n);
    gint pad = (help_w > 0 && term_width > help_w) ? (term_width - help_w) / 2 : 0;
    for (gint i = 0; i < pad; i++) putchar(' ');

    gint col = 1 + pad;
    for (gsize i = 0; i < n; i++) {
        gint seg_w = utf8_display_width(segments[i].key) + 1 + utf8_display_width(segments[i].label);
        gint trailing = (i + 1 < n) ? 2 : 0;
        if (col + seg_w + trailing - 1 > term_width) {
            break;
        }
        printf("\033[36m%s\033[0m %s", segments[i].key, segments[i].label);
        col += seg_w;
        if (i + 1 < n) {
            printf("  ");
            col += 2;
        }
    }
}

static void app_begin_sync_update(void) {
    // Use terminal synchronized output to reduce flicker during full-frame draws.
    printf("\033[?2026h");
}

static void app_end_sync_update(void) {
    printf("\033[?2026l");
}

static void app_clear_kitty_images(const PixelTermApp *app) {
    if (!app || !app->force_kitty) {
        return;
    }
    // Delete all kitty image placements (quiet) so old images don't linger.
    printf("\033_Ga=d,q=2\033\\");
}

static void app_clear_async_render_state(PixelTermApp *app) {
    if (!app) {
        return;
    }
    app->async_image_pending = FALSE;
    app->async_image_index = -1;
    g_clear_pointer(&app->async_image_path, g_free);
}

static void app_queue_async_render(PixelTermApp *app, const gchar *filepath,
                                   gint target_width, gint target_height) {
    if (!app || !filepath) {
        return;
    }
    app->async_image_pending = TRUE;
    app->async_image_index = app->current_index;
    g_free(app->async_image_path);
    app->async_image_path = g_strdup(filepath);
    if (app->preloader && app->preload_enabled) {
        preloader_add_task(app->preloader, filepath, 0, target_width, target_height);
    }
}

static void app_render_single_placeholder(PixelTermApp *app, const gchar *filepath) {
    if (!app || !filepath || app->ui_text_hidden) {
        return;
    }
    get_terminal_size(&app->term_width, &app->term_height);

    app_begin_sync_update();
    printf("\033[H\033[0m");

    const char *title = "Image View";
    gint title_len = (gint)strlen(title);
    gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
    printf("\033[1;1H\033[2K");
    for (gint i = 0; i < title_pad; i++) putchar(' ');
    printf("%s", title);

    printf("\033[2;1H\033[2K");

    gint current = app_get_current_index(app) + 1;
    gint total = app_get_total_images(app);
    if (current < 1) current = 1;
    if (total < 1) total = 1;
    char idx_text[32];
    g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
    gint idx_len = (gint)strlen(idx_text);
    gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
    printf("\033[3;1H\033[2K");
    for (gint i = 0; i < idx_pad; i++) putchar(' ');
    printf("%s", idx_text);

    gchar *basename = g_path_get_basename(filepath);
    gchar *safe_basename = sanitize_for_terminal(basename);
    gint max_width = app_filename_max_width(app);
    if (max_width <= 0) {
        max_width = app->term_width;
    }
    gchar *display_name = truncate_utf8_middle_keep_suffix(safe_basename, max_width);
    gint filename_len = utf8_display_width(display_name);
    gint filename_start_col = (app->term_width - filename_len) / 2;
    if (filename_start_col < 0) filename_start_col = 0;
    gint filename_row = (app->term_height >= 3) ? (app->term_height - 2) : 1;
    printf("\033[%d;1H\033[2K", filename_row);
    for (gint i = 0; i < filename_start_col; i++) putchar(' ');
    printf("\033[34m%s\033[0m", display_name);
    g_free(display_name);
    g_free(safe_basename);
    g_free(basename);

    if (app->term_height > 0) {
        const HelpSegment segments[] = {
            {"←/→", "Prev/Next"},
            {"Enter", "Preview"},
            {"TAB", "Toggle"},
            {"i", "Info"},
            {"r", "Delete"},
            {"~", "Zen"},
            {"ESC", "Exit"}
        };
        print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
    }

    app_end_sync_update();
    fflush(stdout);
}

static void app_clear_screen_for_refresh(const PixelTermApp *app) {
    if (!app) {
        printf("\033[2J\033[H\033[0m");
        return;
    }

    if (!app->clear_workaround_enabled || app->term_height <= 0) {
        printf("\033[2J\033[H\033[0m");
        return;
    }

    // Some terminals can leave stale content artifacts after clears/mode switches.
    // Optional workaround: clear -> print blank lines at bottom -> clear again.
    printf("\033[2J\033[H\033[0m");
    const gint blank_lines = 10;
    printf("\033[%d;1H", app->term_height);
    for (gint i = 0; i < blank_lines; i++) {
        printf("\033[2K\n");
    }
    printf("\033[2J\033[H\033[0m");
}

static void app_clear_single_view_ui_lines(const PixelTermApp *app) {
    if (!app || app->term_height <= 0) {
        return;
    }

    const gint top_rows[] = {1, 2, 3};
    for (gsize i = 0; i < G_N_ELEMENTS(top_rows); i++) {
        gint row = top_rows[i];
        if (row > app->term_height) {
            continue;
        }
        printf("\033[%d;1H\033[2K", row);
    }

    for (gint row = app->term_height - 2; row <= app->term_height; row++) {
        if (row < 1) {
            continue;
        }
        printf("\033[%d;1H\033[2K", row);
    }
}

static void app_clear_image_area(const PixelTermApp *app, gint top_row, gint height) {
    if (!app || app->term_height <= 0 || height <= 0) {
        return;
    }

    gint start_row = MAX(1, top_row);
    gint end_row = MIN(app->term_height, top_row + height - 1);
    for (gint row = start_row; row <= end_row; row++) {
        printf("\033[%d;1H\033[2K", row);
    }
}

static gint app_count_rendered_lines(const GString *rendered) {
    if (!rendered || rendered->len == 0) {
        return 0;
    }
    gint lines = 1;
    for (gsize i = 0; i < rendered->len; i++) {
        if (rendered->str[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

static void app_print_rendered_at(const GString *rendered, gint top_row, gint left_col) {
    if (!rendered || top_row < 1 || left_col < 1) {
        return;
    }
    const gchar *line_ptr = rendered->str;
    gint row = top_row;
    while (line_ptr && *line_ptr) {
        const gchar *newline = strchr(line_ptr, '\n');
        gint line_len = newline ? (gint)(newline - line_ptr) : (gint)strlen(line_ptr);
        printf("\033[%d;%dH", row, left_col);
        if (line_len > 0) {
            fwrite(line_ptr, 1, line_len, stdout);
        }
        if (!newline) {
            break;
        }
        line_ptr = newline + 1;
        row++;
    }
}

// Check if a directory contains media files
static gboolean directory_contains_media(const gchar *dir_path) {
    if (!dir_path || !g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
        return FALSE;
    }

    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) {
        return FALSE;
    }

    const gchar *filename;
    while ((filename = g_dir_read_name(dir))) {
        gchar *full_path = g_build_filename(dir_path, filename, NULL);
        if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR) && is_media_file(full_path)) {
            g_free(full_path);
            g_dir_close(dir);
            return TRUE;
        }
        g_free(full_path);
    }

    g_dir_close(dir);
    return FALSE;
}

static gboolean directory_contains_images(const gchar *dir_path) {
    if (!dir_path || !g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
        return FALSE;
    }

    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) {
        return FALSE;
    }

    const gchar *filename;
    while ((filename = g_dir_read_name(dir))) {
        gchar *full_path = g_build_filename(dir_path, filename, NULL);
        if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR) && is_image_file(full_path)) {
            g_free(full_path);
            g_dir_close(dir);
            return TRUE;
        }
        g_free(full_path);
    }

    g_dir_close(dir);
    return FALSE;
}

static gboolean directory_contains_books(const gchar *dir_path) {
    if (!dir_path || !g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
        return FALSE;
    }

    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) {
        return FALSE;
    }

    const gchar *filename;
    while ((filename = g_dir_read_name(dir))) {
        gchar *full_path = g_build_filename(dir_path, filename, NULL);
        if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR) && is_book_file(full_path)) {
            g_free(full_path);
            g_dir_close(dir);
            return TRUE;
        }
        g_free(full_path);
    }

    g_dir_close(dir);
    return FALSE;
}

static void app_get_image_target_dimensions(const PixelTermApp *app, gint *max_width, gint *max_height) {
    if (!max_width || !max_height) {
        return;
    }
    gint width = (app && app->term_width > 0) ? app->term_width : 80;
    gint height = (app && app->term_height > 0) ? app->term_height : 24;
    if (app && app->info_visible) {
        height -= 10;
    } else {
        // Single view reserves: title (row 1), spacer (row 2), index (row 3),
        // filename (row -2), spacer (row -1), footer (row -0).
        // Keep image position/size stable even when Zen hides UI text.
        height -= 6;
    }
    if (height < 1) {
        height = 1;
    }
    if (width < 1) {
        width = 1;
    }
    *max_width = width;
    *max_height = height;
}

// Helpers for file manager layout
static gchar* app_file_manager_display_name(const PixelTermApp *app, const gchar *entry, gboolean *is_directory) {
    (void)app; // currently unused but kept for potential future context
    if (is_directory) *is_directory = FALSE;

    gboolean is_dir = g_file_test(entry, G_FILE_TEST_IS_DIR);
    if (is_directory) *is_directory = is_dir;

    gchar *display_name = NULL;
    if (is_dir) {
        gchar *basename = g_path_get_basename(entry);
        display_name = g_strdup_printf("%s/", basename);
        g_free(basename);
    } else {
        display_name = g_path_get_basename(entry);
    }

    return display_name;
}

// Sort file manager entries in AaBb order (case-insensitive with uppercase first on ties)
static gint app_file_manager_compare_names(gconstpointer a, gconstpointer b) {
    const gchar *path_a = (const gchar*)a;
    const gchar *path_b = (const gchar*)b;
    if (!path_a || !path_b) {
        return path_a ? 1 : (path_b ? -1 : 0);
    }

    // Compare basenames using an AaBb… ordering: uppercase letters first,
    // then lowercase letters of the same alphabet, while still sorting by
    // alphabetical order within each case group.
    gchar *base_a = g_path_get_basename(path_a);
    gchar *base_b = g_path_get_basename(path_b);

    const gchar *pa = base_a;
    const gchar *pb = base_b;
    while (*pa && *pb) {
        gboolean a_is_alpha = g_ascii_isalpha(*pa);
        gboolean b_is_alpha = g_ascii_isalpha(*pb);

        gint weight_a;
        gint weight_b;

        if (a_is_alpha) {
            weight_a = (g_ascii_tolower(*pa) - 'a') * 2 + (g_ascii_isupper(*pa) ? 0 : 1);
        } else {
            weight_a = 1000 + (guchar)(*pa); // Non-letters sort after letters
        }

        if (b_is_alpha) {
            weight_b = (g_ascii_tolower(*pb) - 'a') * 2 + (g_ascii_isupper(*pb) ? 0 : 1);
        } else {
            weight_b = 1000 + (guchar)(*pb);
        }

        if (weight_a != weight_b) {
            gint diff = weight_a - weight_b;
            g_free(base_a);
            g_free(base_b);
            return diff;
        }

        pa++;
        pb++;
    }

    // All compared characters matched; shorter string wins
    gint result = (gint)(*pa) - (gint)(*pb);

    g_free(base_a);
    g_free(base_b);
    return result;
}

// Hide system-like entries that start with special prefixes (e.g., $RECYCLE.BIN)
static gboolean app_file_manager_is_special_entry(const gchar *name) {
    if (!name || !name[0]) {
        return FALSE;
    }
    return name[0] == '$';
}

static gint app_file_manager_max_display_len(const PixelTermApp *app) {
    gint max_len = 0;
    for (GList *cur = app->directory_entries; cur; cur = cur->next) {
        gchar *entry = (gchar*)cur->data;
        gboolean is_dir = FALSE;
        gchar *name = app_file_manager_display_name(app, entry, &is_dir);
        if (name) {
            gint len = strlen(name);
            if (len > max_len) {
                max_len = len;
            }
            g_free(name);
        }
    }
    return max_len > 0 ? max_len : 12;
}

static void app_file_manager_layout(const PixelTermApp *app, gint *col_width, gint *cols, gint *visible_rows, gint *total_rows) {
    gint max_len = app_file_manager_max_display_len(app);
    gint width = MAX(app->term_width, max_len + 2);

    gint column_count = 1;

    // Layout:
    //   Row 1: title
    //   Row 2: blank
    //   Row 3: current directory
    //   Row 4: blank
    //   Rows 5 .. (term_height - 4): file list (centered)
    //   Last 4 rows: footer area (only last row used for help)
    gint header_lines = 4;
    gint help_lines = 4;
    gint vis_rows = app->term_height - header_lines - help_lines;
    if (vis_rows < 1) {
        vis_rows = 1;
    }

    gint total_entries = g_list_length(app->directory_entries);
    gint rows = (total_entries + column_count - 1) / column_count;
    if (rows < 1) rows = 1;

    if (col_width) *col_width = width;
    if (cols) *cols = column_count;
    if (visible_rows) *visible_rows = vis_rows;
    if (total_rows) *total_rows = rows;
}

typedef struct {
    gint col_width;
    gint cols;
    gint visible_rows;
    gint total_rows;
    gint total_entries;
    gint start_row;
    gint end_row;
    gint rows_to_render;
    gint top_padding;
    gint bottom_padding;
} FileManagerViewport;

static FileManagerViewport app_file_manager_compute_viewport(PixelTermApp *app) {
    FileManagerViewport viewport = {0};
    if (!app) {
        return viewport;
    }

    app_file_manager_layout(app, &viewport.col_width, &viewport.cols,
                            &viewport.visible_rows, &viewport.total_rows);
    viewport.total_entries = g_list_length(app->directory_entries);

    gint available_rows = viewport.visible_rows;
    if (available_rows < 0) {
        available_rows = 0;
    }

    gint max_offset = MAX(0, viewport.total_rows - 1);
    gint scroll_offset = app->scroll_offset;
    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }

    gint start_row = scroll_offset;
    if (viewport.total_rows <= 0) {
        start_row = 0;
    } else if (start_row >= viewport.total_rows) {
        start_row = viewport.total_rows - 1;
    }

    gint end_row = MIN(start_row + available_rows, viewport.total_rows);
    gint rows_to_render = end_row - start_row;
    if (rows_to_render < 0) {
        rows_to_render = 0;
    }

    gint selected_row = app->selected_entry;
    if (selected_row < 0) {
        selected_row = 0;
    } else if (viewport.total_rows > 0 && selected_row >= viewport.total_rows) {
        selected_row = viewport.total_rows - 1;
    } else if (viewport.total_rows == 0) {
        selected_row = 0;
    }

    gint selected_pos = selected_row - start_row;
    if (selected_pos < 0) {
        selected_pos = 0;
    }
    if (rows_to_render > 0 && selected_pos >= rows_to_render) {
        selected_pos = rows_to_render - 1;
    }

    gint target_row = available_rows / 2;
    gint top_padding = target_row - selected_pos;
    if (top_padding < 0) {
        gint more_rows_below = MAX(0, viewport.total_rows - end_row);
        gint scroll_shift = MIN(-top_padding, more_rows_below);
        if (scroll_shift > 0) {
            start_row += scroll_shift;
            end_row = MIN(start_row + available_rows, viewport.total_rows);
            rows_to_render = end_row - start_row;
            if (rows_to_render < 0) {
                rows_to_render = 0;
            }
            selected_pos = selected_row - start_row;
            if (selected_pos < 0) {
                selected_pos = 0;
            }
            if (rows_to_render > 0 && selected_pos >= rows_to_render) {
                selected_pos = rows_to_render - 1;
            }
            top_padding = target_row - selected_pos;
        }
        if (top_padding < 0) {
            top_padding = 0;
        }
    }

    gint visible_space = MAX(0, available_rows - top_padding);
    if (rows_to_render > visible_space) {
        end_row = MIN(viewport.total_rows, start_row + visible_space);
        rows_to_render = end_row - start_row;
        if (rows_to_render < 0) {
            rows_to_render = 0;
        }
    }

    gint bottom_padding = available_rows - rows_to_render - top_padding;
    if (bottom_padding < 0) {
        bottom_padding = 0;
    }

    viewport.start_row = start_row;
    viewport.end_row = end_row;
    viewport.rows_to_render = rows_to_render;
    viewport.top_padding = top_padding;
    viewport.bottom_padding = bottom_padding;
    return viewport;
}

static gboolean app_file_manager_hit_test(PixelTermApp *app, gint mouse_x, gint mouse_y, gint *out_index) {
    (void)mouse_x; // Currently unused because layout is single column
    if (!app || !app->file_manager_mode) {
        return FALSE;
    }

    get_terminal_size(&app->term_width, &app->term_height);
    FileManagerViewport viewport = app_file_manager_compute_viewport(app);

    // Header occupies four rows; list starts at row 5 and ends at row (term_height - 4)
    const gint header_lines = 4;
    const gint list_top_row = header_lines + 1;
    gint list_bottom_row = app->term_height - 4;
    if (list_bottom_row < list_top_row) {
        list_bottom_row = list_top_row;
    }
    if (mouse_y < list_top_row || mouse_y > list_bottom_row) {
        return FALSE;
    }

    gint row_idx = mouse_y - list_top_row;
    if (row_idx < 0 || row_idx >= viewport.visible_rows) {
        return FALSE;
    }

    if (row_idx < viewport.top_padding) {
        return FALSE;
    }

    gint relative_row = row_idx - viewport.top_padding;
    if (relative_row >= viewport.rows_to_render) {
        return FALSE;
    }

    gint absolute_row = viewport.start_row + relative_row;
    gint selected_idx = absolute_row * viewport.cols;
    if (selected_idx < 0 || selected_idx >= viewport.total_entries) {
        return FALSE;
    }

    if (out_index) {
        *out_index = selected_idx;
    }
    return TRUE;
}

static void app_file_manager_adjust_scroll(PixelTermApp *app, gint cols, gint visible_rows) {
    gint total_entries = g_list_length(app->directory_entries);
    gint total_rows = (total_entries + cols - 1) / cols;
    if (total_rows < 1) total_rows = 1;

    gint row = app->selected_entry / cols;
    
    // Always aim to center the selection
    gint target_row = visible_rows / 2;
    gint desired_offset = row - target_row;

    // Limit the range of scroll offset
    // Allow scrolling past the bottom to keep the last item centered
    gint max_offset = MAX(0, total_rows - 1 - (visible_rows / 2));
    
    if (desired_offset < 0) desired_offset = 0;
    if (desired_offset > max_offset) desired_offset = max_offset;

    // Only update scroll offset when necessary to avoid frequent changes
    if (app->scroll_offset != desired_offset) {
        app->scroll_offset = desired_offset;
    }
}

// Select the current image in file manager (only when appropriate)
static void app_file_manager_select_current_image(PixelTermApp *app) {
    // Only select current image if we're returning from image view.
    // For initial file manager entry (RETURN_MODE_NONE), keep selected_entry = 0.
    if (app->return_to_mode == RETURN_MODE_NONE) {
        return;
    }

    if (!app || !app->current_directory || !app->file_manager_directory) {
        return;
    }

    if (g_strcmp0(app->current_directory, app->file_manager_directory) != 0) {
        return;
    }

    const gchar *current_file = app_get_current_filepath(app);
    if (!current_file) {
        return;
    }

    gchar *current_norm = g_canonicalize_filename(current_file, NULL);
    if (!current_norm) {
        return;
    }

    gint idx = 0;
    for (GList *cur = app->directory_entries; cur; cur = cur->next, idx++) {
        gchar *path = (gchar*)cur->data;
        if (g_strcmp0(path, current_norm) == 0) {
            app->selected_entry = idx;
            break;
        }
    }

    g_free(current_norm);

    // Adjust scroll so selection is visible
    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, cols, visible_rows);
}

// Jump to next entry starting with a letter
ErrorCode app_file_manager_jump_to_letter(PixelTermApp *app, char letter) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint total_entries = g_list_length(app->directory_entries);
    if (total_entries == 0) {
        return ERROR_NONE;
    }

    gchar target = g_ascii_tolower(letter);
    gint start = (app->selected_entry + 1) % total_entries;
    gint idx = start;

    do {
        gchar *entry = (gchar*)g_list_nth_data(app->directory_entries, idx);
        if (entry) {
            gchar *base = g_path_get_basename(entry);
            if (base && base[0]) {
                gchar first = g_ascii_tolower(base[0]);
                if (first == target) {
                    app->selected_entry = idx;
                    g_free(base);
                    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
                    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);

                    // Center selection vertically when possible
                    gint row = app->selected_entry / cols;
                    gint offset = row - visible_rows / 2;
                    if (offset < 0) offset = 0;
                    gint max_offset = MAX(0, total_rows - visible_rows);
                    if (offset > max_offset) offset = max_offset;
                    app->scroll_offset = offset;

                    app_file_manager_adjust_scroll(app, cols, visible_rows);
                    return ERROR_NONE;
                }
            }
            g_free(base);
        }
        idx = (idx + 1) % total_entries;
    } while (idx != start);

    return ERROR_NONE;
}

// Destroy application and free resources
void app_destroy(PixelTermApp *app) {
    if (!app) {
        return;
    }

    // Stop any running threads
    app->running = FALSE;
    
    // Stop and destroy preloader
    if (app->preloader) {
        preloader_stop(app->preloader);
        preloader_destroy(app->preloader);
        app->preloader = NULL;
    }
    
    // Stop and destroy GIF player
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
        gif_player_destroy(app->gif_player);
        app->gif_player = NULL;
    }

    // Stop and destroy video player
    if (app->video_player) {
        video_player_stop(app->video_player);
        video_player_destroy(app->video_player);
        app->video_player = NULL;
    }

    app_close_book(app);


    // Cleanup Chafa resources
    if (app->canvas) {
        chafa_canvas_unref(app->canvas);
    }
    
    if (app->canvas_config) {
        chafa_canvas_config_unref(app->canvas_config);
    }
    
    if (app->term_info) {
        chafa_term_info_unref(app->term_info);
    }

    // Cleanup file list
    if (app->image_files) {
        g_list_free_full(app->image_files, (GDestroyNotify)g_free);
    }

    // Cleanup directory path
    g_free(app->current_directory);

    // Cleanup file manager entries
    if (app->directory_entries) {
        g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
    }
    g_free(app->file_manager_directory);
    g_free(app->async_image_path);

    // Cleanup error
    if (app->gerror) {
        g_error_free(app->gerror);
    }

    g_free(app);
}

// Initialize application
ErrorCode app_initialize(PixelTermApp *app, gboolean dither_enabled) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    app->dither_enabled = dither_enabled;

    // Detect terminal capabilities
    ChafaTermDb *term_db = chafa_term_db_get_default();
    if (!term_db) {
        return ERROR_CHAFA_INIT;
    }

    app->term_info = chafa_term_db_detect(term_db, NULL);
    if (!app->term_info) {
        return ERROR_CHAFA_INIT;
    }

    // Get terminal dimensions
    get_terminal_size(&app->term_width, &app->term_height);

    // Create canvas configuration
    app->canvas_config = chafa_canvas_config_new();
    if (!app->canvas_config) {
        return ERROR_CHAFA_INIT;
    }

    // Configure canvas for optimal terminal display
    chafa_canvas_config_set_geometry(app->canvas_config, app->term_width, app->term_height - 6);
    chafa_canvas_config_set_canvas_mode(app->canvas_config, CHAFA_CANVAS_MODE_TRUECOLOR);
    chafa_canvas_config_set_color_space(app->canvas_config, CHAFA_COLOR_SPACE_RGB);
    chafa_canvas_config_set_pixel_mode(app->canvas_config, CHAFA_PIXEL_MODE_SYMBOLS);

    // Create canvas
    app->canvas = chafa_canvas_new(app->canvas_config);
    if (!app->canvas) {
        return ERROR_CHAFA_INIT;
    }

    // Initialize GIF player
    app->gif_player = gif_player_new(app->render_work_factor, app->force_text, app->force_sixel,
                                     app->force_kitty, app->force_iterm2, app->gamma);
    if (!app->gif_player) {
        return ERROR_MEMORY_ALLOC;
    }

    // Initialize video player
    app->video_player = video_player_new(app->render_work_factor, app->force_text, app->force_sixel,
                                         app->force_kitty, app->force_iterm2, app->gamma);
    if (!app->video_player) {
        gif_player_destroy(app->gif_player);
        app->gif_player = NULL;
        return ERROR_MEMORY_ALLOC;
    }

    return ERROR_NONE;
}

// Load directory with images
ErrorCode app_load_directory(PixelTermApp *app, const char *directory) {
    if (!app || !directory) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Cleanup existing file list
    if (app->image_files) {
        g_list_free_full(app->image_files, (GDestroyNotify)g_free);
        app->image_files = NULL;
    }
    // Reset preloader state to avoid leaking threads or stale cache
    if (app->preloader) {
        preloader_stop(app->preloader);
        preloader_destroy(app->preloader);
        app->preloader = NULL;
    }

    g_free(app->current_directory);
    // Normalize path to remove trailing slashes and resolve relative components
    gchar *normalized_dir = g_canonicalize_filename(directory, NULL);
    app->current_directory = normalized_dir ? normalized_dir : g_strdup(directory);

    // Scan directory for image files
    FileBrowser *browser = browser_create();
    if (!browser) {
        return ERROR_MEMORY_ALLOC;
    }

    ErrorCode error = browser_scan_directory(browser, directory);
    if (error != ERROR_NONE) {
        browser_destroy(browser);
        return error;
    }

    GList *file_list_from_browser = browser_get_all_files(browser);

    // Clear existing app->image_files and prepare for new entries
    if (app->image_files) {
        g_list_free_full(app->image_files, (GDestroyNotify)g_free);
        app->image_files = NULL;
    }
    app->total_images = 0; // Reset count

    // Copy and duplicate file paths from browser's list to app's list
    for (GList *current_node = file_list_from_browser; current_node; current_node = g_list_next(current_node)) {
        gchar *filepath = (gchar*)current_node->data;
        app->image_files = g_list_append(app->image_files, g_strdup(filepath));
    }
    
    // Now sort app->image_files using the custom comparison function
    app->image_files = g_list_sort(app->image_files, (GCompareFunc)app_file_manager_compare_names);
    app->total_images = g_list_length(app->image_files);
    app->current_index = 0;

    browser_destroy(browser);

    // Note: We return ERROR_NONE even if no images are found
    // The caller should check app_has_images() to handle this case

    // Start preloading if enabled
    if (app->preload_enabled) {
        app->preloader = preloader_create();
        if (app->preloader) {
            preloader_initialize(app->preloader, app->dither_enabled, app->render_work_factor,
                                 app->force_text, app->force_sixel, app->force_kitty, app->force_iterm2,
                                 app->gamma);
            
            // Set terminal dimensions for preloader
            preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
            gint target_width = 0, target_height = 0;
            app_get_image_target_dimensions(app, &target_width, &target_height);
            
            ErrorCode preload_err = preloader_start(app->preloader);
            if (preload_err == ERROR_NONE) {
                // Add initial preload tasks for current directory
                preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
            } else {
                preloader_destroy(app->preloader);
                app->preloader = NULL;
                app->preload_enabled = FALSE;
            }
        }
    }

    return ERROR_NONE;
}

// Load single image file
ErrorCode app_load_single_file(PixelTermApp *app, const char *filepath) {
    if (!app || !filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    if (!file_exists(filepath)) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if the file is a valid image/video file
    if (!is_valid_media_file(filepath)) {
        return ERROR_INVALID_IMAGE;
    }

    // Get directory from file path
    gchar *directory = g_path_get_dirname(filepath);
    if (!directory) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Load the directory
    ErrorCode error = app_load_directory(app, directory);
    g_free(directory); // Free early to avoid memory leak
    
    if (error != ERROR_NONE) {
        return error;
    }

    // Find the specific file in the list (single pass)
    gchar *target_basename = g_path_get_basename(filepath);
    gboolean found = FALSE;
    gint idx = 0;
    for (GList *cur = app->image_files; cur; cur = cur->next, idx++) {
        gchar *current_basename = g_path_get_basename((gchar*)cur->data);
        if (g_strcmp0(current_basename, target_basename) == 0) {
            app->current_index = idx;
            found = TRUE;
            g_free(current_basename);
            break;
        }
        g_free(current_basename);
    }
    g_free(target_basename);
    
    // If file was found and loaded successfully, mark for redraw
    if (found) {
        app->needs_redraw = TRUE;
        app->info_visible = FALSE;
        app->return_to_mode = RETURN_MODE_SINGLE;
        app->image_zoom = 1.0;
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;
    }
    
    return found ? ERROR_NONE : ERROR_FILE_NOT_FOUND;
}

ErrorCode app_open_book(PixelTermApp *app, const char *filepath) {
    if (!app || !filepath) {
        return ERROR_FILE_NOT_FOUND;
    }
    if (!is_valid_book_file(filepath)) {
        return ERROR_INVALID_IMAGE;
    }

    app_close_book(app);

    ErrorCode open_err = ERROR_NONE;
    BookDocument *doc = book_open(filepath, &open_err);
    if (!doc) {
        return open_err != ERROR_NONE ? open_err : ERROR_INVALID_IMAGE;
    }

    app->book_doc = doc;
    app->book_path = g_strdup(filepath);
    app->book_page_count = book_get_page_count(doc);
    app->book_page = 0;
    app->book_preview_selected = 0;
    app->book_preview_scroll = 0;
    app->book_preview_zoom = 0;
    app->book_toc = book_load_toc(doc);
    app->book_toc_selected = 0;
    app->book_toc_scroll = 0;
    app->book_toc_visible = FALSE;

    g_free(app->current_directory);
    gchar *directory = g_path_get_dirname(filepath);
    app->current_directory = directory ? directory : g_strdup(filepath);

    return ERROR_NONE;
}

void app_close_book(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (app->book_doc) {
        book_close(app->book_doc);
        app->book_doc = NULL;
    }
    g_clear_pointer(&app->book_path, g_free);
    app->book_page = 0;
    app->book_page_count = 0;
    app->book_preview_selected = 0;
    app->book_preview_scroll = 0;
    app->book_preview_zoom = 0;
    app->book_jump_active = FALSE;
    app->book_jump_dirty = FALSE;
    app->book_jump_len = 0;
    app->book_jump_buf[0] = '\0';
    if (app->book_toc) {
        book_toc_free(app->book_toc);
        app->book_toc = NULL;
    }
    app->book_toc_selected = 0;
    app->book_toc_scroll = 0;
    app->book_toc_visible = FALSE;
    app->book_mode = FALSE;
    app->book_preview_mode = FALSE;
}

// Navigate to next image
ErrorCode app_next_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Store the old index to check if it actually changes
    gint old_index = app->current_index;

    if (app->current_index < app->total_images - 1) {
        app->current_index++;
    } else {
        // Wrap around to first image
        app->current_index = 0;
    }
    
    // Only redraw if the index actually changed
    if (old_index != app->current_index) {
        app->needs_redraw = TRUE;
        app->info_visible = FALSE;  // Reset info visibility when switching images
        app->image_zoom = 1.0;
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;

        // Update preload tasks for new position
        if (app->preloader && app->preload_enabled) {
            gint target_width = 0, target_height = 0;
            app_get_image_target_dimensions(app, &target_width, &target_height);
            preloader_clear_queue(app->preloader);
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
        }
    }

    return ERROR_NONE;
}

// Navigate to previous image
ErrorCode app_previous_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Store the old index to check if it actually changes
    gint old_index = app->current_index;

    if (app->current_index > 0) {
        app->current_index--;
    } else {
        // Wrap around to last image
        app->current_index = app->total_images - 1;
    }
    
    // Only redraw if the index actually changed
    if (old_index != app->current_index) {
        app->needs_redraw = TRUE;
        app->info_visible = FALSE;  // Reset info visibility when switching images
        app->image_zoom = 1.0;
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;

        // Update preload tasks for new position
        if (app->preloader && app->preload_enabled) {
            gint target_width = 0, target_height = 0;
            app_get_image_target_dimensions(app, &target_width, &target_height);
            preloader_clear_queue(app->preloader);
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
        }
    }

    return ERROR_NONE;
}

// Go to specific image index
ErrorCode app_goto_image(PixelTermApp *app, gint index) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    if (index >= 0 && index < app->total_images) {
        // Only redraw if the index actually changes
        if (app->current_index != index) {
            app->current_index = index;
            app->needs_redraw = TRUE;
            app->info_visible = FALSE;  // Reset info visibility when switching images
            app->image_zoom = 1.0;
            app->image_pan_x = 0.0;
            app->image_pan_y = 0.0;
            
            // Update preload tasks for new position
            if (app->preloader && app->preload_enabled) {
                gint target_width = 0, target_height = 0;
                app_get_image_target_dimensions(app, &target_width, &target_height);
                preloader_clear_queue(app->preloader);
                preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
            }
        }
        
        return ERROR_NONE;
    }

    return ERROR_INVALID_IMAGE;
}

// Render current image
ErrorCode app_render_current_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Reset info visibility when rendering image
    app->info_visible = FALSE;

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if it's an animated image/video file and handle animation
    gboolean is_animated_image = is_animated_image_candidate(filepath);
    gboolean is_video = is_video_file(filepath);
    gboolean gif_is_animated = FALSE;
    if (!is_video && !is_animated_image && !is_image_file(filepath)) {
        is_video = is_valid_video_file(filepath);
    }

    if (is_animated_image && is_video) {
        is_video = FALSE;
    }

    if (is_video && app->video_player) {
        if (!app->video_player->filepath || g_strcmp0(app->video_player->filepath, filepath) != 0) {
            ErrorCode load_result = video_player_load(app->video_player, filepath);
            if (load_result != ERROR_NONE) {
                is_video = FALSE;
            }
        }
    }

    if (is_animated_image && app->gif_player && !is_video) {
        // First, check if we need to load the animated image
        if (!app->gif_player->filepath || g_strcmp0(app->gif_player->filepath, filepath) != 0) {
            ErrorCode load_result = gif_player_load(app->gif_player, filepath);
            if (load_result != ERROR_NONE) {
                // If animation loading fails, just treat it as a regular image
                is_animated_image = FALSE;
            }
        }
    }
    if (is_animated_image && app->gif_player && !is_video) {
        gif_is_animated = gif_player_is_animated(app->gif_player);
    }

    gint target_width = 0, target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);
    gint image_area_height = target_height;
    if (is_video) {
        gdouble scale = app->video_scale;
        if (scale < 0.3) {
            scale = 0.3;
        } else if (scale > 1.5) {
            scale = 1.5;
        }
        target_width = (gint)(target_width * scale + 0.5);
        target_height = (gint)(target_height * scale + 0.5);
        if (target_width < 1) {
            target_width = 1;
        }
        if (target_height < 1) {
            target_height = 1;
        }
    }

    gint cell_w = 0, cell_h = 0;
    get_terminal_cell_geometry(&cell_w, &cell_h);
    if (cell_w <= 0) cell_w = 10;
    if (cell_h <= 0) cell_h = 20;
    app->image_viewport_px_w = MAX(1, target_width * cell_w);
    app->image_viewport_px_h = MAX(1, target_height * cell_h);

    gdouble image_zoom = app->image_zoom;
    if (image_zoom < 1.0) {
        image_zoom = 1.0;
    }
    app->image_zoom = image_zoom;
    if (image_zoom <= 1.0) {
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;
    }

    gboolean async_request = app->async_render_request;
    app->async_render_request = FALSE;
    gboolean use_zoom = (!is_video && !gif_is_animated && image_zoom > 1.0 + 0.001);
    if (!app->async_render_force_sync &&
        async_request &&
        app->preloader &&
        app->preload_enabled &&
        !is_video &&
        !gif_is_animated &&
        !use_zoom) {
        gint cached_width = 0, cached_height = 0;
        if (!preloader_get_cached_image_dimensions(app->preloader, filepath, target_width, target_height,
                                                   &cached_width, &cached_height)) {
            app_queue_async_render(app, filepath, target_width, target_height);
            app_render_single_placeholder(app, filepath);
            return ERROR_NONE;
        }
    }
    if (app->async_render_force_sync) {
        app->async_render_force_sync = FALSE;
    }
    if (app->async_image_pending &&
        app->async_image_index == app->current_index &&
        g_strcmp0(app->async_image_path, filepath) == 0) {
        app_clear_async_render_state(app);
    }

    // Title + index area (single image view)
    gint image_area_top_row = 4; // Keep layout stable even in Zen (UI hidden)
    gint image_render_top_row = image_area_top_row;
    if (is_video && target_height > 0 && image_area_height > target_height) {
        gint vpad = (image_area_height - target_height) / 2;
        if (vpad > 0) {
            image_render_top_row += vpad;
        }
    }

    app_begin_sync_update();
    app_clear_kitty_images(app);
    // Clear screen and reset terminal state
    if (app && app->suppress_full_clear) {
        app->suppress_full_clear = FALSE;
        printf("\033[H\033[0m");
        app_clear_single_view_ui_lines(app);
        app_clear_image_area(app, image_area_top_row, image_area_height);
    } else {
        app_clear_screen_for_refresh(app);
    }
    if (app->gif_player) {
        gif_player_set_render_area(app->gif_player,
                                   app->term_width,
                                   app->term_height,
                                   image_area_top_row,
                                   target_height,
                                   target_width,
                                   target_height);
    }
    if (app->video_player) {
        video_player_set_render_area(app->video_player,
                                     app->term_width,
                                     app->term_height,
                                     image_render_top_row,
                                     target_height,
                                     target_width,
                                     target_height);
        app->video_player->show_stats = app->show_fps && !app->ui_text_hidden;
    }
    if (!app->ui_text_hidden && app->term_height > 0) {
        const char *title = is_video ? "Video View" : "Image View";
        gint title_len = strlen(title);
        gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
        printf("\033[1;1H\033[2K");
        for (gint i = 0; i < title_pad; i++) putchar(' ');
        printf("%s", title);

        // Row 2: spacer
        printf("\033[2;1H\033[2K");

        // Row 3: Index indicator centered (numbers only)
        gint current = app_get_current_index(app) + 1;
        gint total = app_get_total_images(app);
        if (current < 1) current = 1;
        if (total < 1) total = 1;
        char idx_text[32];
        g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
        gint idx_len = (gint)strlen(idx_text);
        gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
        printf("\033[3;1H\033[2K");
        for (gint i = 0; i < idx_pad; i++) putchar(' ');
        printf("%s", idx_text);
    }

    if (is_video) {
        gint effective_width = target_width > 0 ? target_width : app->term_width;
        if (effective_width > app->term_width) {
            effective_width = app->term_width;
        }
        if (effective_width < 0) {
            effective_width = 0;
        }
        gint left_pad = (app->term_width > effective_width) ? (app->term_width - effective_width) / 2 : 0;
        if (left_pad < 0) left_pad = 0;

        if (filepath && !app->ui_text_hidden) {
            gchar *basename = g_path_get_basename(filepath);
            gchar *safe_basename = sanitize_for_terminal(basename);
            if (safe_basename) {
                gint max_width = app_filename_max_width(app);
                if (max_width <= 0) {
                    max_width = app->term_width;
                }
                gchar *display_name = truncate_utf8_middle_keep_suffix(safe_basename, max_width);
                gint filename_len = utf8_display_width(display_name);
                gint image_center_col = effective_width / 2;
                gint filename_start_col = left_pad + image_center_col - filename_len / 2;
                if (filename_start_col < 0) filename_start_col = 0;
                if (filename_start_col + filename_len > app->term_width) {
                    filename_start_col = app->term_width - filename_len;
                }
                if (filename_start_col < 0) filename_start_col = 0;
                gint filename_row = (app->term_height >= 3) ? (app->term_height - 2) : 1;
                printf("\033[%d;%dH", filename_row, filename_start_col + 1);
                printf("\033[34m%s\033[0m", display_name);
                g_free(display_name);
                g_free(safe_basename);
                g_free(basename);
            }
        }

        if (app->term_height > 0 && !app->ui_text_hidden) {
            const HelpSegment segments[] = {
                {"←/→", "Prev/Next"},
                {"Space", "Pause/Play"},
                {"F", "FPS"},
                {"P", "Protocol"},
                {"+/-", "Scale"},
                {"Enter", "Preview"},
                {"TAB", "Toggle"},
                {"r", "Delete"},
                {"~", "Zen"},
                {"ESC", "Exit"}
            };
            print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
        }

        if (app->gif_player) {
            gif_player_stop(app->gif_player);
        }
        if (app->video_player) {
            video_player_play(app->video_player);
            app->needs_redraw = FALSE;
        } else {
            app_end_sync_update();
            return ERROR_INVALID_IMAGE;
        }

        app_end_sync_update();
        fflush(stdout);
        return ERROR_NONE;
    }

    // Check if image is already cached
    GString *rendered = NULL;
    gint image_width = 0;
    gint image_height = 0;

    if (use_zoom) {
        GError *load_error = NULL;
        GdkPixbuf *pixbuf = app_load_pixbuf_from_stream(filepath, &load_error);
        if (!pixbuf) {
            if (load_error) {
                g_error_free(load_error);
            }
            app_end_sync_update();
            return ERROR_INVALID_IMAGE;
        }

        gint orig_w = gdk_pixbuf_get_width(pixbuf);
        gint orig_h = gdk_pixbuf_get_height(pixbuf);
        if (orig_w < 1) orig_w = 1;
        if (orig_h < 1) orig_h = 1;

        gdouble scale_w = (gdouble)app->image_viewport_px_w / (gdouble)orig_w;
        gdouble scale_h = (gdouble)app->image_viewport_px_h / (gdouble)orig_h;
        gdouble base_scale = scale_w < scale_h ? scale_w : scale_h;
        if (!isfinite(base_scale) || base_scale <= 0.0) {
            base_scale = 1.0;
        }
        gdouble desired_scale = base_scale * image_zoom;
        gdouble scaled_w = (gdouble)orig_w * desired_scale;
        gdouble scaled_h = (gdouble)orig_h * desired_scale;
        const gdouble max_dim = 4096.0;
        if (scaled_w > max_dim || scaled_h > max_dim) {
            gdouble descale = scaled_w / max_dim;
            gdouble descale_h = scaled_h / max_dim;
            if (descale_h > descale) {
                descale = descale_h;
            }
            if (descale > 1.0) {
                desired_scale /= descale;
                scaled_w = (gdouble)orig_w * desired_scale;
                scaled_h = (gdouble)orig_h * desired_scale;
            }
        }

        gint scaled_px_w = (gint)ceil(scaled_w);
        gint scaled_px_h = (gint)ceil(scaled_h);
        if (scaled_px_w < 1) scaled_px_w = 1;
        if (scaled_px_h < 1) scaled_px_h = 1;

        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, scaled_px_w, scaled_px_h, GDK_INTERP_BILINEAR);
        g_object_unref(pixbuf);
        if (!scaled) {
            app_end_sync_update();
            return ERROR_MEMORY_ALLOC;
        }

        gint crop_w = app->image_viewport_px_w;
        gint crop_h = app->image_viewport_px_h;
        if (crop_w < 1) crop_w = 1;
        if (crop_h < 1) crop_h = 1;
        if (crop_w > scaled_px_w) crop_w = scaled_px_w;
        if (crop_h > scaled_px_h) crop_h = scaled_px_h;

        gint max_pan_x = MAX(0, scaled_px_w - crop_w);
        gint max_pan_y = MAX(0, scaled_px_h - crop_h);
        if (app->image_pan_x < 0.0) app->image_pan_x = 0.0;
        if (app->image_pan_y < 0.0) app->image_pan_y = 0.0;
        if (app->image_pan_x > max_pan_x) app->image_pan_x = max_pan_x;
        if (app->image_pan_y > max_pan_y) app->image_pan_y = max_pan_y;

        gint crop_x = (gint)(app->image_pan_x + 0.5);
        gint crop_y = (gint)(app->image_pan_y + 0.5);
        if (crop_x < 0) crop_x = 0;
        if (crop_y < 0) crop_y = 0;
        if (crop_x > max_pan_x) crop_x = max_pan_x;
        if (crop_y > max_pan_y) crop_y = max_pan_y;

        GdkPixbuf *render_pixbuf = scaled;
        if (crop_w < scaled_px_w || crop_h < scaled_px_h) {
            render_pixbuf = gdk_pixbuf_new_subpixbuf(scaled, crop_x, crop_y, crop_w, crop_h);
            if (!render_pixbuf) {
                g_object_unref(scaled);
                app_end_sync_update();
                return ERROR_MEMORY_ALLOC;
            }
        } else {
            g_object_ref(render_pixbuf);
        }

        ImageRenderer *renderer = renderer_create();
        if (!renderer) {
            g_object_unref(render_pixbuf);
            g_object_unref(scaled);
            app_end_sync_update();
            return ERROR_MEMORY_ALLOC;
        }

        RendererConfig config = {
            .max_width = target_width,
            .max_height = target_height,
            .preserve_aspect_ratio = TRUE,
            .dither = app->dither_enabled,
            .color_space = CHAFA_COLOR_SPACE_RGB,
            .work_factor = app->render_work_factor,
            .force_text = app->force_text,
            .force_sixel = app->force_sixel,
            .force_kitty = app->force_kitty,
            .force_iterm2 = app->force_iterm2,
            .gamma = app->gamma,
            .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
            .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
            .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
        };

        ErrorCode error = renderer_initialize(renderer, &config);
        if (error != ERROR_NONE) {
            renderer_destroy(renderer);
            g_object_unref(render_pixbuf);
            g_object_unref(scaled);
            app_end_sync_update();
            return error;
        }

        rendered = renderer_render_image_data(renderer,
                                              gdk_pixbuf_get_pixels(render_pixbuf),
                                              gdk_pixbuf_get_width(render_pixbuf),
                                              gdk_pixbuf_get_height(render_pixbuf),
                                              gdk_pixbuf_get_rowstride(render_pixbuf),
                                              gdk_pixbuf_get_n_channels(render_pixbuf));
        renderer_get_rendered_dimensions(renderer, &image_width, &image_height);

        renderer_destroy(renderer);
        g_object_unref(render_pixbuf);
        g_object_unref(scaled);

        if (!rendered) {
            app_end_sync_update();
            return ERROR_INVALID_IMAGE;
        }
    } else {
        if (app->preloader && app->preload_enabled) {
            rendered = preloader_get_cached_image(app->preloader, filepath, target_width, target_height);
        }

        // If not in cache, render it normally
        if (!rendered) {
            // Create renderer
            ImageRenderer *renderer = renderer_create();
            if (!renderer) {
                app_end_sync_update();
                return ERROR_MEMORY_ALLOC;
            }

            // Configure renderer
            RendererConfig config = {
                .max_width = target_width,
                .max_height = target_height, // Normal: use almost full height, Info: reserve space
                .preserve_aspect_ratio = TRUE,
                .dither = app->dither_enabled,
                .color_space = CHAFA_COLOR_SPACE_RGB,
                .work_factor = app->render_work_factor,
                .force_text = app->force_text,
                .force_sixel = app->force_sixel,
                .force_kitty = app->force_kitty,
                .force_iterm2 = app->force_iterm2,
                .gamma = app->gamma,
                .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
                .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
                .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
            };

            ErrorCode error = renderer_initialize(renderer, &config);
            if (error != ERROR_NONE) {
                renderer_destroy(renderer);
                app_end_sync_update();
                return error;
            }

            // Render image
            rendered = renderer_render_image_file(renderer, filepath);
            if (!rendered) {
                renderer_destroy(renderer);
                app_end_sync_update();
                return ERROR_INVALID_IMAGE;
            }

            // Get rendered image dimensions
            renderer_get_rendered_dimensions(renderer, &image_width, &image_height);

            // Add to cache if preloader is available
            if (app->preloader && app->preload_enabled) {
                preloader_cache_add(app->preloader, filepath, rendered, image_width, image_height, target_width, target_height);
            }

            renderer_destroy(renderer);
        } else {
            // For cached images, get the actual dimensions from cache
            if (!preloader_get_cached_image_dimensions(app->preloader, filepath, target_width, target_height, &image_width, &image_height)) {
                // Fallback: count lines in the rendered output to get actual height
                image_width = app->term_width;
                image_height = 1; // Start with 1 for first line

                for (gsize i = 0; i < rendered->len; i++) {
                    if (rendered->str[i] == '\n') {
                        image_height++;
                    }
                }
            }
        }
    }

    // Determine effective width for centering
    gint effective_width = image_width > 0 ? image_width : target_width;
    if (effective_width > app->term_width) {
        effective_width = app->term_width;
    }
    if (effective_width < 0) {
        effective_width = 0;
    }
    gint left_pad = (app->term_width > effective_width) ? (app->term_width - effective_width) / 2 : 0;
    if (left_pad < 0) left_pad = 0;

    // Vertically center the rendered image inside the image area
    gint image_top_row = image_area_top_row;
    if (target_height > 0 && image_height > 0 && image_height < target_height) {
        gint vpad = (target_height - image_height) / 2;
        if (vpad < 0) vpad = 0;
        image_top_row = image_area_top_row + vpad;
    }
    if (app) {
        gint stored_height = image_height > 0 ? image_height : (target_height > 0 ? target_height : 1);
        app->last_render_top_row = image_top_row;
        app->last_render_height = stored_height;
        app->image_view_left_col = left_pad + 1;
        app->image_view_top_row = image_top_row;
        app->image_view_width = image_width > 0 ? image_width : effective_width;
        app->image_view_height = stored_height;
    }

    gchar *pad_buffer = NULL;
    if (left_pad > 0) {
        pad_buffer = g_malloc(left_pad);
        memset(pad_buffer, ' ', left_pad);
    }

    const gchar *line_ptr = rendered->str;
    gint row = image_top_row;
    while (line_ptr && *line_ptr) {
        const gchar *newline = strchr(line_ptr, '\n');
        gint line_len = newline ? (gint)(newline - line_ptr) : (gint)strlen(line_ptr);
        printf("\033[%d;1H", row);
        if (left_pad > 0) {
            fwrite(pad_buffer, 1, left_pad, stdout);
        }
        if (line_len > 0) {
            fwrite(line_ptr, 1, line_len, stdout);
        }
        if (!newline) {
            break;
        }
        line_ptr = newline + 1;
        row++;
    }
    g_free(pad_buffer);

    // Calculate filename position relative to image center
    if (filepath && !app->ui_text_hidden) {
        gchar *basename = g_path_get_basename(filepath);
        gchar *safe_basename = sanitize_for_terminal(basename);
        if (safe_basename) {
            gint max_width = app_filename_max_width(app);
            if (max_width <= 0) {
                max_width = app->term_width;
            }
            gchar *display_name = truncate_utf8_middle_keep_suffix(safe_basename, max_width);
            gint filename_len = utf8_display_width(display_name);
            // Center filename relative to image width, but ensure it stays within terminal bounds
            gint image_center_col = effective_width / 2;
            gint filename_start_col = left_pad + image_center_col - filename_len / 2;

            // Ensure filename doesn't go beyond terminal bounds
            if (filename_start_col < 0) filename_start_col = 0;
            if (filename_start_col + filename_len > app->term_width) {
                filename_start_col = app->term_width - filename_len;
            }
            if (filename_start_col < 0) filename_start_col = 0;

            // Keep filename on the third-to-last line to keep it outside the image area
            gint filename_row = (app->term_height >= 3) ? (app->term_height - 2) : 1;
            printf("\033[%d;%dH", filename_row, filename_start_col + 1);
            printf("\033[34m%s\033[0m", display_name); // Blue filename with reset
            g_free(display_name);
            g_free(safe_basename);
            g_free(basename);
        }
    }

    // Footer hints (single image view) centered on last line
    if (app->term_height > 0 && !app->ui_text_hidden) {
        const HelpSegment segments[] = {
            {"←/→", "Prev/Next"},
            {"Enter", "Preview"},
            {"TAB", "Toggle"},
            {"i", "Info"},
            {"r", "Delete"},
            {"~", "Zen"},
            {"ESC", "Exit"}
        };
        print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
    }

    // If it's an animated image and player is available, start playing if animated
    if (gif_is_animated && app->gif_player) {
        // For first render, just show the first frame, then start animation
        gif_player_play(app->gif_player);
        // Indicate that we are currently displaying an animated GIF
        app->needs_redraw = FALSE; // Don't immediately redraw since animation will handle updates
    } else {
        // For non-animated images, stop any existing animation
        if (app->gif_player) {
            gif_player_stop(app->gif_player);
        }
        if (app->video_player) {
            video_player_stop(app->video_player);
        }
    }

    app_end_sync_update();
    fflush(stdout);

    g_string_free(rendered, TRUE);

    return ERROR_NONE;
}

// Display image information (toggle mode)
ErrorCode app_display_image_info(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // If info is already visible, clear it by redrawing the image
    if (app->info_visible) {
        app->info_visible = FALSE;
        return app_render_current_image(app);
    }

    app->info_visible = TRUE;

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Get image dimensions
    gint width, height;
    ErrorCode error = renderer_get_media_dimensions(filepath, &width, &height);
    if (error != ERROR_NONE) {
        return error;
    }

    // Get file information
    gchar *basename = g_path_get_basename(filepath);
    gchar *dirname = g_path_get_dirname(filepath);
    gchar *safe_basename = sanitize_for_terminal(basename);
    gchar *safe_dirname = sanitize_for_terminal(dirname);
    gint64 file_size = get_file_size(filepath);
    const char *ext = get_file_extension(filepath);

    // Calculate display values
    gdouble file_size_mb = file_size / (1024.0 * 1024.0);
    gdouble aspect_ratio = (height > 0) ? (gdouble)width / height : 1.0;
    gint index = app_get_current_index(app) + 1; // Convert to 1-based
    gint total = app_get_total_images(app);

    // Move to next line and ensure cursor is at the beginning
    printf("\n\033[G"); // New line and move cursor to column 1

    // Display information with colored labels
    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    printf("\033[36m📸 Image Details\033[0m");
    printf("\n\033[G");
    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    printf("\033[36m📁 Filename:\033[0m %s\n\033[G", safe_basename);
    printf("\033[36m📂 Path:\033[0m %s\n\033[G", safe_dirname);
    printf("\033[36m📄 Index:\033[0m %d/%d\n\033[G", index, total);
    printf("\033[36m💾 File size:\033[0m %.1f MB\n\033[G", file_size_mb);
    printf("\033[36m📐 Dimensions:\033[0m %d x %d pixels\n\033[G", width, height);
    printf("\033[36m🎨 Format:\033[0m %s\n\033[G", ext ? ext + 1 : "unknown"); // Skip the dot
    printf("\033[36m🎭 Color mode:\033[0m RGB\n\033[G");
    printf("\033[36m📏 Aspect ratio:\033[0m %.2f\n\033[G", aspect_ratio);
    for (gint i = 0; i < 60; i++) printf("=");
    // Keep cursor on the last line (do not append a newline)

    // Reset terminal attributes to prevent interference with future rendering
    printf("\033[0m");  // Reset all attributes

    fflush(stdout);

    g_free(safe_basename);
    g_free(safe_dirname);
    g_free(basename);
    g_free(dirname);

    return ERROR_NONE;
}

// Refresh the display
ErrorCode app_refresh_display(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    // Update terminal size
    get_terminal_size(&app->term_width, &app->term_height);
    
    if (app->book_preview_mode) {
        return app_render_book_preview(app);
    }
    if (app->book_mode) {
        return app_render_book_page(app);
    }
    if (app->preview_mode) {
        return app_render_preview_grid(app);
    }
    if (app->file_manager_mode) {
        return app_render_file_manager(app);
    }

    // Update preloader with new terminal dimensions
    if (app->preloader && app->preload_enabled) {
        preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
    }
    
    // Update GIF player terminal size if active
    if (app->gif_player) {
        gif_player_update_terminal_size(app->gif_player);
    }
    if (app->video_player) {
        video_player_update_terminal_size(app->video_player);
    }

    return app_render_current_image(app);
}

void app_process_async_render(PixelTermApp *app) {
    if (!app || !app->async_image_pending) {
        return;
    }
    if (app->file_manager_mode || app->preview_mode || app->book_mode || app->book_preview_mode) {
        app_clear_async_render_state(app);
        return;
    }
    if (!app->preloader || !app->preload_enabled) {
        app_clear_async_render_state(app);
        return;
    }

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        app_clear_async_render_state(app);
        return;
    }
    if (app->current_index != app->async_image_index ||
        g_strcmp0(filepath, app->async_image_path) != 0) {
        return;
    }

    gint target_width = 0, target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);
    gint cached_width = 0, cached_height = 0;
    if (!preloader_get_cached_image_dimensions(app->preloader, filepath, target_width, target_height,
                                               &cached_width, &cached_height)) {
        return;
    }
    (void)cached_width;
    (void)cached_height;

    app->async_render_force_sync = TRUE;
    app->suppress_full_clear = TRUE;
    app_clear_async_render_state(app);
    app_render_current_image(app);
}

ErrorCode app_render_by_mode(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    if (app->book_preview_mode) {
        return app_render_book_preview(app);
    }
    if (app->book_mode) {
        return app_render_book_page(app);
    }
    if (app->preview_mode) {
        return app_render_preview_grid(app);
    }
    if (app->file_manager_mode) {
        return app_render_file_manager(app);
    }
    return app_refresh_display(app);
}



// Toggle preloading
void app_toggle_preload(PixelTermApp *app) {
    if (app) {
        app->preload_enabled = !app->preload_enabled;
        
        if (app->preload_enabled) {
            // Enable preloading
            if (!app->preloader) {
                app->preloader = preloader_create();
                if (app->preloader) {
                    preloader_initialize(app->preloader, app->dither_enabled, app->render_work_factor,
                                         app->force_text, app->force_sixel, app->force_kitty, app->force_iterm2,
                                         app->gamma);
                    
                    // Set terminal dimensions for preloader
                    preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
                    gint target_width = 0, target_height = 0;
                    app_get_image_target_dimensions(app, &target_width, &target_height);
                    
                    ErrorCode preload_err = preloader_start(app->preloader);
                    if (preload_err == ERROR_NONE) {
                        // Add initial preload tasks
                        preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
                    } else {
                        preloader_destroy(app->preloader);
                        app->preloader = NULL;
                        app->preload_enabled = FALSE;
                    }
                }
            } else {
                // Update terminal dimensions before enabling
                preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
                preloader_enable(app->preloader);
                preloader_resume(app->preloader);
            }
        } else {
            // Disable preloading
            if (app->preloader) {
                preloader_disable(app->preloader);
                preloader_clear_queue(app->preloader);
            }
        }
    }
}

// Check if application should exit
gboolean app_should_exit(const PixelTermApp *app) {
    return !app || !app->running;
}

// Delete current image
ErrorCode app_delete_current_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Safety guard: refuse to delete symlinks or files outside current directory
    struct stat st;
    if (lstat(filepath, &st) != 0) {
        return ERROR_FILE_NOT_FOUND;
    }
    if (S_ISLNK(st.st_mode)) {
        return ERROR_INVALID_IMAGE;
    }
    gchar *canonical_file = g_canonicalize_filename(filepath, NULL);
    gchar *canonical_dir = app->current_directory ? g_canonicalize_filename(app->current_directory, NULL)
                                                  : g_get_current_dir();
    gboolean allowed = FALSE;
    if (canonical_file && canonical_dir) {
        size_t dir_len = strlen(canonical_dir);
        if (dir_len > 0 && canonical_file && strncmp(canonical_file, canonical_dir, dir_len) == 0) {
            // Ensure boundary: either exact dir or subpath separator
            if (canonical_file[dir_len] == '\0' || canonical_file[dir_len] == '/') {
                allowed = TRUE;
            }
        }
    }
    g_free(canonical_file);
    g_free(canonical_dir);
    if (!allowed) {
        return ERROR_INVALID_IMAGE;
    }

    // Remove file
    if (unlink(filepath) != 0) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Remove from file list
    GList *current_link = g_list_nth(app->image_files, app->current_index);
    if (current_link) {
        // Remove from preload cache
        if (app->preloader && app->preload_enabled) {
            preloader_cache_remove(app->preloader, (const char*)current_link->data);
        }
        
        g_free(current_link->data);
        app->image_files = g_list_delete_link(app->image_files, current_link);
        app->total_images--;
        
        // Adjust index if necessary
        if (app->current_index >= app->total_images && app->current_index > 0) {
            app->current_index--;
        }
        
        // Update preload tasks after deletion
        if (app->preloader && app->preload_enabled && app_has_images(app)) {
            gint target_width = 0, target_height = 0;
            app_get_image_target_dimensions(app, &target_width, &target_height);
            preloader_clear_queue(app->preloader);
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
        }
    }

    app->needs_redraw = TRUE;
    return ERROR_NONE;
}

// Get current image index
gint app_get_current_index(const PixelTermApp *app) {
    return app ? app->current_index : -1;
}

// Get total number of images
gint app_get_total_images(const PixelTermApp *app) {
    return app ? app->total_images : 0;
}

// Get current file path
const gchar* app_get_current_filepath(const PixelTermApp *app) {
    if (!app || !app->image_files || app->current_index < 0) {
        return NULL;
    }

    return (const gchar*)g_list_nth_data(app->image_files, app->current_index);
}

// Check if application has images
gboolean app_has_images(const PixelTermApp *app) {
    return app && app->image_files && app->total_images > 0;
}

// Enter file manager mode
ErrorCode app_enter_file_manager(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    // Stop GIF playback if active
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player) {
        video_player_stop(app->video_player);
    }

    app->file_manager_mode = TRUE;
    app->selected_entry = 0;
    app->scroll_offset = 0;
    
    if (app->file_manager_directory) {
        g_free(app->file_manager_directory);
        app->file_manager_directory = NULL;
    }
    // Start browsing from current image directory if available, else current working dir
    if (app->current_directory) {
        app->file_manager_directory = g_strdup(app->current_directory);
    } else {
        app->file_manager_directory = g_get_current_dir();
    }

    // Always start with hidden files hidden
    app->show_hidden_files = FALSE;
    
    // Clear screen on mode entry to avoid ghosting
    printf("\033[2J\033[H\033[0m");
    fflush(stdout);
    
    return app_file_manager_refresh(app);
}

// Exit file manager mode
ErrorCode app_exit_file_manager(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    app->file_manager_mode = FALSE;
    app->pending_file_manager_single_click = FALSE;
    
    // Cleanup directory entries
    if (app->directory_entries) {
        g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
        app->directory_entries = NULL;
    }
    g_clear_pointer(&app->file_manager_directory, g_free);
    
    // Reset info visibility to ensure proper display
    app->info_visible = FALSE;
    app->needs_redraw = TRUE;
    
    return ERROR_NONE;
}

// Move selection up in file manager
ErrorCode app_file_manager_up(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    gint total_entries = g_list_length(app->directory_entries);
    if (total_entries <= 0) {
        return ERROR_NONE;
    }

    if (app->selected_entry >= cols) {
        app->selected_entry -= cols;
    } else {
        // Jump to last entry when at the top and pressing up
        app->selected_entry = total_entries - 1;
    }
    
    // Adjust scroll using the improved algorithm
    app_file_manager_adjust_scroll(app, cols, visible_rows);

    return ERROR_NONE;
}

// Move selection down in file manager
ErrorCode app_file_manager_down(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);

    gint total_entries = g_list_length(app->directory_entries);
    if (total_entries <= 0) {
        return ERROR_NONE;
    }
    gint target = app->selected_entry + cols;
    if (target < total_entries) {
        app->selected_entry = target;
    } else {
        // Jump to first entry when at the bottom and pressing down
        app->selected_entry = 0;
    }
    
    // Adjust scroll using the improved algorithm
    app_file_manager_adjust_scroll(app, cols, visible_rows);

    return ERROR_NONE;
}

// Move selection left in file manager
ErrorCode app_file_manager_left(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    const gchar *current_dir = app->file_manager_directory;
    gboolean free_dir = FALSE;
    if (!current_dir) {
        current_dir = app->current_directory;
    }
    if (!current_dir) {
        current_dir = g_get_current_dir();
        free_dir = TRUE;
    }

    // Remember the directory we came from so we can highlight it in the parent view
    gchar *child_dir = g_strdup(current_dir);

    gchar *parent = g_path_get_dirname(current_dir);
    if (parent && g_strcmp0(parent, current_dir) != 0) {
        g_free(app->file_manager_directory);
        app->file_manager_directory = g_canonicalize_filename(parent, NULL);
        if (!app->file_manager_directory) {
            g_free(parent);
            g_free(child_dir);
            if (free_dir) g_free((gchar*)current_dir);
            return ERROR_FILE_NOT_FOUND;
        }
        // Refresh file manager with new directory
        ErrorCode err = app_file_manager_refresh(app);

        // Highlight the directory we just came from in the parent listing
        if (err == ERROR_NONE && child_dir) {
            gint idx = 0;
            for (GList *cur = app->directory_entries; cur; cur = cur->next, idx++) {
                gchar *path = (gchar*)cur->data;
                if (g_strcmp0(path, child_dir) == 0) {
                    app->selected_entry = idx;
                    // Recalculate scroll to keep selection visible
                    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
                    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
                    app_file_manager_adjust_scroll(app, cols, visible_rows);
                    break;
                }
            }
        }

        g_free(parent);
        g_free(child_dir);
        if (free_dir) g_free((gchar*)current_dir);
        return err;
    }

    g_free(parent);
    g_free(child_dir);
    if (free_dir) g_free((gchar*)current_dir);
    return ERROR_NONE;
}

// Move selection right in file manager
ErrorCode app_file_manager_right(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    return app_file_manager_enter(app);
}

// Enter selected directory or open file in file manager
ErrorCode app_file_manager_enter(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    if (app->selected_entry >= g_list_length(app->directory_entries)) {
        return ERROR_INVALID_IMAGE;
    }

    gchar *selected_path = (gchar*)g_list_nth_data(app->directory_entries, app->selected_entry);
    if (!selected_path) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if it's a directory
    if (g_file_test(selected_path, G_FILE_TEST_IS_DIR)) {
        // Load the selected directory
        g_free(app->file_manager_directory);
        app->file_manager_directory = g_canonicalize_filename(selected_path, NULL);
        if (!app->file_manager_directory) {
            return ERROR_FILE_NOT_FOUND;
        }
        // Reset selection to first entry when changing directory
        app->selected_entry = 0;
        app->scroll_offset = 0;
        // Refresh file manager with new directory
        return app_file_manager_refresh(app);
    } else {
        if (is_valid_book_file(selected_path)) {
            ErrorCode error = app_open_book(app, selected_path);
            if (error != ERROR_NONE) {
                return error;
            }

            printf("\033[2J\033[H\033[0m");
            fflush(stdout);

            app->file_manager_mode = FALSE;
            if (app->directory_entries) {
                g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
                app->directory_entries = NULL;
            }
            g_clear_pointer(&app->file_manager_directory, g_free);
            app->info_visible = FALSE;
            app->needs_redraw = TRUE;

            app_enter_book_preview(app);
            app_render_book_preview(app);
            fflush(stdout);
            return ERROR_NONE;
        }

        // It's a file, check if it's a valid image file before loading
        if (!is_valid_media_file(selected_path)) {
            // If it's not a valid media file, don't try to load it
            return ERROR_INVALID_IMAGE;
        }

        ErrorCode error = app_load_single_file(app, selected_path);

        // Only exit file manager if file was loaded successfully
        if (error == ERROR_NONE) {
            // Clear screen before exiting file manager
            printf("\033[2J\033[H\033[0m");
            fflush(stdout);

            // Exit file manager mode
            app->file_manager_mode = FALSE;
            // Cleanup directory entries
            if (app->directory_entries) {
                g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
                app->directory_entries = NULL;
            }
            g_clear_pointer(&app->file_manager_directory, g_free);
            // Reset info visibility to ensure proper display
            app->info_visible = FALSE;
            app->needs_redraw = TRUE;
            // Force immediate rendering
            app_render_current_image(app);
            fflush(stdout);
        }
        return error;
    }
}

// Refresh file manager directory listing
ErrorCode app_file_manager_refresh(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    // Clear existing entries
    if (app->directory_entries) {
        g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
        app->directory_entries = NULL;
    }

    // Resolve directory to display: prefer file manager dir, then viewer dir, then cwd
    gchar *base_dir_dup = NULL;
    if (app->file_manager_directory) {
        base_dir_dup = g_strdup(app->file_manager_directory);
    } else if (app->current_directory) {
        base_dir_dup = g_strdup(app->current_directory);
    } else {
        base_dir_dup = g_get_current_dir();
    }

    gchar *current_dir = g_canonicalize_filename(base_dir_dup, NULL);
    g_free(base_dir_dup);
    if (!current_dir) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Persist canonical directory for consistent rendering/navigation
    g_free(app->file_manager_directory);
    app->file_manager_directory = current_dir;

    // Open directory
    GDir *dir = g_dir_open(current_dir, 0, NULL);
    if (!dir) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Collect all entries
    GList *entries = NULL;
    gchar *parent_entry = NULL;
    gchar *parent_dir = g_path_get_dirname(current_dir);
    if (parent_dir) {
        if (g_strcmp0(parent_dir, current_dir) != 0) {
            parent_entry = g_build_filename(current_dir, "..", NULL);
            entries = g_list_append(entries, parent_entry);
        }
        g_free(parent_dir);
    }
    const gchar *name;

    // Add directories and files (skip current directory entry if encountered)
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *full_path = g_build_filename(current_dir, name, NULL);
        
        if (g_strcmp0(full_path, current_dir) == 0) {
            g_free(full_path);
            continue;
        }

        // Respect hidden-file visibility toggle
        if (!app->show_hidden_files && name[0] == '.') {
            g_free(full_path);
            continue;
        }

        // Skip system-like entries that start with special symbols (e.g., $RECYCLE.BIN)
        if (app_file_manager_is_special_entry(name)) {
            g_free(full_path);
            continue;
        }
        
        if (g_file_test(full_path, G_FILE_TEST_IS_DIR) || g_file_test(full_path, G_FILE_TEST_IS_REGULAR)) {
            entries = g_list_append(entries, full_path);  // Use full path
        } else {
            g_free(full_path);
        }
    }

    g_dir_close(dir);

    // Sort entries: directories first, then files; each group alphabetical
    GList *dirs = NULL;
    GList *files = NULL;
    for (GList *cur = entries; cur != NULL; cur = cur->next) {
        gchar *path = (gchar*)cur->data;
        if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            dirs = g_list_prepend(dirs, path);
        } else {
            files = g_list_prepend(files, path);
        }
    }
    dirs = g_list_sort(dirs, (GCompareFunc)app_file_manager_compare_names);
    if (parent_entry) {
        dirs = g_list_remove(dirs, parent_entry);
        dirs = g_list_prepend(dirs, parent_entry);
    }
    files = g_list_sort(files, (GCompareFunc)app_file_manager_compare_names);
    app->directory_entries = g_list_concat(dirs, files);
    g_list_free(entries); // pointers moved into dirs/files concatenated list
    app->selected_entry = 0;
    app->scroll_offset = 0;
    app_file_manager_select_current_image(app);

    // Default: avoid selecting ".." when entering a directory; pick the first real entry.
    if (app->directory_entries && app->selected_entry == 0 && g_list_length(app->directory_entries) > 1) {
        const gchar *first = (const gchar*)g_list_nth_data(app->directory_entries, 0);
        if (first) {
            gchar *base = g_path_get_basename(first);
            gboolean is_parent = (base && g_strcmp0(base, "..") == 0);
            g_free(base);
            if (is_parent) {
                app->selected_entry = 1;
            }
        }
    }

    return ERROR_NONE;
}

ErrorCode app_file_manager_select_path(PixelTermApp *app, const char *path) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!path || !app->directory_entries) {
        return ERROR_FILE_NOT_FOUND;
    }

    gchar *target = g_canonicalize_filename(path, NULL);
    if (!target) {
        return ERROR_FILE_NOT_FOUND;
    }

    gboolean found = FALSE;
    gint idx = 0;
    for (GList *cur = app->directory_entries; cur; cur = cur->next, idx++) {
        gchar *entry = (gchar*)cur->data;
        if (g_strcmp0(entry, target) == 0) {
            app->selected_entry = idx;
            found = TRUE;
            break;
        }
    }
    g_free(target);

    if (!found) {
        return ERROR_FILE_NOT_FOUND;
    }

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, cols, visible_rows);
    return ERROR_NONE;
}

// Check if current file manager directory contains any images
gboolean app_file_manager_has_images(PixelTermApp *app) {
    if (!app || !app->directory_entries) {
        return FALSE;
    }

    // Check if any entry in the current directory listing is an image file
    for (GList *cur = app->directory_entries; cur; cur = cur->next) {
        gchar *path = (gchar*)cur->data;
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR) && is_valid_media_file(path)) {
            return TRUE;
        }
    }

    return FALSE;
}

// Check if current file manager selection is an image file
gboolean app_file_manager_selection_is_image(PixelTermApp *app) {
    if (!app || !app->file_manager_mode || !app->directory_entries) {
        return FALSE;
    }

    if (app->selected_entry < 0 || app->selected_entry >= g_list_length(app->directory_entries)) {
        return FALSE;
    }

    gchar *path = (gchar*)g_list_nth_data(app->directory_entries, app->selected_entry);
    if (!path) {
        return FALSE;
    }

    return g_file_test(path, G_FILE_TEST_IS_REGULAR) && is_valid_media_file(path);
}

// Get the image index of the currently selected file in file manager
gint app_file_manager_get_selected_image_index(PixelTermApp *app) {
    if (!app || !app->file_manager_mode || !app->directory_entries || !app->image_files) {
        return -1;
    }

    if (app->selected_entry < 0 || app->selected_entry >= g_list_length(app->directory_entries)) {
        return -1;
    }

    gchar *selected_path = (gchar*)g_list_nth_data(app->directory_entries, app->selected_entry);
    if (!selected_path) {
        return -1;
    }

    // Find the index of this file in the image list
    gint image_index = 0;
    for (GList *cur = app->image_files; cur; cur = cur->next, image_index++) {
        const gchar *image_path = (const gchar*)cur->data;
        if (g_strcmp0(selected_path, image_path) == 0) {
            return image_index;
        }
    }

    return -1; // Not found
}

// Toggle hidden files visibility while preserving selection when possible
ErrorCode app_file_manager_toggle_hidden(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    // Remember current selection path
    gchar *prev_selected = NULL;
    if (app->selected_entry >= 0 && app->selected_entry < g_list_length(app->directory_entries)) {
        gchar *path = (gchar*)g_list_nth_data(app->directory_entries, app->selected_entry);
        if (path) {
            prev_selected = g_strdup(path);
        }
    }

    app->show_hidden_files = !app->show_hidden_files;
    ErrorCode err = app_file_manager_refresh(app);
    if (err != ERROR_NONE) {
        g_free(prev_selected);
        return err;
    }

    // Restore selection to the same path if still visible
    if (prev_selected) {
        gint idx = 0;
        for (GList *cur = app->directory_entries; cur; cur = cur->next, idx++) {
            gchar *path = (gchar*)cur->data;
            if (g_strcmp0(path, prev_selected) == 0) {
                app->selected_entry = idx;
                break;
            }
        }
        g_free(prev_selected);
    }

    // Ensure scroll offset keeps selection visible
    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, cols, visible_rows);

    return ERROR_NONE;
}

// Handle mouse click in file manager
ErrorCode app_handle_mouse_file_manager(PixelTermApp *app, gint mouse_x, gint mouse_y) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint hit_index = -1;
    if (!app_file_manager_hit_test(app, mouse_x, mouse_y, &hit_index)) {
        return ERROR_NONE;
    }

    if (hit_index == app->selected_entry) {
        return ERROR_NONE;
    }

    app->selected_entry = hit_index;

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, cols, visible_rows);

    return ERROR_NONE;
}

ErrorCode app_file_manager_enter_at_position(PixelTermApp *app, gint mouse_x, gint mouse_y) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint hit_index = -1;
    if (!app_file_manager_hit_test(app, mouse_x, mouse_y, &hit_index)) {
        return ERROR_INVALID_IMAGE;
    }

    gint prev_selected = app->selected_entry;
    gint prev_scroll = app->scroll_offset;
    app->selected_entry = hit_index;
    ErrorCode err = app_file_manager_enter(app);

    if (err != ERROR_NONE && app->file_manager_mode) {
        app->selected_entry = prev_selected;
        app->scroll_offset = prev_scroll;
    }

    return err;
}

// ----- Preview grid helpers -----
typedef struct {
    gint cols;
    gint rows;
    gint cell_width;
    gint cell_height;
    gint header_lines;
    gint visible_rows;
} PreviewLayout;

static gint app_preview_bottom_reserved_lines(const PixelTermApp *app);
static gint app_preview_compute_vertical_offset(const PixelTermApp *app,
                                               const PreviewLayout *layout,
                                               gint start_row,
                                               gint end_row);

// Calculate preview grid layout using preview_zoom as target cell width
static PreviewLayout app_preview_calculate_layout(PixelTermApp *app) {
    PreviewLayout layout = {1, 1, app ? app->term_width : 80, 10, 3, 1};
    if (!app || app->total_images <= 0) {
        return layout;
    }

    const gint header_lines = app->ui_text_hidden ? 0 : 3;
    gint usable_width = app->term_width > 0 ? app->term_width : 80;
    gint bottom_reserved = app_preview_bottom_reserved_lines(app);
    gint usable_height = app->term_height > header_lines + bottom_reserved
                             ? app->term_height - header_lines - bottom_reserved
                             : 6;

    // If preview_zoom (target cell width) is uninitialized, default to ~30 chars/col
    if (app->preview_zoom <= 0) {
        app->preview_zoom = 30;
    }

    // Calculate columns based on target width
    gint cols = usable_width / app->preview_zoom;

    // Enforce minimum of 2 columns as requested
    if (cols < 2) cols = 2;

    // Enforce reasonable minimum width per column (e.g. 4 chars) to prevent garbage
    if (usable_width / cols < 4) {
        cols = usable_width / 4;
        if (cols < 2) cols = 2; // Priority to min 2 columns if width allows
    }

    gint cell_width = usable_width / cols;

    // Determine cell height based on aspect ratio (approx 2:1 char aspect)
    // We add 1 to compensate for the border padding (2 chars) which affects height proportionately more than width
    // Target: (w-2)/(h-2) = 2  =>  w-2 = 2h-4  =>  2h = w+2  =>  h = w/2 + 1
    gint cell_height = cell_width / 2 + 1;
    if (cell_height < 4) cell_height = 4;

    // Calculate rows
    gint rows = (app->total_images + cols - 1) / cols;
    if (rows < 1) rows = 1;

    gint visible_rows = usable_height / cell_height;
    if (visible_rows < 1) visible_rows = 1;

    layout.cols = cols;
    layout.rows = rows;
    layout.cell_width = cell_width;
    layout.cell_height = cell_height;
    layout.header_lines = header_lines;
    layout.visible_rows = visible_rows;
    return layout;
}

static void app_preview_adjust_scroll(PixelTermApp *app, const PreviewLayout *layout) {
    if (!app || !layout) {
        return;
    }

    gint total_rows = layout->rows;
    gint visible_rows = layout->visible_rows;
    if (visible_rows < 1) visible_rows = 1;

    // Clamp scroll to valid range
    // Allow scrolling until the last row is at the top (pagination style)
    gint max_offset = MAX(0, total_rows - 1);
    if (app->preview_scroll > max_offset) {
        app->preview_scroll = max_offset;
    }
    if (app->preview_scroll < 0) {
        app->preview_scroll = 0;
    }

    // Ensure selection is visible
    gint row = app->preview_selected / layout->cols;
    if (row < app->preview_scroll) {
        app->preview_scroll = row;
    } else if (row >= app->preview_scroll + visible_rows) {
        app->preview_scroll = row - visible_rows + 1;
    }
}

// Queue preload tasks for currently visible (and adjacent) preview cells
static void app_preview_queue_preloads(PixelTermApp *app, const PreviewLayout *layout) {
    if (!app || !layout || !app->preloader || !app->preload_enabled) {
        return;
    }

    gint content_width = MAX(2, layout->cell_width - 2);
    gint content_height = MAX(2, layout->cell_height - 2);

    // Preload current screen plus one row of lookahead/behind to smooth paging
    gint start_row = MAX(0, app->preview_scroll - 1);
    gint end_row = MIN(layout->rows, app->preview_scroll + layout->visible_rows + 1);
    gint start_index = start_row * layout->cols;
    GList *cursor = g_list_nth(app->image_files, start_index);

    for (gint row = start_row; row < end_row && cursor; row++) {
        for (gint col = 0; col < layout->cols && cursor; col++) {
            gint idx = row * layout->cols + col;
            if (idx >= app->total_images) {
                cursor = NULL;
                break;
            }
            const gchar *filepath = (const gchar*)cursor->data;
            gint distance = ABS(idx - app->preview_selected);
            gint priority = (distance == 0) ? 0 : (distance <= layout->cols ? 1 : 5 + distance);
            preloader_add_task(app->preloader, filepath, priority, content_width, content_height);
            cursor = cursor->next;
        }
    }
}

static gint app_preview_bottom_reserved_lines(const PixelTermApp *app) {
    if (app && app->ui_text_hidden) {
        return 0;
    }
    // Row -2: filename, Row -1: spacer, Row -0: footer hints.
    return 3;
}

static gint app_preview_compute_vertical_offset(const PixelTermApp *app,
                                               const PreviewLayout *layout,
                                               gint start_row,
                                               gint end_row) {
    if (!app || !layout) {
        return 0;
    }
    gint bottom_reserved = app_preview_bottom_reserved_lines(app);
    gint available = app->term_height - layout->header_lines - bottom_reserved;
    if (available < 0) {
        available = 0;
    }
    gint rows_drawn = MAX(0, end_row - start_row);
    gint grid_h = rows_drawn * layout->cell_height;
    if (grid_h >= available) {
        return 0;
    }
    return (available - grid_h) / 2;
}

static void app_preview_render_selected_filename(PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height < 3) {
        return;
    }

    const gchar *sel_path = (const gchar*)g_list_nth_data(app->image_files, app->preview_selected);
    if (!sel_path) {
        return;
    }

    gchar *base = g_path_get_basename(sel_path);
    gchar *safe = sanitize_for_terminal(base);
    gint max_width = app_filename_max_width(app);
    if (max_width <= 0) {
        max_width = app->term_width;
    }
    gchar *display_name = truncate_utf8_middle_keep_suffix(safe, max_width);
    gint row = app->term_height - 2;
    gint name_len = utf8_display_width(display_name);
    gint pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
    printf("\033[%d;1H", row);
    for (gint i = 0; i < app->term_width; i++) putchar(' ');
    if (name_len > 0) {
        printf("\033[%d;%dH\033[34m%s\033[0m", row, pad + 1, display_name);
    }
    g_free(display_name);
    g_free(safe);
    g_free(base);
}

static gboolean app_preview_get_cell_origin(const PixelTermApp *app,
                                            const PreviewLayout *layout,
                                            gint index,
                                            gint start_row,
                                            gint vertical_offset,
                                            gint *cell_x,
                                            gint *cell_y) {
    if (!app || !layout || !cell_x || !cell_y) {
        return FALSE;
    }
    if (index < 0 || index >= app->total_images) {
        return FALSE;
    }
    gint row = index / layout->cols;
    gint col = index % layout->cols;
    if (row < start_row || row >= start_row + layout->visible_rows) {
        return FALSE;
    }
    *cell_x = col * layout->cell_width + 1;
    *cell_y = layout->header_lines + vertical_offset + (row - start_row) * layout->cell_height + 1;
    return TRUE;
}

static void app_preview_clear_cell_border(const PixelTermApp *app,
                                          const PreviewLayout *layout,
                                          gint index,
                                          gint start_row,
                                          gint vertical_offset) {
    if (!app || !layout) {
        return;
    }
    if (layout->cell_width < 4 || layout->cell_height < 4) {
        return;
    }
    gint cell_x = 0;
    gint cell_y = 0;
    if (!app_preview_get_cell_origin(app, layout, index, start_row, vertical_offset, &cell_x, &cell_y)) {
        return;
    }

    printf("\033[0m");
    printf("\033[%d;%dH", cell_y, cell_x);
    for (gint c = 0; c < layout->cell_width; c++) putchar(' ');

    gint bottom_y = cell_y + layout->cell_height - 1;
    printf("\033[%d;%dH", bottom_y, cell_x);
    for (gint c = 0; c < layout->cell_width; c++) putchar(' ');

    for (gint line = 1; line < layout->cell_height - 1; line++) {
        gint y = cell_y + line;
        printf("\033[%d;%dH ", y, cell_x);
        printf("\033[%d;%dH ", y, cell_x + layout->cell_width - 1);
    }
}

static void app_preview_draw_cell_border(const PixelTermApp *app,
                                         const PreviewLayout *layout,
                                         gint index,
                                         gint start_row,
                                         gint vertical_offset) {
    if (!app || !layout) {
        return;
    }
    if (layout->cell_width < 4 || layout->cell_height < 4) {
        return;
    }
    gint cell_x = 0;
    gint cell_y = 0;
    if (!app_preview_get_cell_origin(app, layout, index, start_row, vertical_offset, &cell_x, &cell_y)) {
        return;
    }

    const char *border_style = (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL)
                                   ? "\033[33;1m"
                                   : "\033[34;1m";
    printf("\033[%d;%dH%s+", cell_y, cell_x, border_style);
    for (gint c = 0; c < layout->cell_width - 2; c++) putchar('-');
    printf("+\033[0m");

    for (gint line = 1; line < layout->cell_height - 1; line++) {
        gint y = cell_y + line;
        printf("\033[%d;%dH%s|\033[0m", y, cell_x, border_style);
        printf("\033[%d;%dH%s|\033[0m", y, cell_x + layout->cell_width - 1, border_style);
    }

    gint bottom_y = cell_y + layout->cell_height - 1;
    printf("\033[%d;%dH%s+", bottom_y, cell_x, border_style);
    for (gint c = 0; c < layout->cell_width - 2; c++) putchar('-');
    printf("+\033[0m");
}

// Print brief info for the currently selected preview item on the status line
ErrorCode app_preview_print_info(PixelTermApp *app) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Use current selection (preview) or current image index if available
    const gchar *filepath = NULL;
    gint display_index = 0;
    if (app->preview_mode) {
        filepath = (const gchar*)g_list_nth_data(app->image_files, app->preview_selected);
        display_index = app->preview_selected;
    } else {
        filepath = app_get_current_filepath(app);
        display_index = app_get_current_index(app);
    }
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    gchar *basename = g_path_get_basename(filepath);
    gchar *dirname = g_path_get_dirname(filepath);
    gchar *safe_basename = sanitize_for_terminal(basename);
    gchar *safe_dirname = sanitize_for_terminal(dirname);
    gint width = 0, height = 0;
    renderer_get_media_dimensions(filepath, &width, &height);
    gint64 file_size = get_file_size(filepath);
    gdouble file_size_mb = file_size > 0 ? file_size / (1024.0 * 1024.0) : 0.0;
    const char *ext = get_file_extension(filepath);
    gdouble aspect = (height > 0) ? (gdouble)width / height : 0.0;

    // Refresh terminal size in case of resize
    get_terminal_size(&app->term_width, &app->term_height);

    gint start_row = app->term_height - 8;
    if (start_row < 1) start_row = 1;

    const char *labels[7] = {
        "📁 Filename:",
        "📂 Path:",
        "📄 Index:",
        "💾 File size:",
        "📐 Dimensions:",
        "🎨 Format:",
        "📏 Aspect ratio:"
    };

    char values[7][256];
    g_snprintf(values[0], sizeof(values[0]), "%s", safe_basename ? safe_basename : "");
    g_snprintf(values[1], sizeof(values[1]), "%s", safe_dirname ? safe_dirname : "");
    g_snprintf(values[2], sizeof(values[2]), "%d/%d", display_index + 1, app->total_images);
    g_snprintf(values[3], sizeof(values[3]), "%.1f MB", file_size_mb);
    g_snprintf(values[4], sizeof(values[4]), "%d x %d pixels", width, height);
    g_snprintf(values[5], sizeof(values[5]), "%s", ext ? ext + 1 : "unknown");
    g_snprintf(values[6], sizeof(values[6]), "%.2f", aspect);

    // Clear area and print info block with colored labels
    for (gint i = 0; i < 7; i++) {
        gint row = start_row + i;
        printf("\033[%d;1H", row);
        for (gint c = 0; c < app->term_width; c++) putchar(' ');
        printf("\033[%d;1H\033[36m%s\033[0m %s", row, labels[i], values[i]);
    }
    printf("\033[0m");
    fflush(stdout);

    g_free(safe_basename);
    g_free(safe_dirname);
    g_free(basename);
    g_free(dirname);
    return ERROR_NONE;
}

// Compute visible width of a line ignoring ANSI escape sequences
static gint app_preview_visible_width(const char *str, gint len) {
    if (!str || len <= 0) {
        return 0;
    }
    gint width = 0;
    for (gint i = 0; i < len; i++) {
        if (str[i] == '\033' && i + 1 < len && str[i + 1] == '[') {
            // Skip CSI sequence
            i += 2;
            while (i < len && str[i] != 'm' && (str[i] < 'A' || str[i] > 'z')) {
                i++;
            }
            continue;
        }
        width++;
    }
    return width;
}

// Move selection inside preview grid
ErrorCode app_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint cols = layout.cols;
    gint rows = layout.rows;

    gint old_scroll = app->preview_scroll;

    gint row = app->preview_selected / cols;
    gint col = app->preview_selected % cols;

    // Page-aware movement: if moving past the visible window, jump by a full page and select first item
    row += delta_row;
    col += delta_col;

    // Horizontal wrap within current row
    if (delta_col < 0 && col < 0) {
        col = cols - 1;
    } else if (delta_col > 0 && col >= cols) {
        col = 0;
    }

    // Wrap vertically across pages
    if (delta_row > 0 && row >= rows) {
        row = 0;
        app->preview_scroll = 0;
    } else if (delta_row < 0 && row < 0) {
        gint visible_rows = layout.visible_rows > 0 ? layout.visible_rows : 1;
        gint last_page_scroll = 0;
        if (rows > 0) {
            last_page_scroll = ((rows - 1) / visible_rows) * visible_rows;
            if (last_page_scroll < 0) {
                last_page_scroll = 0;
            } else if (last_page_scroll > rows - 1) {
                last_page_scroll = rows - 1;
            }
        }
        row = rows - 1;
        app->preview_scroll = last_page_scroll;
    } else if (delta_row > 0 && row >= app->preview_scroll + layout.visible_rows) {
        gint new_scroll = MIN(app->preview_scroll + layout.visible_rows, MAX(rows - 1, 0));
        app->preview_scroll = new_scroll;
        row = new_scroll; // first row of next page, keep column
    } else if (delta_row < 0 && row < app->preview_scroll) {
        gint new_scroll = MAX(app->preview_scroll - layout.visible_rows, 0);
        app->preview_scroll = new_scroll;
        row = MIN(new_scroll + layout.visible_rows - 1, rows - 1); // last row of prev page, keep column
    }

    if (row < 0) row = 0;
    if (row >= rows) row = rows - 1;
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;

    gint new_index = row * cols + col;
    // Keep selection inside current row bounds when wrapping on incomplete rows
    gint row_start = row * cols;
    gint row_end = MIN(app->total_images - 1, row_start + cols - 1);
    if (new_index < row_start) new_index = row_start;
    if (new_index > row_end) new_index = row_end;
    if (new_index >= app->total_images) {
        new_index = app->total_images - 1;
    }
    app->preview_selected = new_index;

    app_preview_adjust_scroll(app, &layout);
    if (app->preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

// Jump a page up/down based on visible rows
ErrorCode app_preview_page_move(PixelTermApp *app, gint direction) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;
    gint total_pages = (layout.rows + rows_per_page - 1) / rows_per_page;
    if (total_pages <= 1) {
        // Page scrolling is a no-op when everything fits on one page.
        return ERROR_NONE;
    }

    gint old_selected = app->preview_selected;
    gint old_scroll = app->preview_scroll;
    gint rows = layout.rows;
    gint cols = layout.cols;

    gint current_row = cols > 0 ? app->preview_selected / cols : 0;
    gint current_col = cols > 0 ? app->preview_selected % cols : 0;
    gint relative_row = current_row - app->preview_scroll;
    if (relative_row < 0) relative_row = 0;
    if (relative_row >= rows_per_page) relative_row = rows_per_page - 1;

    gint delta_scroll = direction >= 0 ? rows_per_page : -rows_per_page;
    gint new_scroll = app->preview_scroll + delta_scroll;
    gint last_page_scroll = ((rows - 1) / rows_per_page) * rows_per_page;
    if (last_page_scroll < 0) last_page_scroll = 0;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > last_page_scroll) new_scroll = last_page_scroll;

    gint new_row = new_scroll + relative_row;
    if (new_row < 0) new_row = 0;
    if (new_row >= rows) new_row = rows - 1;

    if (current_col < 0) current_col = 0;
    if (current_col >= cols) current_col = cols - 1;

    gint new_index = new_row * cols + current_col;
    gint row_start = new_row * cols;
    gint row_end = MIN(app->total_images - 1, row_start + cols - 1);
    if (new_index < row_start) new_index = row_start;
    if (new_index > row_end) new_index = row_end;
    if (new_index >= app->total_images) new_index = app->total_images - 1;
    if (new_index < 0) new_index = 0;

    app->preview_scroll = new_scroll;
    app->preview_selected = new_index;

    if (app->preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    if (app->preview_selected == old_selected && app->preview_scroll == old_scroll) {
        return ERROR_NONE;
    }
    return ERROR_NONE;
}

// Change preview zoom (target cell width) by stepping column count
ErrorCode app_preview_change_zoom(PixelTermApp *app, gint delta) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint usable_width = app->term_width > 0 ? app->term_width : 80;

    // Initialize if needed (default to 4 columns)
    if (app->preview_zoom <= 0) {
        app->preview_zoom = usable_width / 4; // Start with 4 columns
    }

    // Calculate current implied columns with proper rounding
    gint current_cols = (gint)(usable_width / app->preview_zoom + 0.5f);
    if (current_cols < 2) current_cols = 2;
    if (current_cols > 12) current_cols = 12;

    // Apply delta to columns.
    // +delta means Zoom In -> Larger Images -> Fewer Columns
    // -delta means Zoom Out -> Smaller Images -> More Columns
    gint new_cols = current_cols - delta;

    // Apply limits
    if (new_cols < 2) new_cols = 2;   // Minimum 2 columns
    if (new_cols > 12) new_cols = 12; // Maximum 12 columns

    // Check if we're already at limits and trying to go further
    if (new_cols == current_cols) {
        // Already at zoom limit, don't refresh
        return ERROR_NONE;
    }

    // Update target width based on new column count
    app->preview_zoom = (gdouble)usable_width / new_cols;
    if (app->preview_zoom < 1) app->preview_zoom = 1;

    // Mark for screen clear since layout changed
    app->needs_screen_clear = TRUE;

    // Only refresh if zoom actually changed
    return app_render_preview_grid(app);
}

static PreviewLayout app_book_preview_calculate_layout(PixelTermApp *app) {
    PreviewLayout layout = {1, 1, app ? app->term_width : 80, 10, 3, 1};
    if (!app || app->book_page_count <= 0) {
        return layout;
    }

    const gint header_lines = app->ui_text_hidden ? 0 : 3;
    gint usable_width = app->term_width > 0 ? app->term_width : 80;
    gint bottom_reserved = app_preview_bottom_reserved_lines(app);
    gint usable_height = app->term_height > header_lines + bottom_reserved
                             ? app->term_height - header_lines - bottom_reserved
                             : 6;

    if (app->book_preview_zoom <= 0) {
        app->book_preview_zoom = 30;
    }

    gint cols = usable_width / app->book_preview_zoom;
    if (cols < 2) cols = 2;
    if (usable_width / cols < 4) {
        cols = usable_width / 4;
        if (cols < 2) cols = 2;
    }

    gint cell_width = usable_width / cols;
    gint cell_height = cell_width / 2 + 1;
    if (cell_height < 4) cell_height = 4;

    gint rows = (app->book_page_count + cols - 1) / cols;
    if (rows < 1) rows = 1;

    gint visible_rows = usable_height / cell_height;
    if (visible_rows < 1) visible_rows = 1;

    layout.cols = cols;
    layout.rows = rows;
    layout.cell_width = cell_width;
    layout.cell_height = cell_height;
    layout.header_lines = header_lines;
    layout.visible_rows = visible_rows;
    return layout;
}

static void app_book_preview_adjust_scroll(PixelTermApp *app, const PreviewLayout *layout) {
    if (!app || !layout) {
        return;
    }

    gint total_rows = layout->rows;
    gint visible_rows = layout->visible_rows;
    if (visible_rows < 1) visible_rows = 1;

    gint max_offset = MAX(0, total_rows - 1);
    if (app->book_preview_scroll > max_offset) {
        app->book_preview_scroll = max_offset;
    }
    if (app->book_preview_scroll < 0) {
        app->book_preview_scroll = 0;
    }

    gint row = app->book_preview_selected / layout->cols;
    if (row < app->book_preview_scroll) {
        app->book_preview_scroll = row;
    } else if (row >= app->book_preview_scroll + visible_rows) {
        app->book_preview_scroll = row - visible_rows + 1;
    }
}

static void app_book_preview_render_page_indicator(PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height < 3) {
        return;
    }
    if (app->book_page_count <= 0) {
        return;
    }

    gint total_pages = app->book_page_count > 0 ? app->book_page_count : 1;
    gint page_display = app->book_preview_selected + 1;
    if (page_display < 1) page_display = 1;
    if (page_display > total_pages) page_display = total_pages;

    char page_text[32];
    g_snprintf(page_text, sizeof(page_text), "%d/%d", page_display, total_pages);
    gint page_len = (gint)strlen(page_text);
    gint page_pad = (app->term_width > page_len) ? (app->term_width - page_len) / 2 : 0;
    printf("\033[3;1H\033[2K");
    for (gint i = 0; i < page_pad; i++) putchar(' ');
    printf("%s", page_text);
}

static void app_book_preview_render_selected_info(PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height < 3) {
        return;
    }
    if (!app->book_path || app->book_page_count <= 0) {
        return;
    }

    gchar *base = g_path_get_basename(app->book_path);
    gchar *safe = sanitize_for_terminal(base);
    gint max_width = app_filename_max_width(app);
    if (max_width <= 0) {
        max_width = app->term_width;
    }

    gchar *display_name = truncate_utf8_middle_keep_suffix(safe, max_width);
    gint row = app->term_height - 2;
    gint name_len = utf8_display_width(display_name);
    gint pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
    printf("\033[%d;1H", row);
    for (gint i = 0; i < app->term_width; i++) putchar(' ');
    if (name_len > 0) {
        printf("\033[%d;%dH\033[34m%s\033[0m", row, pad + 1, display_name);
    }

    g_free(display_name);
    g_free(safe);
    g_free(base);
}

static void app_book_render_page_indicator(const PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height <= 0) {
        return;
    }

    gint current = app->book_page + 1;
    gint total = app->book_page_count;
    if (current < 1) current = 1;
    if (total < 1) total = 1;

    gint idx_row = (app->term_height >= 2) ? (app->term_height - 2) : 1;
    printf("\033[%d;1H\033[2K", idx_row);

    gboolean double_page = app_book_use_double_page(app);
    if (!double_page) {
        char idx_text[32];
        g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
        gint idx_len = (gint)strlen(idx_text);
        gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
        for (gint i = 0; i < idx_pad; i++) putchar(' ');
        printf("%s", idx_text);
        return;
    }

    gint target_width = 0;
    gint target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);
    (void)target_height;
    gint gutter_cols = k_book_spread_gutter_cols;
    gint per_page_cols = (target_width - gutter_cols) / 2;
    if (per_page_cols < 1) {
        char idx_text[32];
        g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
        gint idx_len = (gint)strlen(idx_text);
        gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
        for (gint i = 0; i < idx_pad; i++) putchar(' ');
        printf("%s", idx_text);
        return;
    }

    gint spread_cols = per_page_cols * 2 + gutter_cols;
    gint spread_left_col = 1;
    if (spread_cols > 0 && app->term_width > spread_cols) {
        spread_left_col = (app->term_width - spread_cols) / 2 + 1;
    }
    gint left_half_start = spread_left_col;
    gint right_half_start = spread_left_col + per_page_cols + gutter_cols;

    char left_text[32];
    g_snprintf(left_text, sizeof(left_text), "%d/%d", current, total);
    gint left_len = (gint)strlen(left_text);
    gint left_col = left_half_start;
    if (left_len > 0 && left_len < per_page_cols) {
        left_col += (per_page_cols - left_len) / 2;
    }
    printf("\033[%d;%dH%s", idx_row, left_col, left_text);

    gint right_page = current + 1;
    if (right_page <= total) {
        char right_text[32];
        g_snprintf(right_text, sizeof(right_text), "%d/%d", right_page, total);
        gint right_len = (gint)strlen(right_text);
        gint right_col = right_half_start;
        if (right_len > 0 && right_len < per_page_cols) {
            right_col += (per_page_cols - right_len) / 2;
        }
        printf("\033[%d;%dH%s", idx_row, right_col, right_text);
    }
}

void app_book_jump_render_prompt(PixelTermApp *app) {
    if (!app || !app->book_jump_active) {
        return;
    }
    if (!app->book_mode && !app->book_preview_mode) {
        return;
    }

    gint total_pages = app->book_page_count > 0 ? app->book_page_count : 1;
    gint total_len = 1;
    for (gint tmp = total_pages; tmp >= 10; tmp /= 10) {
        total_len++;
    }
    const char *buf = app->book_jump_buf;
    gint buf_len = app->book_jump_len;
    gint field_width = MIN(total_len, (gint)sizeof(app->book_jump_buf) - 1);
    if (field_width < 1) field_width = 1;

    const char *label = "Jump:";
    const gint label_gap = 1;
    gint label_len = (gint)strlen(label);
    gint layout_width = label_len + label_gap + field_width;

    gint term_h = app->term_height > 0 ? app->term_height : 24;
    gint term_w = app->term_width > 0 ? app->term_width : 80;
    gint input_row = app->book_preview_mode ? term_h - 3 : term_h - 2;
    if (input_row < 1) input_row = 1;

    printf("\033[%d;1H\033[2K", input_row);
    gint input_pad = (term_w > layout_width) ? (term_w - layout_width) / 2 : 0;
    if (input_pad < 0) input_pad = 0;
    for (gint i = 0; i < input_pad; i++) putchar(' ');
    gint base_col = input_pad + 1;
    printf("\033[%d;%dH\033[36m%s\033[0m", input_row, base_col, label);

    gint field_col = base_col + label_len + label_gap;
    char field_buf[32];
    gint field_fill = MIN(field_width, (gint)sizeof(field_buf) - 1);
    for (gint i = 0; i < field_fill; i++) {
        field_buf[i] = ' ';
    }
    if (buf_len > 0) {
        gint print_len = MIN(buf_len, field_fill);
        memcpy(field_buf, buf, print_len);
    } else {
        field_buf[0] = '_';
    }
    field_buf[field_fill] = '\0';
    printf("\033[%d;%dH\033[33m%s\033[0m", input_row, field_col, field_buf);

    gint cursor_col = field_col + (buf_len > 0 ? MIN(buf_len, field_fill) : 0);
    if (cursor_col < 1) cursor_col = 1;
    if (cursor_col > term_w) cursor_col = term_w;
    printf("\033[%d;%dH\033[?25h", input_row, cursor_col);
    fflush(stdout);
}

void app_book_jump_clear_prompt(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (!app->book_mode && !app->book_preview_mode) {
        return;
    }

    gint term_h = app->term_height > 0 ? app->term_height : 24;
    gint input_row = app->book_preview_mode ? term_h - 3 : term_h - 2;
    if (input_row < 1) input_row = 1;
    printf("\033[%d;1H\033[2K", input_row);
    printf("\033[?25l");

    if (app->book_preview_mode) {
        app_book_preview_render_selected_info(app);
        app_book_preview_render_page_indicator(app);
    } else if (app->book_mode && !app->ui_text_hidden) {
        app_book_render_page_indicator(app);
    }

    fflush(stdout);
}

static gboolean app_book_preview_get_cell_origin(const PixelTermApp *app,
                                                 const PreviewLayout *layout,
                                                 gint index,
                                                 gint start_row,
                                                 gint vertical_offset,
                                                 gint *cell_x,
                                                 gint *cell_y) {
    if (!app || !layout || !cell_x || !cell_y) {
        return FALSE;
    }
    if (index < 0 || index >= app->book_page_count) {
        return FALSE;
    }
    gint row = index / layout->cols;
    gint col = index % layout->cols;
    if (row < start_row || row >= start_row + layout->visible_rows) {
        return FALSE;
    }
    *cell_x = col * layout->cell_width + 1;
    *cell_y = layout->header_lines + vertical_offset + (row - start_row) * layout->cell_height + 1;
    return TRUE;
}

static void app_book_preview_clear_cell_border(const PixelTermApp *app,
                                               const PreviewLayout *layout,
                                               gint index,
                                               gint start_row,
                                               gint vertical_offset) {
    if (!app || !layout) {
        return;
    }
    if (layout->cell_width < 4 || layout->cell_height < 4) {
        return;
    }
    gint cell_x = 0;
    gint cell_y = 0;
    if (!app_book_preview_get_cell_origin(app, layout, index, start_row, vertical_offset, &cell_x, &cell_y)) {
        return;
    }

    printf("\033[0m");
    printf("\033[%d;%dH", cell_y, cell_x);
    for (gint c = 0; c < layout->cell_width; c++) putchar(' ');

    gint bottom_y = cell_y + layout->cell_height - 1;
    printf("\033[%d;%dH", bottom_y, cell_x);
    for (gint c = 0; c < layout->cell_width; c++) putchar(' ');

    for (gint line = 1; line < layout->cell_height - 1; line++) {
        gint y = cell_y + line;
        printf("\033[%d;%dH ", y, cell_x);
        printf("\033[%d;%dH ", y, cell_x + layout->cell_width - 1);
    }
}

static void app_book_preview_draw_cell_border(const PixelTermApp *app,
                                              const PreviewLayout *layout,
                                              gint index,
                                              gint start_row,
                                              gint vertical_offset) {
    if (!app || !layout) {
        return;
    }
    if (layout->cell_width < 4 || layout->cell_height < 4) {
        return;
    }
    gint cell_x = 0;
    gint cell_y = 0;
    if (!app_book_preview_get_cell_origin(app, layout, index, start_row, vertical_offset, &cell_x, &cell_y)) {
        return;
    }

    const char *border_style = "\033[34;1m";
    printf("\033[%d;%dH%s+", cell_y, cell_x, border_style);
    for (gint c = 0; c < layout->cell_width - 2; c++) putchar('-');
    printf("+\033[0m");

    for (gint line = 1; line < layout->cell_height - 1; line++) {
        gint y = cell_y + line;
        printf("\033[%d;%dH%s|\033[0m", y, cell_x, border_style);
        printf("\033[%d;%dH%s|\033[0m", y, cell_x + layout->cell_width - 1, border_style);
    }

    gint bottom_y = cell_y + layout->cell_height - 1;
    printf("\033[%d;%dH%s+", bottom_y, cell_x, border_style);
    for (gint c = 0; c < layout->cell_width - 2; c++) putchar('-');
    printf("+\033[0m");
}

ErrorCode app_book_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col) {
    if (!app || !app->book_preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (app->book_page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint cols = layout.cols;
    gint rows = layout.rows;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    gint old_scroll = app->book_preview_scroll;

    gint row = app->book_preview_selected / cols;
    gint col = app->book_preview_selected % cols;

    row += delta_row;
    col += delta_col;

    if (delta_col < 0 && col < 0) {
        col = cols - 1;
    } else if (delta_col > 0 && col >= cols) {
        col = 0;
    }

    if (delta_row > 0 && row >= rows) {
        row = 0;
        app->book_preview_scroll = 0;
    } else if (delta_row < 0 && row < 0) {
        gint visible_rows = layout.visible_rows > 0 ? layout.visible_rows : 1;
        gint last_page_scroll = 0;
        if (rows > 0) {
            last_page_scroll = ((rows - 1) / visible_rows) * visible_rows;
            if (last_page_scroll < 0) {
                last_page_scroll = 0;
            } else if (last_page_scroll > rows - 1) {
                last_page_scroll = rows - 1;
            }
        }
        row = rows - 1;
        app->book_preview_scroll = last_page_scroll;
    } else if (delta_row > 0 && row >= app->book_preview_scroll + layout.visible_rows) {
        gint new_scroll = MIN(app->book_preview_scroll + layout.visible_rows, MAX(rows - 1, 0));
        app->book_preview_scroll = new_scroll;
        row = new_scroll;
    } else if (delta_row < 0 && row < app->book_preview_scroll) {
        gint new_scroll = MAX(app->book_preview_scroll - layout.visible_rows, 0);
        app->book_preview_scroll = new_scroll;
        row = MIN(new_scroll + layout.visible_rows - 1, rows - 1);
    }

    if (row < 0) row = 0;
    if (row >= rows) row = rows - 1;
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;

    gint new_index = row * cols + col;
    gint row_start = row * cols;
    gint row_end = MIN(app->book_page_count - 1, row_start + cols - 1);
    if (new_index < row_start) new_index = row_start;
    if (new_index > row_end) new_index = row_end;
    if (new_index >= app->book_page_count) {
        new_index = app->book_page_count - 1;
    }

    app->book_preview_selected = new_index;
    app_book_preview_adjust_scroll(app, &layout);
    if (app->book_preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

ErrorCode app_book_preview_page_move(PixelTermApp *app, gint direction) {
    if (!app || !app->book_preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (app->book_page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;
    gint total_pages = (layout.rows + rows_per_page - 1) / rows_per_page;
    if (total_pages <= 1) {
        return ERROR_NONE;
    }
    gint cols = layout.cols;
    if (cols < 1) cols = 1;
    gint rows = layout.rows;
    gint old_scroll = app->book_preview_scroll;

    gint current_row = cols > 0 ? app->book_preview_selected / cols : 0;
    gint current_col = cols > 0 ? app->book_preview_selected % cols : 0;
    gint relative_row = current_row - app->book_preview_scroll;
    if (relative_row < 0) relative_row = 0;
    if (relative_row >= rows_per_page) relative_row = rows_per_page - 1;

    gint delta_scroll = direction >= 0 ? rows_per_page : -rows_per_page;
    gint new_scroll = app->book_preview_scroll + delta_scroll;
    gint last_page_scroll = ((rows - 1) / rows_per_page) * rows_per_page;
    if (last_page_scroll < 0) last_page_scroll = 0;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > last_page_scroll) new_scroll = last_page_scroll;

    gint new_row = new_scroll + relative_row;
    if (new_row < 0) new_row = 0;
    if (new_row >= rows) new_row = rows - 1;

    if (current_col < 0) current_col = 0;
    if (current_col >= cols) current_col = cols - 1;

    gint new_index = new_row * cols + current_col;
    gint row_start = new_row * cols;
    gint row_end = MIN(app->book_page_count - 1, row_start + cols - 1);
    if (new_index < row_start) new_index = row_start;
    if (new_index > row_end) new_index = row_end;
    if (new_index >= app->book_page_count) {
        new_index = app->book_page_count - 1;
    }

    app->book_preview_scroll = new_scroll;
    app->book_preview_selected = new_index;

    if (app->book_preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

ErrorCode app_book_preview_jump_to_page(PixelTermApp *app, gint page_index) {
    if (!app || !app->book_preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (app->book_page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    if (page_index < 0) page_index = 0;
    if (page_index >= app->book_page_count) page_index = app->book_page_count - 1;

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint cols = layout.cols > 0 ? layout.cols : 1;
    gint rows = layout.rows > 0 ? layout.rows : 1;
    gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;

    gint row = page_index / cols;
    gint new_scroll = (rows_per_page > 0) ? (row / rows_per_page) * rows_per_page : row;
    gint last_page_scroll = 0;
    if (rows > 0 && rows_per_page > 0) {
        last_page_scroll = ((rows - 1) / rows_per_page) * rows_per_page;
    }
    if (new_scroll > last_page_scroll) new_scroll = last_page_scroll;
    if (new_scroll < 0) new_scroll = 0;

    gint old_scroll = app->book_preview_scroll;
    app->book_preview_selected = page_index;
    app->book_preview_scroll = new_scroll;
    if (app->book_preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

ErrorCode app_book_preview_scroll_pages(PixelTermApp *app, gint direction) {
    if (!app || !app->book_preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (app->book_page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint visible_rows = layout.visible_rows;
    if (visible_rows < 1) visible_rows = 1;
    gint total_rows = layout.rows;
    if (total_rows <= visible_rows) {
        return ERROR_NONE;
    }

    gint delta = direction > 0 ? visible_rows : -visible_rows;
    gint new_scroll = app->book_preview_scroll + delta;
    if (new_scroll < 0) new_scroll = 0;
    gint max_scroll = MAX(0, total_rows - visible_rows);
    if (new_scroll > max_scroll) new_scroll = max_scroll;

    if (new_scroll == app->book_preview_scroll) {
        return ERROR_NONE;
    }

    app->book_preview_scroll = new_scroll;
    return ERROR_NONE;
}

ErrorCode app_book_preview_change_zoom(PixelTermApp *app, gint delta) {
    if (!app || !app->book_preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (app->term_width <= 0) {
        return ERROR_NONE;
    }

    gint usable_width = app->term_width;
    if (app->book_preview_zoom <= 0) {
        app->book_preview_zoom = usable_width / 4;
    }

    gint current_cols = (gint)(usable_width / app->book_preview_zoom + 0.5f);
    if (current_cols < 2) current_cols = 2;
    gint new_cols = current_cols - delta;
    if (new_cols < 2) new_cols = 2;
    if (new_cols > usable_width) new_cols = usable_width;

    app->book_preview_zoom = (gdouble)usable_width / new_cols;
    if (app->book_preview_zoom < 1) app->book_preview_zoom = 1;

    app->needs_screen_clear = TRUE;
    return ERROR_NONE;
}

// Handle mouse click in preview grid mode
ErrorCode app_handle_mouse_click_preview(PixelTermApp *app,
                                         gint mouse_x,
                                         gint mouse_y,
                                         gboolean *redraw_needed,
                                         gboolean *out_hit) {
    if (!app || !app->preview_mode) {
        if (redraw_needed) *redraw_needed = FALSE;
        if (out_hit) *out_hit = FALSE;
        return ERROR_MEMORY_ALLOC;
    }
    if (redraw_needed) *redraw_needed = FALSE; // Default to no redraw
    if (out_hit) *out_hit = FALSE; // Default to no hit

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint start_row = app->preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);
    gint grid_top_y = layout.header_lines + 1 + vertical_offset;

    // Check if click is in header area
    if (mouse_y < grid_top_y) {
        return ERROR_NONE; // Ignore clicks in header
    }

    // Calculate clicked cell position
    gint col = (mouse_x - 1) / layout.cell_width;
    gint row_in_visible = (mouse_y - grid_top_y) / layout.cell_height;
    gint absolute_row = start_row + row_in_visible;

    // Check bounds
    gint rows_drawn = MAX(0, end_row - start_row);
    if (col < 0 || col >= layout.cols || row_in_visible < 0 || row_in_visible >= rows_drawn) {
        return ERROR_NONE; // Out of bounds
    }

    // Calculate image index
    gint index = absolute_row * layout.cols + col;

    // Check if index is valid
    if (index >= 0 && index < app->total_images) {
        if (out_hit) *out_hit = TRUE;
        if (app->preview_selected != index) { // Check if selection actually changed
            app->preview_selected = index;
            app->current_index = index; // Also update current index for consistency
            if (redraw_needed) *redraw_needed = TRUE;
        }
    }

    return ERROR_NONE;
}

ErrorCode app_handle_mouse_click_book_preview(PixelTermApp *app,
                                              gint mouse_x,
                                              gint mouse_y,
                                              gboolean *redraw_needed,
                                              gboolean *out_hit) {
    if (!app || !app->book_preview_mode) {
        if (redraw_needed) *redraw_needed = FALSE;
        if (out_hit) *out_hit = FALSE;
        return ERROR_MEMORY_ALLOC;
    }
    if (redraw_needed) *redraw_needed = FALSE;
    if (out_hit) *out_hit = FALSE;

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint start_row = app->book_preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);
    gint grid_top_y = layout.header_lines + 1 + vertical_offset;

    if (mouse_y < grid_top_y) {
        return ERROR_NONE;
    }

    gint col = (mouse_x - 1) / layout.cell_width;
    gint row_in_visible = (mouse_y - grid_top_y) / layout.cell_height;
    gint absolute_row = start_row + row_in_visible;

    gint rows_drawn = MAX(0, end_row - start_row);
    if (col < 0 || col >= layout.cols || row_in_visible < 0 || row_in_visible >= rows_drawn) {
        return ERROR_NONE;
    }

    gint index = absolute_row * layout.cols + col;
    if (index >= 0 && index < app->book_page_count) {
        if (out_hit) *out_hit = TRUE;
        if (app->book_preview_selected != index) {
            app->book_preview_selected = index;
            if (redraw_needed) *redraw_needed = TRUE;
        }
    }

    return ERROR_NONE;
}

// Enter preview grid mode
ErrorCode app_enter_preview(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Filter out invalid images before entering preview mode
    GList *valid_images = NULL;
    gint valid_count = 0;
    gint new_current_index = app->current_index;
    gint valid_current_index = -1;
    
    GList *current = app->image_files;
    gint original_index = 0;
    while (current) {
        const gchar *filepath = (const gchar*)current->data;
        if (is_valid_media_file(filepath)) {
            valid_images = g_list_append(valid_images, g_strdup(filepath));
            if (original_index == app->current_index) {
                valid_current_index = valid_count;
            }
            valid_count++;
        }
        current = g_list_next(current);
        original_index++;
    }
    
    // If we found valid images, replace the image list
    if (valid_images && valid_count > 0) {
        // Free the old image list
        if (app->image_files) {
            g_list_free_full(app->image_files, (GDestroyNotify)g_free);
        }
        
        app->image_files = valid_images;
        app->total_images = valid_count;
        
        // Update current index to the valid image that was selected
        if (valid_current_index >= 0) {
            app->current_index = valid_current_index;
        } else if (app->current_index >= app->total_images) {
            app->current_index = 0; // fallback to first image
        }
    } else {
        // If no valid images remain, return an error
        g_list_free_full(valid_images, (GDestroyNotify)g_free);
        return ERROR_INVALID_IMAGE;
    }

    // Stop GIF playback if active
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player) {
        video_player_stop(app->video_player);
    }

    app->preview_mode = TRUE;
    app->file_manager_mode = FALSE; // ensure we are not in file manager
    app->preview_selected = app->current_index >= 0 ? app->current_index : 0;

    // For yellow border mode (RETURN_MODE_PREVIEW_VIRTUAL), always select first image
    if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
        app->preview_selected = 0;
    }

    app->info_visible = FALSE;
    app->needs_redraw = TRUE;
    
    // Clear screen on mode entry to avoid ghosting
    app_clear_screen_for_refresh(app);
    fflush(stdout);

    if (app->preloader && app->preload_enabled) {
        preloader_clear_queue(app->preloader);
    }
    return ERROR_NONE;
}

ErrorCode app_enter_book_preview(PixelTermApp *app) {
    if (!app || !app->book_doc) {
        return ERROR_INVALID_IMAGE;
    }

    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player) {
        video_player_stop(app->video_player);
    }

    app->book_preview_mode = TRUE;
    app->book_mode = FALSE;
    app->preview_mode = FALSE;
    app->file_manager_mode = FALSE;
    app->book_preview_selected = app->book_page >= 0 ? app->book_page : 0;
    if (app->book_preview_selected >= app->book_page_count) {
        app->book_preview_selected = MAX(0, app->book_page_count - 1);
    }
    app->book_preview_scroll = 0;
    app->info_visible = FALSE;
    app->needs_screen_clear = TRUE;

    if (app->preloader && app->preload_enabled) {
        preloader_clear_queue(app->preloader);
    }
    return ERROR_NONE;
}

ErrorCode app_enter_book_page(PixelTermApp *app, gint page_index) {
    if (!app || !app->book_doc) {
        return ERROR_INVALID_IMAGE;
    }

    if (page_index < 0) page_index = 0;
    if (page_index >= app->book_page_count) {
        page_index = MAX(0, app->book_page_count - 1);
    }

    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player) {
        video_player_stop(app->video_player);
    }

    app->book_page = page_index;
    app->book_mode = TRUE;
    app->book_preview_mode = FALSE;
    app->preview_mode = FALSE;
    app->file_manager_mode = FALSE;
    app->info_visible = FALSE;
    app->needs_redraw = TRUE;

    if (app->preloader && app->preload_enabled) {
        preloader_clear_queue(app->preloader);
    }
    return ERROR_NONE;
}

// Exit preview grid mode
ErrorCode app_exit_preview(PixelTermApp *app, gboolean open_selected) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app->preview_mode) {
        return ERROR_NONE;
    }

    if (open_selected && app_has_images(app)) {
        if (app->preview_selected >= 0 && app->preview_selected < app->total_images) {
            app->current_index = app->preview_selected;
        }
        app->image_zoom = 1.0;
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;
    }

    app->preview_mode = FALSE;
    app->info_visible = FALSE;
    app->needs_redraw = TRUE;
    if (app->preloader && app->preload_enabled && app_has_images(app)) {
        gint target_width = 0, target_height = 0;
        app_get_image_target_dimensions(app, &target_width, &target_height);
        preloader_clear_queue(app->preloader);
        preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
    }
    return ERROR_NONE;
}

// Render preview grid of images
ErrorCode app_render_preview_grid(PixelTermApp *app) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Update terminal dimensions and force full clear if size changed
    gint prev_width = app->term_width;
    gint prev_height = app->term_height;
    get_terminal_size(&app->term_width, &app->term_height);
    if ((prev_width > 0 && prev_width != app->term_width) ||
        (prev_height > 0 && prev_height != app->term_height)) {
        app->needs_screen_clear = TRUE;
    }

    PreviewLayout layout = app_preview_calculate_layout(app);
    app_preview_adjust_scroll(app, &layout);
    app_preview_queue_preloads(app, &layout);

    if (app->needs_screen_clear) {
        // Inside preview mode, prefer a normal clear to avoid extra terminal work.
        printf("\033[2J\033[H\033[0m"); // Clear screen and move cursor to top-left
        app->needs_screen_clear = FALSE;
    } else {
        printf("\033[H\033[0m"); // Move cursor to top-left (don't clear screen to avoid flicker)
    }

    // Renderer reused for all cells to avoid repeated init/decode overhead
    gint content_width = layout.cell_width - 2;
    gint content_height = layout.cell_height - 2;
    if (content_width < 1) content_width = 1;
    if (content_height < 1) content_height = 1;
    RendererConfig config = {
        .max_width = MAX(2, content_width),
        .max_height = MAX(2, content_height),
        .preserve_aspect_ratio = TRUE,
        .dither = app->dither_enabled,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = app->render_work_factor,
        .force_text = app->force_text,
        .force_sixel = app->force_sixel,
        .force_kitty = app->force_kitty,
        .force_iterm2 = app->force_iterm2,
        .gamma = app->gamma,
        .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };
    ImageRenderer *renderer = renderer_create();
    if (!renderer) {
        return ERROR_MEMORY_ALLOC;
    }
    if (renderer_initialize(renderer, &config) != ERROR_NONE) {
        renderer_destroy(renderer);
        return ERROR_CHAFA_INIT;
    }

    if (!app->ui_text_hidden) {
        // Header: title + page indicator on row 2; keep 3 header lines total
        const char *title = "Preview Grid";
        gint title_len = strlen(title);
        gint pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
        printf("\033[1;1H\033[2K");
        for (gint i = 0; i < pad; i++) putchar(' ');
        printf("%s", title);

        // Row 3: Page indicator centered (numbers only)
        gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;
        gint total_pages = (layout.rows + rows_per_page - 1) / rows_per_page;
        if (total_pages < 1) total_pages = 1;
        gint current_page = (app->preview_scroll + rows_per_page - 1) / rows_per_page + 1;
        if (current_page < 1) current_page = 1;
        if (current_page > total_pages) current_page = total_pages;
        char page_text[32];
        g_snprintf(page_text, sizeof(page_text), "%d/%d", current_page, total_pages);
        gint page_len = (gint)strlen(page_text);
        gint page_pad = (app->term_width > page_len) ? (app->term_width - page_len) / 2 : 0;
        printf("\033[3;1H\033[2K");
        for (gint i = 0; i < page_pad; i++) putchar(' ');
        printf("%s", page_text);

        // Row 2: spacer
        printf("\033[2;1H\033[2K");
    }

    gint start_row = app->preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);
    gint start_index = start_row * layout.cols;
    GList *cursor = g_list_nth(app->image_files, start_index);

    for (gint row = start_row; row < end_row; row++) {
        for (gint col = 0; col < layout.cols; col++) {
            gint idx = row * layout.cols + col;
            if (idx >= app->total_images || !cursor) {
                // Skip remaining columns in this row if we've run out of images
                break;
            }

            gint cell_x = col * layout.cell_width + 1;
            gint cell_y = layout.header_lines + vertical_offset + (row - start_row) * layout.cell_height + 1;

            const gchar *filepath = (const gchar*)cursor->data;
            cursor = cursor->next;
            gboolean is_video = is_video_file(filepath);
            if (!is_video && !is_image_file(filepath)) {
                is_video = is_valid_video_file(filepath);
            }
            gboolean selected = (idx == app->preview_selected);
            gboolean use_border = selected &&
                                  layout.cell_width >= 4 &&
                                  layout.cell_height >= 4;

            // Keep content area constant so selection doesn't change available image space
            gint content_x = cell_x + 1;
            gint content_y = cell_y + 1;

            // Clear cell and draw border without occupying content area
            const char *border_style =
                (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) ? "\033[33;1m" : "\033[34;1m";
            for (gint line = 0; line < layout.cell_height; line++) {
                gint y = cell_y + line;
                printf("\033[%d;%dH", y, cell_x);
                for (gint c = 0; c < layout.cell_width; c++) {
                    putchar(' ');
                }

                if (use_border) {
                    if (line == 0 || line == layout.cell_height - 1) {
                        printf("\033[%d;%dH%s+", y, cell_x, border_style);
                        for (gint c = 0; c < layout.cell_width - 2; c++) putchar('-');
                        printf("+\033[0m");
                    } else {
                        printf("\033[%d;%dH%s|\033[0m", y, cell_x, border_style);
                        printf("\033[%d;%dH%s|\033[0m", y, cell_x + layout.cell_width - 1, border_style);
                    }
                }
            }

            gboolean rendered_from_preload = FALSE;
            gboolean rendered_from_renderer_cache = FALSE;

            GString *rendered = NULL;
            if (app->preloader && app->preload_enabled) {
                rendered = preloader_get_cached_image(app->preloader, filepath, content_width, content_height);
                rendered_from_preload = (rendered != NULL);
            }
            if (!rendered) {
                if (is_video) {
                    guint8 *frame_pixels = NULL;
                    gint frame_width = 0;
                    gint frame_height = 0;
                    gint frame_rowstride = 0;
                    if (video_player_get_first_frame(filepath,
                                                     &frame_pixels,
                                                     &frame_width,
                                                     &frame_height,
                                                     &frame_rowstride) == ERROR_NONE) {
                        rendered = renderer_render_image_data(renderer,
                                                              frame_pixels,
                                                              frame_width,
                                                              frame_height,
                                                              frame_rowstride,
                                                              4);
                    }
                    g_free(frame_pixels);
                } else {
                    rendered = renderer_render_image_file(renderer, filepath);
                    if (rendered) {
                        GString *cached_entry = renderer_cache_get(renderer, filepath);
                        rendered_from_renderer_cache = (cached_entry == rendered);
                    }
                }
            }
            if (!rendered) {
                if (is_video) {
                    const char *label = "VIDEO";
                    gint label_len = (gint)strlen(label);
                    gint label_row = content_y + content_height / 2;
                    gint label_col = content_x + (content_width - label_len) / 2;
                    if (label_row < content_y) label_row = content_y;
                    if (label_col < content_x) label_col = content_x;
                    printf("\033[%d;%dH\033[35m%s\033[0m", label_row, label_col, label);
                }
                continue;
            }

            if (!rendered_from_preload && app->preloader && app->preload_enabled) {
                gint rendered_w = 0, rendered_h = 0;
                renderer_get_rendered_dimensions(renderer, &rendered_w, &rendered_h);
                preloader_cache_add(app->preloader, filepath, rendered, rendered_w, rendered_h, content_width, content_height);
            }

            // Draw image lines within the cell bounds, horizontally centered
            gint line_no = 0;
            char *cursor = rendered->str;
            while (cursor && line_no < content_height) {
                char *newline = strchr(cursor, '\n');
                gint line_len = newline ? (gint)(newline - cursor) : (gint)strlen(cursor);

                gint visible_len = app_preview_visible_width(cursor, line_len);
                gint pad_left = 0;
                if (content_width > visible_len) {
                    pad_left = (content_width - visible_len) / 2;
                }

                printf("\033[%d;%dH", content_y + line_no, content_x + pad_left);
                fwrite(cursor, 1, line_len, stdout);

                if (!newline) {
                    break;
                }
                cursor = newline + 1;
                line_no++;
            }

            // Ensure attributes reset after each cell
            printf("\033[0m");

            // Free when we own the buffer (renderer output or preloader copy).
            if (!rendered_from_renderer_cache) {
                g_string_free(rendered, TRUE);
            }
        }
    }

    app_preview_render_selected_filename(app);

    // Footer hints centered on last line
    if (app->term_height > 0 && !app->ui_text_hidden) {
        const HelpSegment segments[] = {
            {"←/→/↑/↓", "Move"},
            {"PgUp/PgDn", "Page"},
            {"Enter", "Open"},
            {"TAB", "Toggle"},
            {"r", "Delete"},
            {"+/-", "Zoom"},
            {"~", "Zen"},
            {"ESC", "Exit"}
        };
        print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
    }

    fflush(stdout);
    renderer_destroy(renderer);
    return ERROR_NONE;
}

ErrorCode app_render_preview_selection_change(PixelTermApp *app, gint old_index) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    gint old_scroll = app->preview_scroll;
    PreviewLayout layout = app_preview_calculate_layout(app);
    app_preview_adjust_scroll(app, &layout);
    if (app->preview_scroll != old_scroll) {
        return app_render_preview_grid(app);
    }

    gint selected_row = app->preview_selected / layout.cols;
    if (selected_row < app->preview_scroll ||
        selected_row >= app->preview_scroll + layout.visible_rows) {
        return app_render_preview_grid(app);
    }

    app_preview_queue_preloads(app, &layout);

    gint start_row = app->preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);

    if (old_index != app->preview_selected) {
        app_preview_clear_cell_border(app, &layout, old_index, start_row, vertical_offset);
    }
    app_preview_draw_cell_border(app, &layout, app->preview_selected, start_row, vertical_offset);
    app_preview_render_selected_filename(app);

    app_end_sync_update();
    fflush(stdout);
    return ERROR_NONE;
}

ErrorCode app_render_book_preview(PixelTermApp *app) {
    if (!app || !app->book_preview_mode || !app->book_doc) {
        return ERROR_MEMORY_ALLOC;
    }
    if (app->book_page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    gint prev_width = app->term_width;
    gint prev_height = app->term_height;
    get_terminal_size(&app->term_width, &app->term_height);
    if ((prev_width > 0 && prev_width != app->term_width) ||
        (prev_height > 0 && prev_height != app->term_height)) {
        app->needs_screen_clear = TRUE;
    }

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    app_book_preview_adjust_scroll(app, &layout);

    if (app->suppress_full_clear) {
        app->suppress_full_clear = FALSE;
        printf("\033[H\033[0m");
        if (app->ui_text_hidden) {
            app_clear_single_view_ui_lines(app);
        }
        app->needs_screen_clear = FALSE;
    } else if (app->needs_screen_clear) {
        printf("\033[2J\033[H\033[0m");
        app->needs_screen_clear = FALSE;
    } else {
        printf("\033[H\033[0m");
    }

    if (!app->ui_text_hidden) {
        const char *title = "Book Preview";
        gint title_len = strlen(title);
        gint pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
        printf("\033[1;1H\033[2K");
        for (gint i = 0; i < pad; i++) putchar(' ');
        printf("%s", title);

        printf("\033[2;1H\033[2K");
        app_book_preview_render_page_indicator(app);
    }

    gint content_width = layout.cell_width - 2;
    gint content_height = layout.cell_height - 2;
    if (content_width < 1) content_width = 1;
    if (content_height < 1) content_height = 1;
    RendererConfig config = {
        .max_width = MAX(2, content_width),
        .max_height = MAX(2, content_height),
        .preserve_aspect_ratio = TRUE,
        .dither = app->dither_enabled,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = app->render_work_factor,
        .force_text = app->force_text,
        .force_sixel = app->force_sixel,
        .force_kitty = app->force_kitty,
        .force_iterm2 = app->force_iterm2,
        .gamma = app->gamma,
        .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };
    ImageRenderer *renderer = renderer_create();
    if (!renderer) {
        return ERROR_MEMORY_ALLOC;
    }
    if (renderer_initialize(renderer, &config) != ERROR_NONE) {
        renderer_destroy(renderer);
        return ERROR_CHAFA_INIT;
    }

    gint start_row = app->book_preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);

    for (gint row = start_row; row < end_row; row++) {
        for (gint col = 0; col < layout.cols; col++) {
            gint idx = row * layout.cols + col;
            if (idx >= app->book_page_count) {
                break;
            }

            gint cell_x = col * layout.cell_width + 1;
            gint cell_y = layout.header_lines + vertical_offset + (row - start_row) * layout.cell_height + 1;
            gboolean selected = (idx == app->book_preview_selected);
            gboolean use_border = selected &&
                                  layout.cell_width >= 4 &&
                                  layout.cell_height >= 4;

            gint content_x = cell_x + 1;
            gint content_y = cell_y + 1;

            for (gint line = 0; line < layout.cell_height; line++) {
                gint y = cell_y + line;
                printf("\033[%d;%dH", y, cell_x);
                for (gint c = 0; c < layout.cell_width; c++) {
                    putchar(' ');
                }

                if (use_border) {
                    if (line == 0 || line == layout.cell_height - 1) {
                        printf("\033[%d;%dH\033[34;1m+", y, cell_x);
                        for (gint c = 0; c < layout.cell_width - 2; c++) putchar('-');
                        printf("+\033[0m");
                    } else {
                        printf("\033[%d;%dH\033[34;1m|\033[0m", y, cell_x);
                        printf("\033[%d;%dH\033[34;1m|\033[0m", y, cell_x + layout.cell_width - 1);
                    }
                }
            }

            BookPageImage page_image;
            ErrorCode page_err = book_render_page(app->book_doc, idx, content_width, content_height, &page_image);
            if (page_err != ERROR_NONE) {
                const char *label = "PAGE";
                gint label_len = (gint)strlen(label);
                gint label_row = content_y + content_height / 2;
                gint label_col = content_x + (content_width - label_len) / 2;
                if (label_row < content_y) label_row = content_y;
                if (label_col < content_x) label_col = content_x;
                printf("\033[%d;%dH\033[33m%s\033[0m", label_row, label_col, label);
                continue;
            }

            GString *rendered = renderer_render_image_data(renderer,
                                                           page_image.pixels,
                                                           page_image.width,
                                                           page_image.height,
                                                           page_image.stride,
                                                           page_image.channels);
            book_page_image_free(&page_image);

            if (!rendered) {
                const char *label = "PAGE";
                gint label_len = (gint)strlen(label);
                gint label_row = content_y + content_height / 2;
                gint label_col = content_x + (content_width - label_len) / 2;
                if (label_row < content_y) label_row = content_y;
                if (label_col < content_x) label_col = content_x;
                printf("\033[%d;%dH\033[33m%s\033[0m", label_row, label_col, label);
                continue;
            }

            gint line_no = 0;
            char *cursor = rendered->str;
            while (cursor && line_no < content_height) {
                char *newline = strchr(cursor, '\n');
                gint line_len = newline ? (gint)(newline - cursor) : (gint)strlen(cursor);

                gint visible_len = app_preview_visible_width(cursor, line_len);
                gint pad_left = 0;
                if (content_width > visible_len) {
                    pad_left = (content_width - visible_len) / 2;
                }

                printf("\033[%d;%dH", content_y + line_no, content_x + pad_left);
                fwrite(cursor, 1, line_len, stdout);

                if (!newline) {
                    break;
                }
                cursor = newline + 1;
                line_no++;
            }
            printf("\033[0m");
            g_string_free(rendered, TRUE);
        }
    }

    app_book_preview_render_selected_info(app);
    if (app->book_jump_active) {
        app_book_jump_render_prompt(app);
    }

    if (app->term_height > 0 && !app->ui_text_hidden) {
        const HelpSegment segments[] = {
            {"←/→/↑/↓", "Move"},
            {"PgUp/PgDn", "Page"},
            {"P", "Page"},
            {"Enter", "Open"},
            {"TAB", "Toggle"},
            {"+/-", "Zoom"},
            {"~", "Zen"},
            {"ESC", "Exit"}
        };
        print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
    }

    fflush(stdout);
    renderer_destroy(renderer);
    return ERROR_NONE;
}

ErrorCode app_render_book_preview_selection_change(PixelTermApp *app, gint old_index) {
    if (!app || !app->book_preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (app->book_page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    gint old_scroll = app->book_preview_scroll;
    PreviewLayout layout = app_book_preview_calculate_layout(app);
    app_book_preview_adjust_scroll(app, &layout);
    if (app->book_preview_scroll != old_scroll) {
        return app_render_book_preview(app);
    }

    gint selected_row = app->book_preview_selected / layout.cols;
    if (selected_row < app->book_preview_scroll ||
        selected_row >= app->book_preview_scroll + layout.visible_rows) {
        return app_render_book_preview(app);
    }

    gint start_row = app->book_preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);

    if (old_index != app->book_preview_selected) {
        app_book_preview_clear_cell_border(app, &layout, old_index, start_row, vertical_offset);
    }
    app_book_preview_draw_cell_border(app, &layout, app->book_preview_selected, start_row, vertical_offset);
    app_book_preview_render_page_indicator(app);
    app_book_preview_render_selected_info(app);
    if (app->book_jump_active) {
        app_book_jump_render_prompt(app);
    }

    fflush(stdout);
    return ERROR_NONE;
}

ErrorCode app_render_book_page(PixelTermApp *app) {
    if (!app || !app->book_mode || !app->book_doc) {
        return ERROR_MEMORY_ALLOC;
    }
    if (app->book_page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    gint target_width = 0;
    gint target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);

    BookPageImage base_image = {0};

    gboolean double_page = app_book_use_double_page(app);
    if (double_page) {
        gint gutter_cols = k_book_spread_gutter_cols;
        gint per_page_cols = (target_width - gutter_cols) / 2;
        if (per_page_cols < 1) {
            double_page = FALSE;
        } else {
            gint per_page_rows = target_height;
            if (per_page_rows < 1) per_page_rows = 1;

            BookPageImage left_image = {0};
            ErrorCode left_err = book_render_page(app->book_doc, app->book_page, per_page_cols, per_page_rows, &left_image);
            if (left_err != ERROR_NONE) {
                return left_err;
            }

            BookPageImage right_image = {0};
            gboolean has_right = (app->book_page + 1 < app->book_page_count);
            if (has_right) {
                ErrorCode right_err = book_render_page(app->book_doc, app->book_page + 1, per_page_cols, per_page_rows, &right_image);
                if (right_err != ERROR_NONE) {
                    has_right = FALSE;
                }
            }

            ImageRenderer *renderer = renderer_create();
            if (!renderer) {
                book_page_image_free(&left_image);
                book_page_image_free(&right_image);
                return ERROR_MEMORY_ALLOC;
            }

            RendererConfig config = {
                .max_width = per_page_cols,
                .max_height = target_height,
                .preserve_aspect_ratio = TRUE,
                .dither = app->dither_enabled,
                .color_space = CHAFA_COLOR_SPACE_RGB,
                .work_factor = app->render_work_factor,
                .force_text = app->force_text,
                .force_sixel = app->force_sixel,
                .force_kitty = app->force_kitty,
                .force_iterm2 = app->force_iterm2,
                .gamma = app->gamma,
                .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
                .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
                .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
            };

            ErrorCode error = renderer_initialize(renderer, &config);
            if (error != ERROR_NONE) {
                renderer_destroy(renderer);
                book_page_image_free(&left_image);
                book_page_image_free(&right_image);
                return error;
            }

            GString *left_rendered = renderer_render_image_data(renderer,
                                                                left_image.pixels,
                                                                left_image.width,
                                                                left_image.height,
                                                                left_image.stride,
                                                                left_image.channels);
            if (!left_rendered) {
                renderer_destroy(renderer);
                book_page_image_free(&left_image);
                book_page_image_free(&right_image);
                return ERROR_INVALID_IMAGE;
            }

            gint left_width = 0;
            gint left_height = 0;
            renderer_get_rendered_dimensions(renderer, &left_width, &left_height);
            if (left_height <= 0) {
                left_height = app_count_rendered_lines(left_rendered);
            }
            if (left_height <= 0) {
                left_height = 1;
            }
            if (left_width <= 0) {
                left_width = per_page_cols;
            }

            GString *right_rendered = NULL;
            gint right_width = 0;
            gint right_height = 0;
            if (has_right && right_image.pixels) {
                right_rendered = renderer_render_image_data(renderer,
                                                            right_image.pixels,
                                                            right_image.width,
                                                            right_image.height,
                                                            right_image.stride,
                                                            right_image.channels);
                if (right_rendered) {
                    renderer_get_rendered_dimensions(renderer, &right_width, &right_height);
                    if (right_height <= 0) {
                        right_height = app_count_rendered_lines(right_rendered);
                    }
                    if (right_height <= 0) {
                        right_height = 1;
                    }
                    if (right_width <= 0) {
                        right_width = per_page_cols;
                    }
                } else {
                    has_right = FALSE;
                }
            }

            book_page_image_free(&left_image);
            book_page_image_free(&right_image);

            app_begin_sync_update();
            app_clear_kitty_images(app);
            gint image_area_top_row = 4;
            if (app->suppress_full_clear) {
                app->suppress_full_clear = FALSE;
                if (app->ui_text_hidden) {
                    app_clear_single_view_ui_lines(app);
                }
                app_clear_image_area(app, image_area_top_row, target_height);
            } else {
                app_clear_screen_for_refresh(app);
            }
            if (!app->ui_text_hidden && app->term_height > 0) {
                const char *title = "Book Reader";
                gchar *display_name = NULL;
                if (app->book_path) {
                    gchar *basename = g_path_get_basename(app->book_path);
                    gchar *safe_basename = sanitize_for_terminal(basename);
                    gint max_width = app_filename_max_width(app);
                    if (max_width <= 0) {
                        max_width = app->term_width;
                    }
                    display_name = truncate_utf8_middle_keep_suffix(safe_basename, max_width);
                    g_free(safe_basename);
                    g_free(basename);
                }
                gint title_len = (gint)strlen(title);
                gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
                printf("\033[1;1H\033[2K");
                for (gint i = 0; i < title_pad; i++) putchar(' ');
                printf("%s", title);

                printf("\033[2;1H\033[2K");

                printf("\033[3;1H\033[2K");
                if (display_name) {
                    gint name_len = utf8_display_width(display_name);
                    gint name_pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
                    for (gint i = 0; i < name_pad; i++) putchar(' ');
                    printf("%s", display_name);
                    g_free(display_name);
                }
            }

            gint spread_cols = per_page_cols * 2 + gutter_cols;
            gint spread_left_col = 1;
            if (spread_cols > 0 && app->term_width > spread_cols) {
                spread_left_col = (app->term_width - spread_cols) / 2 + 1;
            }
            gint left_half_start = spread_left_col;
            gint right_half_start = spread_left_col + per_page_cols + gutter_cols;

            gint left_col = left_half_start;
            if (left_width > 0 && left_width < per_page_cols) {
                left_col += (per_page_cols - left_width) / 2;
            }
            gint left_top_row = image_area_top_row;
            if (target_height > 0 && left_height > 0 && left_height < target_height) {
                gint vpad = (target_height - left_height) / 2;
                if (vpad < 0) vpad = 0;
                left_top_row = image_area_top_row + vpad;
            }

            gint right_col = right_half_start;
            gint right_top_row = image_area_top_row;
            if (right_rendered) {
                if (right_width > 0 && right_width < per_page_cols) {
                    right_col += (per_page_cols - right_width) / 2;
                }
                if (target_height > 0 && right_height > 0 && right_height < target_height) {
                    gint vpad = (target_height - right_height) / 2;
                    if (vpad < 0) vpad = 0;
                    right_top_row = image_area_top_row + vpad;
                }
            }

            app_print_rendered_at(left_rendered, left_top_row, left_col);
            if (right_rendered) {
                app_print_rendered_at(right_rendered, right_top_row, right_col);
            }

            gint top_row = left_top_row;
            gint bottom_row = left_top_row + left_height - 1;
            if (right_rendered && right_height > 0) {
                top_row = MIN(top_row, right_top_row);
                bottom_row = MAX(bottom_row, right_top_row + right_height - 1);
            }
            if (bottom_row < top_row) {
                top_row = image_area_top_row;
                bottom_row = image_area_top_row + (target_height > 0 ? target_height : 1) - 1;
            }

            app->last_render_top_row = top_row;
            app->last_render_height = bottom_row - top_row + 1;

            if (app->term_height > 0 && !app->ui_text_hidden) {
                gint current = app->book_page + 1;
                gint total = app->book_page_count;
                if (current < 1) current = 1;
                if (total < 1) total = 1;

                char left_text[32];
                g_snprintf(left_text, sizeof(left_text), "%d/%d", current, total);
                gint left_len = (gint)strlen(left_text);

                char right_text[32];
                gboolean has_right_page = (current + 1 <= total);
                gint right_len = 0;
                if (has_right_page) {
                    g_snprintf(right_text, sizeof(right_text), "%d/%d", current + 1, total);
                    right_len = (gint)strlen(right_text);
                }

                gint idx_row = (app->term_height >= 2) ? (app->term_height - 2) : 1;
                printf("\033[%d;1H\033[2K", idx_row);

                gint left_idx_col = left_half_start;
                if (left_len > 0 && left_len < per_page_cols) {
                    left_idx_col += (per_page_cols - left_len) / 2;
                }
                printf("\033[%d;%dH", idx_row, left_idx_col);
                printf("%s", left_text);

                if (has_right_page) {
                    gint right_idx_col = right_half_start;
                    if (right_len > 0 && right_len < per_page_cols) {
                        right_idx_col += (per_page_cols - right_len) / 2;
                    }
                    printf("\033[%d;%dH", idx_row, right_idx_col);
                    printf("%s", right_text);
                }

                const HelpSegment segments[] = {
                    {"←/→", "Prev/Next"},
                    {"PgUp/PgDn", "Page"},
                    {"P", "Page"},
                    {"Enter", "Preview"},
                    {"TAB", "Toggle"},
                    {"~", "Zen"},
                    {"ESC", "Exit"}
                };
                print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
            }

            if (app->book_jump_active) {
                app_book_jump_render_prompt(app);
            }

            app_end_sync_update();
            fflush(stdout);
            g_string_free(left_rendered, TRUE);
            if (right_rendered) {
                g_string_free(right_rendered, TRUE);
            }
            renderer_destroy(renderer);
            return ERROR_NONE;
        }
    }

    if (!double_page) {
        gint page_cols = target_width > 0 ? target_width : 1;
        gint page_rows = target_height > 0 ? target_height : 1;
        ErrorCode page_err = book_render_page(app->book_doc, app->book_page, page_cols, page_rows, &base_image);
        if (page_err != ERROR_NONE) {
            return page_err;
        }
    }

    ImageRenderer *renderer = renderer_create();
    if (!renderer) {
        book_page_image_free(&base_image);
        return ERROR_MEMORY_ALLOC;
    }

    RendererConfig config = {
        .max_width = target_width,
        .max_height = target_height,
        .preserve_aspect_ratio = TRUE,
        .dither = app->dither_enabled,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = app->render_work_factor,
        .force_text = app->force_text,
        .force_sixel = app->force_sixel,
        .force_kitty = app->force_kitty,
        .force_iterm2 = app->force_iterm2,
        .gamma = app->gamma,
        .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };

    ErrorCode error = renderer_initialize(renderer, &config);
    if (error != ERROR_NONE) {
        renderer_destroy(renderer);
        book_page_image_free(&base_image);
        return error;
    }

    GString *rendered = renderer_render_image_data(renderer,
                                                   base_image.pixels,
                                                   base_image.width,
                                                   base_image.height,
                                                   base_image.stride,
                                                   base_image.channels);
    book_page_image_free(&base_image);

    if (!rendered) {
        renderer_destroy(renderer);
        return ERROR_INVALID_IMAGE;
    }

    app_begin_sync_update();
    app_clear_kitty_images(app);
    gint image_area_top_row = 4;
    if (app->suppress_full_clear) {
        app->suppress_full_clear = FALSE;
        if (app->ui_text_hidden) {
            app_clear_single_view_ui_lines(app);
        }
        app_clear_image_area(app, image_area_top_row, target_height);
    } else {
        app_clear_screen_for_refresh(app);
    }
    if (!app->ui_text_hidden && app->term_height > 0) {
        const char *title = "Book Reader";
        gchar *display_name = NULL;
        if (app->book_path) {
            gchar *basename = g_path_get_basename(app->book_path);
            gchar *safe_basename = sanitize_for_terminal(basename);
            gint max_width = app_filename_max_width(app);
            if (max_width <= 0) {
                max_width = app->term_width;
            }
            display_name = truncate_utf8_middle_keep_suffix(safe_basename, max_width);
            g_free(safe_basename);
            g_free(basename);
        }
        gint title_len = (gint)strlen(title);
        gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
        printf("\033[1;1H\033[2K");
        for (gint i = 0; i < title_pad; i++) putchar(' ');
        printf("%s", title);

        printf("\033[2;1H\033[2K");

        printf("\033[3;1H\033[2K");
        if (display_name) {
            gint name_len = utf8_display_width(display_name);
            gint name_pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
            for (gint i = 0; i < name_pad; i++) putchar(' ');
            printf("%s", display_name);
            g_free(display_name);
        }
    }

    gint image_width = 0;
    gint image_height = 0;
    renderer_get_rendered_dimensions(renderer, &image_width, &image_height);
    if (image_height <= 0) {
        image_height = 1;
        for (gsize i = 0; i < rendered->len; i++) {
            if (rendered->str[i] == '\n') {
                image_height++;
            }
        }
    }

    gint effective_width = image_width > 0 ? image_width : target_width;
    if (effective_width > app->term_width) {
        effective_width = app->term_width;
    }
    if (effective_width < 0) {
        effective_width = 0;
    }
    gint left_pad = (app->term_width > effective_width) ? (app->term_width - effective_width) / 2 : 0;
    if (left_pad < 0) left_pad = 0;

    gint image_top_row = image_area_top_row;
    if (target_height > 0 && image_height > 0 && image_height < target_height) {
        gint vpad = (target_height - image_height) / 2;
        if (vpad < 0) vpad = 0;
        image_top_row = image_area_top_row + vpad;
    }
    app->last_render_top_row = image_top_row;
    app->last_render_height = image_height > 0 ? image_height : (target_height > 0 ? target_height : 1);

    gchar *pad_buffer = NULL;
    if (left_pad > 0) {
        pad_buffer = g_malloc(left_pad);
        memset(pad_buffer, ' ', left_pad);
    }

    const gchar *line_ptr = rendered->str;
    gint row = image_top_row;
    while (line_ptr && *line_ptr) {
        const gchar *newline = strchr(line_ptr, '\n');
        gint line_len = newline ? (gint)(newline - line_ptr) : (gint)strlen(line_ptr);
        printf("\033[%d;1H", row);
        if (left_pad > 0) {
            fwrite(pad_buffer, 1, left_pad, stdout);
        }
        if (line_len > 0) {
            fwrite(line_ptr, 1, line_len, stdout);
        }
        if (!newline) {
            break;
        }
        line_ptr = newline + 1;
        row++;
    }
    g_free(pad_buffer);

    if (app->term_height > 0 && !app->ui_text_hidden) {
        gint current = app->book_page + 1;
        gint total = app->book_page_count;
        if (current < 1) current = 1;
        if (total < 1) total = 1;
        char idx_text[32];
        if (double_page) {
            gint right_page = MIN(total, current + 1);
            g_snprintf(idx_text, sizeof(idx_text), "%d-%d/%d", current, right_page, total);
        } else {
            g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
        }
        gint idx_len = (gint)strlen(idx_text);
        gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
        gint idx_row = (app->term_height >= 2) ? (app->term_height - 2) : 1;
        printf("\033[%d;1H\033[2K", idx_row);
        for (gint i = 0; i < idx_pad; i++) putchar(' ');
        printf("%s", idx_text);

        const HelpSegment segments[] = {
            {"←/→", "Prev/Next"},
            {"PgUp/PgDn", "Page"},
            {"P", "Page"},
            {"Enter", "Preview"},
            {"TAB", "Toggle"},
            {"~", "Zen"},
            {"ESC", "Exit"}
        };
        print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
    }

    if (app->book_jump_active) {
        app_book_jump_render_prompt(app);
    }

    app_end_sync_update();
    fflush(stdout);
    g_string_free(rendered, TRUE);
    renderer_destroy(renderer);
    return ERROR_NONE;
}

static void app_book_toc_layout(const PixelTermApp *app,
                                gint *content_rows,
                                gint *list_top_row,
                                gint *list_bottom_row,
                                gint *header_lines,
                                gint *footer_lines) {
    gint rows = (app && app->term_height > 0) ? app->term_height : 1;
    gint header = 4;
    gint footer = 4;

    gint top = 5;
    gint bottom = rows - 4;
    if (bottom < top) {
        bottom = top;
    }
    gint available = bottom - top + 1;
    if (available < 1) {
        available = 1;
    }

    if (content_rows) {
        *content_rows = available;
    }
    if (list_top_row) {
        *list_top_row = top;
    }
    if (list_bottom_row) {
        *list_bottom_row = bottom;
    }
    if (header_lines) {
        *header_lines = header;
    }
    if (footer_lines) {
        *footer_lines = footer;
    }
}

static void app_book_toc_adjust_scroll(PixelTermApp *app, gint visible_rows) {
    if (!app || !app->book_toc) {
        return;
    }

    gint total = app->book_toc->count;
    if (total <= 0) {
        app->book_toc_selected = 0;
        app->book_toc_scroll = 0;
        return;
    }

    if (visible_rows < 1) {
        visible_rows = 1;
    }

    if (app->book_toc_selected < 0) {
        app->book_toc_selected = 0;
    } else if (app->book_toc_selected >= total) {
        app->book_toc_selected = total - 1;
    }

    if (total <= visible_rows) {
        app->book_toc_scroll = 0;
        return;
    }

    gint target_row = visible_rows / 2;
    gint desired_offset = app->book_toc_selected - target_row;
    gint max_offset = MAX(0, total - 1 - target_row);

    if (desired_offset < 0) desired_offset = 0;
    if (desired_offset > max_offset) desired_offset = max_offset;

    if (app->book_toc_scroll != desired_offset) {
        app->book_toc_scroll = desired_offset;
    }
}

static BookTocItem* app_book_toc_item_at(BookToc *toc, gint index) {
    if (!toc || index < 0) {
        return NULL;
    }
    BookTocItem *item = toc->items;
    gint idx = 0;
    while (item && idx < index) {
        item = item->next;
        idx++;
    }
    return item;
}

typedef struct {
    gint total_entries;
    gint start_row;
    gint end_row;
    gint rows_to_render;
    gint top_padding;
} BookTocViewport;

static BookTocViewport app_book_toc_compute_viewport(PixelTermApp *app, gint visible_rows) {
    BookTocViewport viewport = {0};
    if (!app || !app->book_toc) {
        return viewport;
    }

    viewport.total_entries = app->book_toc->count;
    gint available_rows = visible_rows;
    if (available_rows < 0) {
        available_rows = 0;
    }

    gint max_offset = MAX(0, viewport.total_entries - 1);
    gint scroll_offset = app->book_toc_scroll;
    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }

    gint start_row = scroll_offset;
    if (viewport.total_entries <= 0) {
        start_row = 0;
    } else if (start_row >= viewport.total_entries) {
        start_row = viewport.total_entries - 1;
    }

    gint end_row = MIN(start_row + available_rows, viewport.total_entries);
    gint rows_to_render = end_row - start_row;
    if (rows_to_render < 0) {
        rows_to_render = 0;
    }

    gint selected_row = app->book_toc_selected;
    if (selected_row < 0) {
        selected_row = 0;
    } else if (viewport.total_entries > 0 && selected_row >= viewport.total_entries) {
        selected_row = viewport.total_entries - 1;
    } else if (viewport.total_entries == 0) {
        selected_row = 0;
    }

    gint selected_pos = selected_row - start_row;
    if (selected_pos < 0) {
        selected_pos = 0;
    }
    if (rows_to_render > 0 && selected_pos >= rows_to_render) {
        selected_pos = rows_to_render - 1;
    }

    gint target_row = available_rows / 2;
    gint top_padding = target_row - selected_pos;
    if (top_padding < 0) {
        gint more_rows_below = MAX(0, viewport.total_entries - end_row);
        gint scroll_shift = MIN(-top_padding, more_rows_below);
        if (scroll_shift > 0) {
            start_row += scroll_shift;
            end_row = MIN(start_row + available_rows, viewport.total_entries);
            rows_to_render = end_row - start_row;
            if (rows_to_render < 0) {
                rows_to_render = 0;
            }
            selected_pos = selected_row - start_row;
            if (selected_pos < 0) {
                selected_pos = 0;
            }
            if (rows_to_render > 0 && selected_pos >= rows_to_render) {
                selected_pos = rows_to_render - 1;
            }
            top_padding = target_row - selected_pos;
        }
        if (top_padding < 0) {
            top_padding = 0;
        }
    }

    gint visible_space = MAX(0, available_rows - top_padding);
    if (rows_to_render > visible_space) {
        end_row = MIN(viewport.total_entries, start_row + visible_space);
        rows_to_render = end_row - start_row;
        if (rows_to_render < 0) {
            rows_to_render = 0;
        }
    }

    viewport.start_row = start_row;
    viewport.end_row = end_row;
    viewport.rows_to_render = rows_to_render;
    viewport.top_padding = top_padding;
    return viewport;
}

static gboolean app_book_toc_hit_test(PixelTermApp *app,
                                      gint mouse_x,
                                      gint mouse_y,
                                      gint *out_index) {
    (void)mouse_x;
    if (!app || !app->book_toc || !app->book_toc_visible) {
        return FALSE;
    }

    get_terminal_size(&app->term_width, &app->term_height);
    gint content_rows = 1;
    gint list_top_row = 1;
    gint list_bottom_row = app->term_height;
    app_book_toc_layout(app, &content_rows, &list_top_row, &list_bottom_row, NULL, NULL);

    if (mouse_y < list_top_row || mouse_y > list_bottom_row) {
        return FALSE;
    }

    gint row_idx = mouse_y - list_top_row;
    if (row_idx < 0 || row_idx >= content_rows) {
        return FALSE;
    }

    BookTocViewport viewport = app_book_toc_compute_viewport(app, content_rows);
    if (row_idx < viewport.top_padding) {
        return FALSE;
    }

    gint relative_row = row_idx - viewport.top_padding;
    if (relative_row < 0 || relative_row >= viewport.rows_to_render) {
        return FALSE;
    }

    gint absolute_row = viewport.start_row + relative_row;
    if (absolute_row < 0 || absolute_row >= viewport.total_entries) {
        return FALSE;
    }

    if (out_index) {
        *out_index = absolute_row;
    }
    return TRUE;
}

ErrorCode app_book_toc_move_selection(PixelTermApp *app, gint delta) {
    if (!app || !app->book_toc) {
        return ERROR_MEMORY_ALLOC;
    }
    gint total = app->book_toc->count;
    if (total <= 0) {
        app->book_toc_selected = 0;
        app->book_toc_scroll = 0;
        return ERROR_NONE;
    }
    gint visible_rows = 1;
    app_book_toc_layout(app, &visible_rows, NULL, NULL, NULL, NULL);
    if (visible_rows < 1) {
        visible_rows = 1;
    }

    gint next = app->book_toc_selected + delta;
    next %= total;
    if (next < 0) {
        next += total;
    }
    app->book_toc_selected = next;
    app_book_toc_adjust_scroll(app, visible_rows);
    return ERROR_NONE;
}

ErrorCode app_book_toc_page_move(PixelTermApp *app, gint direction) {
    if (!app || !app->book_toc) {
        return ERROR_MEMORY_ALLOC;
    }
    gint visible_rows = 1;
    app_book_toc_layout(app, &visible_rows, NULL, NULL, NULL, NULL);
    if (visible_rows < 1) {
        visible_rows = 1;
    }

    gint delta = (direction >= 0) ? visible_rows : -visible_rows;
    return app_book_toc_move_selection(app, delta);
}

ErrorCode app_book_toc_sync_to_page(PixelTermApp *app, gint page_index) {
    if (!app || !app->book_toc) {
        return ERROR_MEMORY_ALLOC;
    }
    gint total = app->book_toc->count;
    if (total <= 0) {
        app->book_toc_selected = 0;
        app->book_toc_scroll = 0;
        return ERROR_NONE;
    }

    gint selected = 0;
    BookTocItem *item = app->book_toc->items;
    gint index = 0;
    while (item) {
        if (item->page <= page_index) {
            selected = index;
        } else {
            break;
        }
        item = item->next;
        index++;
    }
    app->book_toc_selected = selected;

    gint visible_rows = 1;
    app_book_toc_layout(app, &visible_rows, NULL, NULL, NULL, NULL);
    if (visible_rows < 1) {
        visible_rows = 1;
    }
    app_book_toc_adjust_scroll(app, visible_rows);
    return ERROR_NONE;
}

gint app_book_toc_get_selected_page(PixelTermApp *app) {
    if (!app || !app->book_toc) {
        return -1;
    }
    BookTocItem *item = app_book_toc_item_at(app->book_toc, app->book_toc_selected);
    if (!item) {
        return -1;
    }
    return item->page;
}

ErrorCode app_handle_mouse_click_book_toc(PixelTermApp *app,
                                          gint mouse_x,
                                          gint mouse_y,
                                          gboolean *redraw_needed,
                                          gboolean *out_hit) {
    if (redraw_needed) {
        *redraw_needed = FALSE;
    }
    if (out_hit) {
        *out_hit = FALSE;
    }
    if (!app || !app->book_toc_visible || !app->book_toc) {
        return ERROR_MEMORY_ALLOC;
    }

    gint index = -1;
    if (!app_book_toc_hit_test(app, mouse_x, mouse_y, &index)) {
        return ERROR_NONE;
    }

    if (out_hit) {
        *out_hit = TRUE;
    }

    gint old_selected = app->book_toc_selected;
    gint old_scroll = app->book_toc_scroll;
    app->book_toc_selected = index;

    gint visible_rows = 1;
    app_book_toc_layout(app, &visible_rows, NULL, NULL, NULL, NULL);
    if (visible_rows < 1) {
        visible_rows = 1;
    }
    app_book_toc_adjust_scroll(app, visible_rows);

    if (redraw_needed &&
        (app->book_toc_selected != old_selected || app->book_toc_scroll != old_scroll)) {
        *redraw_needed = TRUE;
    }
    return ERROR_NONE;
}

// Render book table of contents
ErrorCode app_render_book_toc(PixelTermApp *app) {
    if (!app || !app->book_toc) {
        return ERROR_MEMORY_ALLOC;
    }

    get_terminal_size(&app->term_width, &app->term_height);
    app_begin_sync_update();
    app_clear_kitty_images(app);
    app_clear_screen_for_refresh(app);

    gint rows = app->term_height;
    gint cols = app->term_width;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    gint content_rows = 1;
    gint list_top_row = 1;
    gint list_bottom_row = rows;
    gint footer_lines = 0;
    app_book_toc_layout(app, &content_rows, &list_top_row, &list_bottom_row, NULL, &footer_lines);

    app_book_toc_adjust_scroll(app, content_rows);
    BookTocViewport viewport = app_book_toc_compute_viewport(app, content_rows);
    app->book_toc_scroll = viewport.start_row;

    const char *header_title = "Table of Contents";
    gint title_len = (gint)strlen(header_title);
    gint title_pad = (cols > title_len) ? (cols - title_len) / 2 : 0;
    printf("\033[1;1H\033[2K");
    for (gint i = 0; i < title_pad; i++) putchar(' ');
    printf("%s", header_title);
    printf("\033[2;1H\033[2K");

    gchar *base = app->book_path ? g_path_get_basename(app->book_path) : g_strdup("");
    if (base && base[0]) {
        char *dot = strrchr(base, '.');
        if (dot && dot != base) {
            *dot = '\0';
        }
    }
    gchar *safe_name = sanitize_for_terminal(base);
    gchar *display_name = truncate_utf8_for_display(safe_name, cols > 8 ? cols - 8 : cols);
    gint name_len = utf8_display_width(display_name);
    gint name_pad = (cols > name_len) ? (cols - name_len) / 2 : 0;
    printf("\033[3;1H\033[2K");
    for (gint i = 0; i < name_pad; i++) putchar(' ');
    printf("%s", display_name);
    printf("\033[4;1H\033[2K");
    g_free(display_name);
    g_free(safe_name);
    g_free(base);

    for (gint row = list_top_row; row <= list_bottom_row; row++) {
        printf("\033[%d;1H\033[2K", row);
    }

    gint total_entries = app->book_toc->count;
    if (total_entries <= 0 || !app->book_toc->items) {
        if (rows > 0) {
            const char *empty_msg = "(No contents)";
            gint msg_len = utf8_display_width(empty_msg);
            gint center_pad = (cols > msg_len) ? (cols - msg_len) / 2 : 0;
            gint target_row = list_top_row + (content_rows / 2);
            if (target_row < 1) target_row = 1;
            if (target_row > rows) target_row = rows;
            printf("\033[%d;1H\033[2K", target_row);
            for (gint i = 0; i < center_pad; i++) putchar(' ');
            printf("\033[33m%s\033[0m", empty_msg);
        }
    } else {
        gint page_digits = 1;
        gint pages = app->book_page_count > 0 ? app->book_page_count : 1;
        while (pages >= 10) {
            page_digits++;
            pages /= 10;
        }
        gint page_width = MAX(3, page_digits);
        const gint prefix_width = 2;
        const gint gap_width = 2;
        gint max_line_width = cols;

        gint visible_start = viewport.start_row;
        gint visible_end = viewport.end_row;

        gint line_content_width = 0;
        BookTocItem *scan = app_book_toc_item_at(app->book_toc, visible_start);
        gint scan_index = visible_start;
        while (scan && scan_index < visible_end) {
            gint indent = scan->level * 2;
            gint max_indent = cols / 4;
            if (indent > max_indent) indent = max_indent;

            gint title_max = max_line_width - prefix_width - indent - gap_width - page_width;
            if (title_max < 1) title_max = 1;

            gchar *safe_title = sanitize_for_terminal(scan->title ? scan->title : "Untitled");
            gchar *display_title = truncate_utf8_for_display(safe_title, title_max);
            gint title_len = utf8_display_width(display_title);

            gint line_width = prefix_width + indent + title_len + gap_width + page_width;
            if (line_width > line_content_width) {
                line_content_width = line_width;
            }

            g_free(display_title);
            g_free(safe_title);
            scan = scan->next;
            scan_index++;
        }
        if (line_content_width < 1) {
            line_content_width = MIN(max_line_width, prefix_width + gap_width + page_width + 1);
        }
        if (line_content_width > max_line_width) {
            line_content_width = max_line_width;
        }
        gint line_pad = (cols > line_content_width) ? (cols - line_content_width) / 2 : 0;

        BookTocItem *item = app_book_toc_item_at(app->book_toc, visible_start);
        gint index = visible_start;
        gint display_row = list_top_row + viewport.top_padding;

        while (item && index < visible_end) {
            if (display_row > list_bottom_row) {
                break;
            }

                gboolean is_selected = (index == app->book_toc_selected);
                gint indent = item->level * 2;
                gint max_indent = cols / 4;
                if (indent > max_indent) indent = max_indent;

                gint title_max = line_content_width - prefix_width - indent - gap_width - page_width;
                if (title_max < 1) title_max = 1;

                gchar *safe_title = sanitize_for_terminal(item->title ? item->title : "Untitled");
                gchar *display_title = truncate_utf8_for_display(safe_title, title_max);
                gint title_len = utf8_display_width(display_title);

                printf("\033[%d;1H", display_row);
                for (gint i = 0; i < line_pad; i++) putchar(' ');
                if (is_selected) {
                    printf("\033[47;30m");
                }
                printf("  ");
                for (gint i = 0; i < indent; i++) putchar(' ');
                printf("%s", display_title);

                gint fill = line_content_width - (prefix_width + indent + title_len + gap_width + page_width);
                if (fill < 0) fill = 0;
                for (gint i = 0; i < gap_width + fill; i++) putchar(' ');
                printf("%*d", page_width, item->page + 1);

                if (is_selected) {
                    printf("\033[0m");
                }

                g_free(display_title);
                g_free(safe_title);
            item = item->next;
            index++;
            display_row++;
        }
    }

    for (gint y = MAX(1, rows - 3); y <= rows - 1; y++) {
        printf("\033[%d;1H\033[2K", y);
    }

    if (footer_lines > 0) {
        const HelpSegment segments[] = {
            {"↑/↓", "Move"},
            {"PgUp/PgDn", "Page"},
            {"Home/End", "Top/Bottom"},
            {"Enter", "Open"},
            {"T/ESC", "Close"},
        };
        print_centered_help_line(rows, cols, segments, G_N_ELEMENTS(segments));
    }

    app_end_sync_update();
    fflush(stdout);
    return ERROR_NONE;
}

// Render file manager interface
ErrorCode app_render_file_manager(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    // Update terminal dimensions before layout
    get_terminal_size(&app->term_width, &app->term_height);

    // Don't do a full-screen clear on every navigation step; we explicitly clear/redraw
    // the rows we touch to keep movement smooth and avoid extra terminal workarounds.
    printf("\033[H\033[0m");
    
    // Get current directory
    const gchar *current_dir = app->file_manager_directory;
    gboolean free_dir = FALSE;
    if (!current_dir) {
        current_dir = app->current_directory;
        if (!current_dir) {
            current_dir = g_get_current_dir();
            free_dir = TRUE;
        }
    }
    gchar *safe_current_dir = sanitize_for_terminal(current_dir);

    // Header centered: row 1 app name, row 3 current directory (row 2/4 blank)
    const char *header_title = "PixelTerm File Manager";
    gint title_len = strlen(header_title);
    gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
    printf("\033[1;1H\033[2K");
    for (gint i = 0; i < title_pad; i++) putchar(' ');
    printf("%s", header_title);
    printf("\033[2;1H\033[2K"); // blank line for symmetry

    gint dir_byte_len = strlen(safe_current_dir);
    gint dir_len = utf8_display_width(safe_current_dir);
    gchar *display_dir = safe_current_dir;
    
    // Truncate directory path if it's too long
    if (dir_len > app->term_width - 8) { // More conservative threshold
        gint max_dir_display = app->term_width - 11; // Reserve space for "..." and more padding
        if (max_dir_display > 20) { // Higher minimum for directory paths
            gint start_len = (max_dir_display * 2) / 3; // Show more of the beginning
            gint end_len = max_dir_display - start_len;
            
            // Adjust start_len to avoid cutting UTF-8 characters
            while (start_len > 0 && (safe_current_dir[start_len] & 0xC0) == 0x80) {
                start_len--;
            }
            
            // Find proper start position for end part
            gint end_start = dir_byte_len - end_len;
            while (end_start < dir_byte_len && (safe_current_dir[end_start] & 0xC0) == 0x80) {
                end_start++;
            }
            
            gchar *start_part = g_strndup(safe_current_dir, start_len);
            gchar *end_part = g_strdup(safe_current_dir + end_start);
            
            display_dir = g_strdup_printf("%s...%s", start_part, end_part);
            g_free(start_part);
            g_free(end_part);
        } else {
            gint truncate_len = max_dir_display;
            // Adjust to avoid cutting UTF-8 characters
            while (truncate_len > 0 && (safe_current_dir[truncate_len] & 0xC0) == 0x80) {
                truncate_len--;
            }
            gchar *shortened = g_strndup(safe_current_dir, MAX(0, truncate_len));
            display_dir = g_strdup_printf("%s...", shortened);
            g_free(shortened);
        }
        dir_len = utf8_display_width(display_dir);
    }
    
    // Better centering calculation for directory path
    gint dir_pad = (app->term_width > dir_len) ? (app->term_width - dir_len) / 2 : 0;
    // Ensure the path doesn't exceed terminal bounds
    if (dir_pad + dir_len > app->term_width) {
        dir_pad = MAX(0, app->term_width - dir_len);
    }
    // Print the centered directory path on row 3
    printf("\033[3;1H\033[2K");
    for (gint i = 0; i < dir_pad; i++) putchar(' ');
    printf("%s", display_dir);
    printf("\033[4;1H\033[2K"); // blank line for symmetry
    
    // Free the truncated directory path if it was created
    if (display_dir != safe_current_dir) {
        g_free(display_dir);
    }

    FileManagerViewport viewport = app_file_manager_compute_viewport(app);
    app->scroll_offset = viewport.start_row;
    gint total_entries = viewport.total_entries;
    const char *help_text = "↑/↓ Move   ← Parent   →/Enter Open   TAB Toggle   Ctrl+H Hidden   ESC Exit";
    gint start_row = viewport.start_row;
    gint end_row = viewport.end_row;
    gint rows_to_render = viewport.rows_to_render;
    gint top_padding = viewport.top_padding;
    gint bottom_padding = viewport.bottom_padding;

    // Render list within fixed rows [5 .. term_height-4] to keep symmetry and avoid scrolling
    const gint list_top_row = 5;
    gint list_bottom_row = app->term_height - 4;
    if (list_bottom_row < list_top_row) {
        list_bottom_row = list_top_row;
    }
    gint list_visible_rows = list_bottom_row - list_top_row + 1;

    for (gint i = 0; i < list_visible_rows; i++) {
        gint y = list_top_row + i;
        printf("\033[%d;1H\033[2K", y);

        if (total_entries == 0) {
            if (i == list_visible_rows / 2) {
                const char *empty_msg = "（No items）";
                gint msg_len = utf8_display_width(empty_msg);
                gint center_pad = (app->term_width > msg_len) ? (app->term_width - msg_len) / 2 : 0;
                for (gint s = 0; s < center_pad; s++) putchar(' ');
                printf("\033[33m%s\033[0m", empty_msg);
            }
            continue;
        }

        if (i < top_padding) {
            continue;
        }
        gint relative_row = i - top_padding;
        if (relative_row < 0 || relative_row >= (end_row - start_row)) {
            continue;
        }
        gint idx = start_row + relative_row; // single column
        if (idx < 0 || idx >= total_entries) {
            continue;
        }

        gchar *entry = (gchar*)g_list_nth_data(app->directory_entries, idx);
        gboolean is_dir = FALSE;
        gchar *display_name = app_file_manager_display_name(app, entry, &is_dir);
        gchar *print_name = sanitize_for_terminal(display_name);
        gint name_len = utf8_display_width(print_name);
        gboolean is_image = (!is_dir && is_image_file(entry));
        gboolean is_video = (!is_dir && is_video_file(entry));
        gboolean is_book = (!is_dir && is_book_file(entry));
        gboolean is_dir_with_images = is_dir && directory_contains_images(entry);
        gboolean is_dir_with_media = is_dir && directory_contains_media(entry);
        gboolean is_dir_with_books = is_dir && directory_contains_books(entry);

        gint max_display_width = (app->term_width / 2) - 2;
        if (max_display_width < 15) max_display_width = 15;

        if (name_len > max_display_width) {
            gint max_display = max_display_width - 3;
            if (max_display > 8) {
                gint start_len = max_display / 2;
                gint end_len = max_display - start_len;

                gint char_count = 0;
                const gchar *p = print_name;
                while (*p) {
                    gunichar ch = g_utf8_get_char_validated(p, -1);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        char_count++;
                        p++;
                    } else {
                        char_count++;
                        p = g_utf8_next_char(p);
                    }
                }

                gint start_byte = 0;
                gint current_char = 0;
                p = print_name;
                while (*p && current_char < start_len) {
                    gunichar ch = g_utf8_get_char_validated(p, -1);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        p++;
                    } else {
                        p = g_utf8_next_char(p);
                    }
                    current_char++;
                }
                start_byte = p - print_name;

                current_char = 0;
                const gchar *end_p = print_name;
                while (*end_p) {
                    gunichar ch = g_utf8_get_char_validated(end_p, -1);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        end_p++;
                    } else {
                        end_p = g_utf8_next_char(end_p);
                    }
                    current_char++;
                }

                current_char = 0;
                end_p = print_name;
                while (*end_p && current_char < char_count - end_len) {
                    gunichar ch = g_utf8_get_char_validated(end_p, -1);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        end_p++;
                    } else {
                        end_p = g_utf8_next_char(end_p);
                    }
                    current_char++;
                }
                gint end_byte = end_p - print_name;

                gchar *start_part = g_strndup(print_name, start_byte);
                gchar *end_part = g_strdup(print_name + end_byte);
                g_free(print_name);
                print_name = g_strdup_printf("%s...%s", start_part, end_part);
                g_free(start_part);
                g_free(end_part);
            } else {
                gint truncate_len = max_display;
                gint display_width = 0;
                const gchar *p = print_name;
                const gchar *truncate_pos = print_name;

                while (*p && display_width < truncate_len) {
                    gunichar ch = g_utf8_get_char_validated(p, -1);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        display_width++;
                        truncate_pos = p + 1;
                        p++;
                    } else {
                        gint char_width = g_unichar_iswide(ch) ? 2 : 1;
                        if (display_width + char_width > truncate_len) {
                            break;
                        }
                        display_width += char_width;
                        truncate_pos = g_utf8_next_char(p);
                        p = truncate_pos;
                    }
                }

                gchar *shortened = g_strndup(print_name, truncate_pos - print_name);
                g_free(print_name);
                print_name = g_strdup_printf("%s...", shortened);
                g_free(shortened);
            }
            name_len = utf8_display_width(print_name);
        }

        gint pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
        if (pad + name_len > app->term_width) {
            pad = MAX(0, app->term_width - name_len);
        }
        for (gint s = 0; s < pad; s++) putchar(' ');

        gboolean is_valid_media = (!is_dir && is_valid_media_file(entry));
        gboolean is_valid_book = (!is_dir && is_valid_book_file(entry));
        gboolean selected = (idx == app->selected_entry);
        if ((is_image || is_video) && !is_valid_media) {
            if (selected) {
                printf("\033[47;30m%s\033[0m\033[31m [Invalid]\033[0m", print_name);
            } else {
                printf("\033[31m%s [Invalid]\033[0m", print_name);
            }
        } else if (is_book && !is_valid_book) {
            if (selected) {
                printf("\033[47;30m%s\033[0m\033[31m [Invalid]\033[0m", print_name);
            } else {
                printf("\033[31m%s [Invalid]\033[0m", print_name);
            }
        } else if (selected) {
            printf("\033[47;30m%s\033[0m", print_name);
        } else if (is_dir_with_images || is_dir_with_media || is_dir_with_books) {
            printf("\033[33m%s\033[0m", print_name);
        } else if (is_dir) {
            printf("\033[34m%s\033[0m", print_name);
        } else if (is_image) {
            printf("\033[32m%s\033[0m", print_name);
        } else if (is_video) {
            printf("\033[35m%s\033[0m", print_name);
        } else if (is_book) {
            printf("\033[36m%s\033[0m", print_name);
        } else {
            printf("%s", print_name);
        }


        g_free(print_name);
        g_free(display_name);
    }

    // Footer area: keep symmetry (rows term_height-3 .. term_height-1 blank) and help on last line
    for (gint y = MAX(1, app->term_height - 3); y <= app->term_height - 1; y++) {
        printf("\033[%d;1H\033[2K", y);
    }

    gint help_len = strlen(help_text);
    gint help_pad = (app->term_width > help_len) ? (app->term_width - help_len) / 2 : 0;
    printf("\033[%d;1H\033[2K", app->term_height);
    for (gint i = 0; i < help_pad; i++) putchar(' ');
    printf("\033[36m↑/↓\033[0m Move   ");
    printf("\033[36m←\033[0m Parent   ");
    printf("\033[36m→/Enter\033[0m Open   ");
    printf("\033[36mTAB\033[0m Toggle   ");
    printf("\033[36mCtrl+H\033[0m Hidden   ");
    printf("\033[36mESC\033[0m Exit");

    fflush(stdout);

    g_free(safe_current_dir);
    if (free_dir) {
        g_free((gchar*)current_dir);
    }
    return ERROR_NONE;
}
