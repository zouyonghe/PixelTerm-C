#ifndef UI_RENDER_UTILS_H
#define UI_RENDER_UTILS_H

#include "app.h"

typedef struct {
    const char *key;
    const char *label;
} HelpSegment;

typedef struct {
    const char *left;
    const char *right;
} UIPanelRow;

typedef struct {
    const char *title;
    const char *const *lines;
    gsize line_count;
    const UIPanelRow *rows;
    gsize row_count;
    gint min_inner_width;
    gint max_inner_width;
} UIPanel;

gint ui_filename_max_width(const PixelTermApp *app);
void ui_render_centered_row(gint row, gint term_width, const char *text, const char *style);
gint ui_single_view_content_top_row(const PixelTermApp *app);
gint ui_single_view_bottom_reserved_lines(const PixelTermApp *app);
gint ui_preview_header_lines(const PixelTermApp *app);
void ui_print_centered_help_line(gint row, gint term_width, const HelpSegment *segments, gsize n);
void ui_begin_sync_update(void);
void ui_end_sync_update(void);
void ui_clear_screen_for_refresh(const PixelTermApp *app);
void ui_clear_kitty_images(const PixelTermApp *app);
void ui_clear_single_view_lines(const PixelTermApp *app);
void ui_clear_area(const PixelTermApp *app, gint top_row, gint height);
void ui_render_panel(gint term_width, gint term_height, const UIPanel *panel);

#endif
