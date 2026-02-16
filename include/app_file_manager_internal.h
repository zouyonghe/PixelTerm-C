#ifndef APP_FILE_MANAGER_INTERNAL_H
#define APP_FILE_MANAGER_INTERNAL_H

#include "app.h"
#include <glib.h>

gint app_file_manager_compare_names(gconstpointer a, gconstpointer b);
void app_file_manager_invalidate_selection_cache(PixelTermApp *app);
GList* app_file_manager_find_link_with_hint(const PixelTermApp *app,
                                            gint target_index,
                                            GList *hint_link,
                                            gint hint_index);
GList* app_file_manager_get_selected_node(PixelTermApp *app);
gchar* app_file_manager_display_name(const PixelTermApp *app, const gchar *entry, gboolean *is_directory);
void app_file_manager_layout(const PixelTermApp *app,
                             gint total_entries,
                             gint *col_width,
                             gint *cols,
                             gint *visible_rows,
                             gint *total_rows);
void app_file_manager_adjust_scroll(PixelTermApp *app,
                                    gint total_entries,
                                    gint cols,
                                    gint visible_rows);

#endif
