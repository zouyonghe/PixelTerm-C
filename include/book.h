#ifndef BOOK_H
#define BOOK_H

#include "common.h"

typedef struct BookDocument BookDocument;

typedef struct {
    guint8 *pixels;
    gint width;
    gint height;
    gint stride;
    gint channels;
} BookPageImage;

typedef struct BookTocItem {
    gchar *title;
    gint page;
    gint level;
    struct BookTocItem *next;
} BookTocItem;

typedef struct {
    BookTocItem *items;
    gint count;
} BookToc;

BookDocument* book_open(const char *filepath, ErrorCode *out_error);
void book_close(BookDocument *doc);

const char* book_get_path(const BookDocument *doc);
gint book_get_page_count(const BookDocument *doc);

ErrorCode book_render_page(BookDocument *doc,
                           gint page_index,
                           gint target_cols,
                           gint target_rows,
                           BookPageImage *out_image);
void book_page_image_free(BookPageImage *image);

BookToc* book_load_toc(BookDocument *doc);
void book_toc_free(BookToc *toc);

#endif
