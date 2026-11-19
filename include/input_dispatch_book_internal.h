#ifndef INPUT_DISPATCH_BOOK_INTERNAL_H
#define INPUT_DISPATCH_BOOK_INTERNAL_H

#include "app.h"

ErrorCode input_dispatch_book_change_page(PixelTermApp *app, gint delta);

static inline ErrorCode input_dispatch_book_enter_and_render_page(PixelTermApp *app, gint page) {
    gint previous_page = app->book.page;
    ErrorCode result = app_enter_book_page(app, page);
    if (result == ERROR_NONE) result = app_render_book_page(app);
    if (result != ERROR_NONE) {
        app->book.page = previous_page;
        app->needs_redraw = TRUE;
        app->needs_screen_clear = TRUE;
    }
    return result;
}

#endif
