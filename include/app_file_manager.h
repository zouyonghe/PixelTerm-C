#ifndef APP_FILE_MANAGER_H
#define APP_FILE_MANAGER_H

#include "app_state.h"

ErrorCode app_enter_file_manager(PixelTermApp *app);
ErrorCode app_exit_file_manager(PixelTermApp *app);
ErrorCode app_file_manager_up(PixelTermApp *app);
ErrorCode app_file_manager_down(PixelTermApp *app);
ErrorCode app_file_manager_left(PixelTermApp *app);
ErrorCode app_file_manager_right(PixelTermApp *app);
ErrorCode app_file_manager_enter(PixelTermApp *app);
ErrorCode app_file_manager_jump_to_letter(PixelTermApp *app, char letter);
ErrorCode app_file_manager_refresh(PixelTermApp *app);
ErrorCode app_file_manager_select_path(PixelTermApp *app, const char *path);
ErrorCode app_file_manager_toggle_hidden(PixelTermApp *app);
gboolean app_file_manager_selection_is_image(PixelTermApp *app);
gint app_file_manager_get_selected_image_index(PixelTermApp *app);
gboolean app_file_manager_has_images(PixelTermApp *app);
ErrorCode app_render_file_manager(PixelTermApp *app);
ErrorCode app_handle_mouse_file_manager(PixelTermApp *app, gint mouse_x, gint mouse_y);
ErrorCode app_file_manager_enter_at_position(PixelTermApp *app, gint mouse_x, gint mouse_y);

#endif // APP_FILE_MANAGER_H
