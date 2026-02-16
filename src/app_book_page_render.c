#include "app.h"
#include "text_utils.h"
#include "ui_render_utils.h"

static const gint k_book_spread_gutter_cols = 2;
static const HelpSegment k_book_page_help_segments[] = {
    {"←/→", "Prev/Next"},
    {"PgUp/PgDn", "Page"},
    {"P", "Page"},
    {"T", "TOC"},
    {"Enter", "Preview"},
    {"TAB", "Toggle"},
    {"~", "Zen"},
    {"ESC", "Exit"}
};

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

static RendererConfig app_book_make_renderer_config(const PixelTermApp *app,
                                                    gint max_width,
                                                    gint max_height) {
    RendererConfig config = {
        .max_width = max_width,
        .max_height = max_height,
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
    return config;
}

static void app_book_render_header(const PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height <= 0) {
        return;
    }

    const char *title = "Book Reader";
    gchar *display_name = NULL;
    if (app->book.path) {
        gchar *basename = g_path_get_basename(app->book.path);
        if (basename) {
            char *dot = strrchr(basename, '.');
            if (dot && dot != basename) {
                *dot = '\0';
            }
        }
        gchar *safe_basename = sanitize_for_terminal(basename);
        gint max_width = ui_filename_max_width(app);
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

static gint app_book_begin_frame(PixelTermApp *app, gint target_height) {
    gint image_area_top_row = 4;
    ui_begin_sync_update();
    ui_clear_kitty_images(app);
    if (app->suppress_full_clear) {
        app->suppress_full_clear = FALSE;
        if (app->ui_text_hidden) {
            ui_clear_single_view_lines(app);
        }
        ui_clear_area(app, image_area_top_row, target_height);
    } else {
        ui_clear_screen_for_refresh(app);
    }
    app_book_render_header(app);
    return image_area_top_row;
}

static void app_book_render_help_line(const PixelTermApp *app) {
    if (!app || app->term_height <= 0) {
        return;
    }
    ui_print_centered_help_line(app->term_height,
                                app->term_width,
                                k_book_page_help_segments,
                                G_N_ELEMENTS(k_book_page_help_segments));
}

ErrorCode app_render_book_page(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (!app->book.doc) {
        return ERROR_INVALID_IMAGE;
    }
    if (app->book.page_count <= 0) {
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
            ErrorCode left_err = book_render_page(app->book.doc, app->book.page, per_page_cols, per_page_rows, &left_image);
            if (left_err != ERROR_NONE) {
                return left_err;
            }

            BookPageImage right_image = {0};
            gboolean has_right = (app->book.page + 1 < app->book.page_count);
            if (has_right) {
                ErrorCode right_err = book_render_page(app->book.doc, app->book.page + 1, per_page_cols, per_page_rows, &right_image);
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

            RendererConfig config = app_book_make_renderer_config(app, per_page_cols, target_height);

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

            gint image_area_top_row = app_book_begin_frame(app, target_height);

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
                gint current = app->book.page + 1;
                gint total = app->book.page_count;
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

                app_book_render_help_line(app);
            }

            if (app->book.jump_active) {
                app_book_jump_render_prompt(app);
            }

            ui_end_sync_update();
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
        ErrorCode page_err = book_render_page(app->book.doc, app->book.page, page_cols, page_rows, &base_image);
        if (page_err != ERROR_NONE) {
            return page_err;
        }
    }

    ImageRenderer *renderer = renderer_create();
    if (!renderer) {
        book_page_image_free(&base_image);
        return ERROR_MEMORY_ALLOC;
    }

    RendererConfig config = app_book_make_renderer_config(app, target_width, target_height);

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

    gint image_area_top_row = app_book_begin_frame(app, target_height);

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
        gint current = app->book.page + 1;
        gint total = app->book.page_count;
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

        app_book_render_help_line(app);
    }

    if (app->book.jump_active) {
        app_book_jump_render_prompt(app);
    }

    ui_end_sync_update();
    fflush(stdout);
    g_string_free(rendered, TRUE);
    renderer_destroy(renderer);
    return ERROR_NONE;
}
