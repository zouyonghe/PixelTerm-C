#ifndef APP_CORE_H
#define APP_CORE_H

#include "app_state.h"

PixelTermApp* app_create(void);
void app_destroy(PixelTermApp *app);
ErrorCode app_initialize(PixelTermApp *app, gboolean dither_enabled);
ErrorCode app_load_directory(PixelTermApp *app, const char *directory);
ErrorCode app_load_single_file(PixelTermApp *app, const char *filepath);

ErrorCode app_next_image(PixelTermApp *app);
ErrorCode app_previous_image(PixelTermApp *app);
ErrorCode app_goto_image(PixelTermApp *app, gint index);
ErrorCode app_switch_to_next_directory(PixelTermApp *app);
ErrorCode app_switch_to_parent_directory(PixelTermApp *app);

ErrorCode app_delete_current_image(PixelTermApp *app);

gint app_get_current_index(const PixelTermApp *app);
gint app_get_total_images(const PixelTermApp *app);
const gchar* app_get_current_filepath(const PixelTermApp *app);
gboolean app_has_images(const PixelTermApp *app);

#endif // APP_CORE_H
