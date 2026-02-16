#ifndef APP_PREVIEW_H
#define APP_PREVIEW_H

#include "app_state.h"

ErrorCode app_enter_preview(PixelTermApp *app);
ErrorCode app_exit_preview(PixelTermApp *app, gboolean open_selected);
ErrorCode app_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col);
ErrorCode app_preview_change_zoom(PixelTermApp *app, gint delta);
ErrorCode app_preview_page_move(PixelTermApp *app, gint direction);
ErrorCode app_render_preview_grid(PixelTermApp *app);
ErrorCode app_render_preview_selection_change(PixelTermApp *app, gint old_index);
ErrorCode app_preview_print_info(PixelTermApp *app);
ErrorCode app_handle_mouse_click_preview(PixelTermApp *app,
                                         gint mouse_x,
                                         gint mouse_y,
                                         gboolean *redraw_needed,
                                         gboolean *out_hit);

ErrorCode app_book_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col);
ErrorCode app_book_preview_page_move(PixelTermApp *app, gint direction);
ErrorCode app_book_preview_change_zoom(PixelTermApp *app, gint delta);
ErrorCode app_book_preview_scroll_pages(PixelTermApp *app, gint direction);
ErrorCode app_book_preview_jump_to_page(PixelTermApp *app, gint page_index);
ErrorCode app_handle_mouse_click_book_preview(PixelTermApp *app,
                                              gint mouse_x,
                                              gint mouse_y,
                                              gboolean *redraw_needed,
                                              gboolean *out_hit);

#endif // APP_PREVIEW_H
