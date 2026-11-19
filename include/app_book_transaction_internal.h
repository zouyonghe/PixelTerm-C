#ifndef APP_BOOK_TRANSACTION_INTERNAL_H
#define APP_BOOK_TRANSACTION_INTERNAL_H

#include "app.h"

/* Keep the previous book alive until entry and the first render both succeed. */
static inline ErrorCode app_open_book_preview(PixelTermApp *app, const char *path) {
    PixelTermApp staged = {0};
    ErrorCode error = app_open_book(&staged, path);
    if (error != ERROR_NONE) return error;
    BookState previous = app->book;
    gchar *directory = app->current_directory;
    AppMode mode = app->mode;
    gboolean info_visible = app->info_visible, help_visible = app->help_visible;
    /* Move staged ownership into app while entry checks the real application. */
    app->book = staged.book;
    app->current_directory = staged.current_directory;
    staged.book = (BookState){0};
    staged.current_directory = NULL;
    error = app_enter_book_preview(app);
    if (error == ERROR_NONE) error = app_render_book_preview(app);
    if (error != ERROR_NONE) {
        staged.book = app->book;
        staged.current_directory = app->current_directory;
        app->book = previous;
        app->current_directory = directory;
        app_set_mode(app, mode);
        app->info_visible = info_visible;
        app->help_visible = help_visible;
    } else {
        staged.book = previous;
        staged.current_directory = directory;
    }
    app_close_book(&staged);
    g_free(staged.current_directory);
    return error;
}

#endif
