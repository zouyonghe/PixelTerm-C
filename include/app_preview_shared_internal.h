#ifndef APP_PREVIEW_SHARED_INTERNAL_H
#define APP_PREVIEW_SHARED_INTERNAL_H

#include "app.h"
#include "grid_render.h"

gint app_preview_bottom_reserved_lines(const PixelTermApp *app);
gint app_preview_compute_vertical_offset(const PixelTermApp *app,
                                         const PreviewLayout *layout,
                                         gint start_row,
                                         gint end_row);

gboolean app_grid_get_cell_origin(const PreviewLayout *layout,
                                  gint index,
                                  gint total_items,
                                  gint start_row,
                                  gint vertical_offset,
                                  gint *cell_x,
                                  gint *cell_y);
void app_grid_clear_cell_border(const PreviewLayout *layout, gint cell_x, gint cell_y);
void app_grid_draw_cell_border(const PreviewLayout *layout,
                               gint cell_x,
                               gint cell_y,
                               const char *border_style);

ImageRenderer* app_create_grid_renderer(const PixelTermApp *app,
                                        gint content_width,
                                        gint content_height,
                                        ErrorCode *out_error);
void app_draw_grid_cell_background(const PreviewLayout *layout,
                                   gint cell_x,
                                   gint cell_y,
                                   gboolean use_border,
                                   const char *border_style);
void app_draw_rendered_lines(gint content_x,
                             gint content_y,
                             gint content_width,
                             gint content_height,
                             const GString *rendered);

#endif
