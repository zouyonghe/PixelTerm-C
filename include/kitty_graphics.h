#ifndef KITTY_GRAPHICS_H
#define KITTY_GRAPHICS_H

#include "common.h"
#include "video_player.h"

typedef struct {
    GString *command;
    gchar *shm_name;
    gint display_width_cells;
    gint display_height_cells;
} KittyGraphicsFrame;

gboolean kitty_graphics_shm_auto_enabled(void);
gboolean kitty_graphics_should_use_shm(KittyTransferMode mode);

KittyGraphicsFrame *kitty_graphics_frame_new_shm_rgba(const guint8 *pixels,
                                                       gint width,
                                                       gint height,
                                                       gint rowstride,
                                                       gint display_width_cells,
                                                       gint display_height_cells);

void kitty_graphics_frame_free(KittyGraphicsFrame *frame);

void kitty_graphics_shm_unlink(const gchar *shm_name);

GString *kitty_graphics_build_shm_command_for_test(const gchar *shm_name,
                                                   gint width,
                                                   gint height,
                                                   gint display_width_cells,
                                                   gint display_height_cells,
                                                   gsize payload_size);

#endif // KITTY_GRAPHICS_H
