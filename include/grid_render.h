#ifndef GRID_RENDER_H
#define GRID_RENDER_H

#include <glib.h>

typedef struct {
    gint cols;
    gint rows;
    gint cell_width;
    gint cell_height;
    gint header_lines;
    gint visible_rows;
} PreviewLayout;

typedef enum {
    GRID_RENDER_CONTINUE = 0,
    GRID_RENDER_STOP_ROW,
    GRID_RENDER_STOP_ALL
} GridRenderResult;

typedef struct {
    const PreviewLayout *layout;
    gint start_row;
    gint end_row;
    gint vertical_offset;
    gint content_width;
    gint content_height;
    gint total_items;
    gint selected_index;
} GridRenderContext;

typedef struct {
    gint index;
    gint cell_x;
    gint cell_y;
    gint content_x;
    gint content_y;
    gboolean selected;
    gboolean use_border;
} GridRenderCell;

typedef GridRenderResult (*GridRenderCellFn)(const GridRenderContext *context,
                                             const GridRenderCell *cell,
                                             void *userdata);

void grid_render_cells(const GridRenderContext *context,
                       GridRenderCellFn callback,
                       void *userdata);

#endif
