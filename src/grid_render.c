#include "grid_render.h"

void grid_render_cells(const GridRenderContext *context,
                       GridRenderCellFn callback,
                       void *userdata) {
    if (!context || !context->layout || !callback) {
        return;
    }

    const PreviewLayout *layout = context->layout;
    for (gint row = context->start_row; row < context->end_row; row++) {
        for (gint col = 0; col < layout->cols; col++) {
            gint idx = row * layout->cols + col;
            if (idx >= context->total_items) {
                break;
            }

            GridRenderCell cell = {
                .index = idx,
                .cell_x = col * layout->cell_width + 1,
                .cell_y = layout->header_lines + context->vertical_offset +
                          (row - context->start_row) * layout->cell_height + 1,
                .selected = (idx == context->selected_index),
                .use_border = (idx == context->selected_index) &&
                              layout->cell_width >= 4 &&
                              layout->cell_height >= 4
            };
            cell.content_x = cell.cell_x + 1;
            cell.content_y = cell.cell_y + 1;

            GridRenderResult result = callback(context, &cell, userdata);
            if (result == GRID_RENDER_STOP_ALL) {
                return;
            }
            if (result == GRID_RENDER_STOP_ROW) {
                break;
            }
        }
    }
}
