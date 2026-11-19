#include "input_dispatch_book_internal.h"

void input_dispatch_book_change_page(PixelTermApp *app, gint delta) {
    if (!app_is_book_mode(app)) {
        return;
    }
    gint new_page = app->book.page + delta;
    if (new_page < 0) new_page = 0;
    if (new_page >= app->book.page_count) {
        new_page = app->book.page_count - 1;
    }
    if (new_page < 0) new_page = 0;
    if (new_page == app->book.page) {
        return;
    }
    app->suppress_full_clear = TRUE;
    ErrorCode result = app_enter_book_page(app, new_page);
    if (result == ERROR_NONE) result = app_render_book_page(app);
    if (result == ERROR_OUTPUT_IO) app->last_error = result;
}
