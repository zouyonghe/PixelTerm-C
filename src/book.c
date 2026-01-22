#include "book.h"

#ifdef HAVE_MUPDF

#include <fcntl.h>
#include <math.h>
#include <mupdf/fitz.h>

struct BookDocument {
    fz_context *ctx;
    fz_document *doc;
    gint page_count;
    gchar *path;
    gboolean suppress_stderr;
};

typedef struct {
    int saved_fd;
    int null_fd;
    gboolean active;
} StderrSilencer;

static void book_mupdf_warn(void *user, const char *message) {
    (void)user;
    (void)message;
}

static gboolean book_should_suppress_warnings(const char *filepath) {
    const char *ext = get_file_extension(filepath);
    return ext && g_ascii_strcasecmp(ext, ".epub") == 0;
}

static void book_stderr_silencer_begin(StderrSilencer *silencer) {
    if (!silencer) {
        return;
    }
    silencer->active = FALSE;
    silencer->saved_fd = -1;
    silencer->null_fd = -1;

    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    if (saved < 0) {
        return;
    }
    int null_fd = open("/dev/null", O_WRONLY);
    if (null_fd < 0) {
        close(saved);
        return;
    }
    if (dup2(null_fd, STDERR_FILENO) < 0) {
        close(saved);
        close(null_fd);
        return;
    }

    silencer->saved_fd = saved;
    silencer->null_fd = null_fd;
    silencer->active = TRUE;
}

static void book_stderr_silencer_end(StderrSilencer *silencer) {
    if (!silencer || !silencer->active) {
        return;
    }
    fflush(stderr);
    dup2(silencer->saved_fd, STDERR_FILENO);
    close(silencer->saved_fd);
    close(silencer->null_fd);
    silencer->saved_fd = -1;
    silencer->null_fd = -1;
    silencer->active = FALSE;
}

static void book_set_error(ErrorCode *out_error, ErrorCode value) {
    if (out_error) {
        *out_error = value;
    }
}

BookDocument* book_open(const char *filepath, ErrorCode *out_error) {
    book_set_error(out_error, ERROR_NONE);
    if (!filepath) {
        book_set_error(out_error, ERROR_FILE_NOT_FOUND);
        return NULL;
    }

    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!ctx) {
        book_set_error(out_error, ERROR_MEMORY_ALLOC);
        return NULL;
    }
    fz_set_warning_callback(ctx, book_mupdf_warn, NULL);

    fz_document *doc = NULL;
    gint page_count = 0;
    gboolean suppress_warnings = book_should_suppress_warnings(filepath);
    StderrSilencer silencer = {0};
    if (suppress_warnings) {
        book_stderr_silencer_begin(&silencer);
    }

    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, filepath);
        page_count = fz_count_pages(ctx, doc);
    }
    fz_catch(ctx) {
        if (silencer.active) {
            book_stderr_silencer_end(&silencer);
        }
        if (doc) {
            fz_drop_document(ctx, doc);
        }
        fz_drop_context(ctx);
        book_set_error(out_error, ERROR_INVALID_IMAGE);
        return NULL;
    }
    if (silencer.active) {
        book_stderr_silencer_end(&silencer);
    }

    if (!doc || page_count <= 0) {
        if (doc) {
            fz_drop_document(ctx, doc);
        }
        fz_drop_context(ctx);
        book_set_error(out_error, ERROR_INVALID_IMAGE);
        return NULL;
    }

    BookDocument *book = g_new0(BookDocument, 1);
    if (!book) {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        book_set_error(out_error, ERROR_MEMORY_ALLOC);
        return NULL;
    }

    book->ctx = ctx;
    book->doc = doc;
    book->page_count = page_count;
    book->path = g_strdup(filepath);
    book->suppress_stderr = suppress_warnings;

    return book;
}

void book_close(BookDocument *doc) {
    if (!doc) {
        return;
    }
    if (doc->doc) {
        fz_drop_document(doc->ctx, doc->doc);
        doc->doc = NULL;
    }
    if (doc->ctx) {
        fz_drop_context(doc->ctx);
        doc->ctx = NULL;
    }
    g_free(doc->path);
    g_free(doc);
}

const char* book_get_path(const BookDocument *doc) {
    return doc ? doc->path : NULL;
}

gint book_get_page_count(const BookDocument *doc) {
    return doc ? doc->page_count : 0;
}

static void book_reset_image(BookPageImage *image) {
    if (!image) {
        return;
    }
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
    image->stride = 0;
    image->channels = 0;
}

static gdouble book_compute_scale(gdouble page_w, gdouble page_h, gint target_px_w, gint target_px_h) {
    if (page_w <= 0.0 || page_h <= 0.0) {
        return 1.0;
    }

    gdouble scale_w = target_px_w > 0 ? (gdouble)target_px_w / page_w : 1.0;
    gdouble scale_h = target_px_h > 0 ? (gdouble)target_px_h / page_h : 1.0;
    gdouble scale = scale_w < scale_h ? scale_w : scale_h;

    if (!isfinite(scale) || scale <= 0.0) {
        scale = 1.0;
    }

    return scale;
}

ErrorCode book_render_page(BookDocument *doc,
                           gint page_index,
                           gint target_cols,
                           gint target_rows,
                           BookPageImage *out_image) {
    if (!doc || !doc->ctx || !doc->doc || !out_image) {
        return ERROR_MEMORY_ALLOC;
    }

    book_reset_image(out_image);

    if (page_index < 0 || page_index >= doc->page_count) {
        return ERROR_INVALID_IMAGE;
    }

    gint cell_w = 0, cell_h = 0;
    get_terminal_cell_geometry(&cell_w, &cell_h);
    if (cell_w <= 0) cell_w = 10;
    if (cell_h <= 0) cell_h = 20;
    if (target_cols < 1) target_cols = 1;
    if (target_rows < 1) target_rows = 1;

    gint target_px_w = target_cols * cell_w;
    gint target_px_h = target_rows * cell_h;
    if (target_px_w < 1) target_px_w = 1;
    if (target_px_h < 1) target_px_h = 1;

    fz_context *ctx = doc->ctx;
    fz_page *page = NULL;
    fz_pixmap *pix = NULL;
    fz_device *dev = NULL;

    ErrorCode status = ERROR_NONE;
    StderrSilencer silencer = {0};
    if (doc->suppress_stderr) {
        book_stderr_silencer_begin(&silencer);
    }

    fz_try(ctx) {
        page = fz_load_page(ctx, doc->doc, page_index);
        fz_rect bounds = fz_bound_page(ctx, page);
        gdouble page_w = bounds.x1 - bounds.x0;
        gdouble page_h = bounds.y1 - bounds.y0;

        gdouble scale = book_compute_scale(page_w, page_h, target_px_w, target_px_h);
        gdouble scaled_w = page_w * scale;
        gdouble scaled_h = page_h * scale;
        const gdouble max_dim = 4096.0;
        if (scaled_w > max_dim || scaled_h > max_dim) {
            gdouble descale = scaled_w / max_dim;
            gdouble descale_h = scaled_h / max_dim;
            if (descale_h > descale) {
                descale = descale_h;
            }
            if (descale > 1.0) {
                scale /= descale;
            }
        }

        fz_matrix ctm = fz_scale((float)scale, (float)scale);
        fz_rect rect = fz_transform_rect(bounds, ctm);
        fz_irect bbox = fz_round_rect(rect);

        pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), bbox, NULL, 1);
        fz_clear_pixmap_with_value(ctx, pix, 0xFF);

        dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_page(ctx, page, dev, ctm, NULL);
        fz_close_device(ctx, dev);

        if (pix->n != 3 && pix->n != 4) {
            status = ERROR_INVALID_IMAGE;
            fz_drop_device(ctx, dev);
            dev = NULL;
            fz_drop_pixmap(ctx, pix);
            pix = NULL;
            fz_drop_page(ctx, page);
            page = NULL;
            fz_throw(ctx, FZ_ERROR_GENERIC, "Unsupported pixmap format");
        }

        gsize bytes = (gsize)pix->stride * (gsize)pix->h;
        guint8 *buffer = g_malloc(bytes);
        if (!buffer) {
            status = ERROR_MEMORY_ALLOC;
            fz_drop_device(ctx, dev);
            dev = NULL;
            fz_drop_pixmap(ctx, pix);
            pix = NULL;
            fz_drop_page(ctx, page);
            page = NULL;
            fz_throw(ctx, FZ_ERROR_GENERIC, "Allocation failure");
        }
        memcpy(buffer, pix->samples, bytes);

        out_image->pixels = buffer;
        out_image->width = pix->w;
        out_image->height = pix->h;
        out_image->stride = pix->stride;
        out_image->channels = pix->n;
    }
    fz_catch(ctx) {
        if (silencer.active) {
            book_stderr_silencer_end(&silencer);
        }
        if (status == ERROR_NONE) {
            status = ERROR_INVALID_IMAGE;
        }
    }
    if (silencer.active) {
        book_stderr_silencer_end(&silencer);
    }

    if (dev) {
        fz_drop_device(ctx, dev);
    }
    if (pix) {
        fz_drop_pixmap(ctx, pix);
    }
    if (page) {
        fz_drop_page(ctx, page);
    }

    if (status != ERROR_NONE) {
        book_page_image_free(out_image);
    }

    return status;
}

void book_page_image_free(BookPageImage *image) {
    if (!image) {
        return;
    }
    if (image->pixels) {
        g_free(image->pixels);
    }
    book_reset_image(image);
}

static void book_outline_to_list(BookDocument *doc,
                                 fz_outline *outline,
                                 gint level,
                                 BookTocItem **head,
                                 BookTocItem **tail,
                                 gint *count) {
    if (!head || !tail || !count) {
        return;
    }
    while (outline) {
        BookTocItem *item = g_new0(BookTocItem, 1);
        item->title = g_strdup(outline->title ? outline->title : "");
        item->level = level;

        gint resolved_page = 0;
        fz_try(doc->ctx) {
            fz_location loc = fz_resolve_link(doc->ctx, doc->doc, outline->uri, NULL, NULL);
            resolved_page = fz_page_number_from_location(doc->ctx, doc->doc, loc);
        }
        fz_catch(doc->ctx) {
            resolved_page = 0;
        }
        if (resolved_page < 0) resolved_page = 0;
        item->page = resolved_page;

        item->next = NULL;

        if (*tail) {
            (*tail)->next = item;
        } else {
            *head = item;
        }
        *tail = item;
        (*count)++;

        if (outline->down) {
            book_outline_to_list(doc, outline->down, level + 1, head, tail, count);
        }

        outline = outline->next;
    }
}

BookToc* book_load_toc(BookDocument *doc) {
    if (!doc || !doc->ctx || !doc->doc) {
        return NULL;
    }

    fz_outline *outline = NULL;
    fz_try(doc->ctx) {
        outline = fz_load_outline(doc->ctx, doc->doc);
    }
    fz_catch(doc->ctx) {
        return NULL;
    }

    if (!outline) {
        return NULL;
    }

    BookToc *toc = g_new0(BookToc, 1);
    toc->items = NULL;
    toc->count = 0;

    BookTocItem *tail = NULL;
    book_outline_to_list(doc, outline, 0, &toc->items, &tail, &toc->count);

    fz_drop_outline(doc->ctx, outline);
    return toc;
}

void book_toc_free(BookToc *toc) {
    if (!toc) return;

    BookTocItem *item = toc->items;
    while (item) {
        BookTocItem *next = item->next;
        g_free(item->title);
        g_free(item);
        item = next;
    }
    g_free(toc);
}

#else

struct BookDocument {
    gchar *path;
    gint page_count;
};

static void book_set_error(ErrorCode *out_error, ErrorCode value) {
    if (out_error) {
        *out_error = value;
    }
}

static void book_reset_image(BookPageImage *image) {
    if (!image) {
        return;
    }
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
    image->stride = 0;
    image->channels = 0;
}

BookDocument* book_open(const char *filepath, ErrorCode *out_error) {
    book_set_error(out_error, ERROR_INVALID_IMAGE);
    (void)filepath;
    return NULL;
}

void book_close(BookDocument *doc) {
    if (!doc) {
        return;
    }
    g_free(doc->path);
    g_free(doc);
}

const char* book_get_path(const BookDocument *doc) {
    return doc ? doc->path : NULL;
}

gint book_get_page_count(const BookDocument *doc) {
    return doc ? doc->page_count : 0;
}

ErrorCode book_render_page(BookDocument *doc,
                           gint page_index,
                           gint target_cols,
                           gint target_rows,
                           BookPageImage *out_image) {
    (void)doc;
    (void)page_index;
    (void)target_cols;
    (void)target_rows;
    book_reset_image(out_image);
    return ERROR_INVALID_IMAGE;
}

void book_page_image_free(BookPageImage *image) {
    book_reset_image(image);
}

#endif
