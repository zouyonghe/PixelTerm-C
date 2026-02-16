#ifndef UI_RENDER_UTILS_H
#define UI_RENDER_UTILS_H

#include "app.h"

typedef struct {
    const char *key;
    const char *label;
} HelpSegment;

gint ui_filename_max_width(const PixelTermApp *app);
void ui_print_centered_help_line(gint row, gint term_width, const HelpSegment *segments, gsize n);
void ui_begin_sync_update(void);
void ui_end_sync_update(void);
void ui_clear_screen_for_refresh(const PixelTermApp *app);
void ui_clear_kitty_images(const PixelTermApp *app);
void ui_clear_single_view_lines(const PixelTermApp *app);
void ui_clear_area(const PixelTermApp *app, gint top_row, gint height);

#endif
