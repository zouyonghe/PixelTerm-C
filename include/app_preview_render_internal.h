#ifndef APP_PREVIEW_RENDER_INTERNAL_H
#define APP_PREVIEW_RENDER_INTERNAL_H

#include "app.h"
#include "grid_render.h"
#include "renderer.h"

const gchar *app_preview_get_selected_filepath(PixelTermApp *app);

void app_preview_render_cells(const GridRenderContext *context,
                              PixelTermApp *app,
                              ImageRenderer *renderer,
                              GList *cursor);
void app_preview_render_selected_filename(PixelTermApp *app);
void app_preview_draw_cell_border(const PixelTermApp *app,
                                  const PreviewLayout *layout,
                                  gint index,
                                  gint start_row,
                                  gint vertical_offset);
void app_preview_clear_cell_border(const PixelTermApp *app,
                                   const PreviewLayout *layout,
                                   gint index,
                                   gint start_row,
                                   gint vertical_offset);

#endif
