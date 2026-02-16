#ifndef APP_BOOK_MODE_H
#define APP_BOOK_MODE_H

#include "app_state.h"

ErrorCode app_open_book(PixelTermApp *app, const char *filepath);
void app_close_book(PixelTermApp *app);
ErrorCode app_enter_book_preview(PixelTermApp *app);
ErrorCode app_enter_book_page(PixelTermApp *app, gint page_index);
ErrorCode app_render_book_preview(PixelTermApp *app);
ErrorCode app_render_book_preview_selection_change(PixelTermApp *app, gint old_index);
ErrorCode app_render_book_page(PixelTermApp *app);

ErrorCode app_render_book_toc(PixelTermApp *app);
ErrorCode app_book_toc_move_selection(PixelTermApp *app, gint delta);
ErrorCode app_book_toc_page_move(PixelTermApp *app, gint direction);
ErrorCode app_book_toc_sync_to_page(PixelTermApp *app, gint page_index);
gint app_book_toc_get_selected_page(PixelTermApp *app);
ErrorCode app_handle_mouse_click_book_toc(PixelTermApp *app,
                                          gint mouse_x,
                                          gint mouse_y,
                                          gboolean *redraw_needed,
                                          gboolean *out_hit);

void app_book_jump_render_prompt(PixelTermApp *app);
void app_book_jump_clear_prompt(PixelTermApp *app);
gboolean app_book_use_double_page(const PixelTermApp *app);

#endif // APP_BOOK_MODE_H
