#ifndef KITTY_GRAPHICS_H
#define KITTY_GRAPHICS_H

#include "common.h"
#include "kitty_transfer.h"

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
KittyGraphicsFrame *kitty_graphics_animation_root_new_shm_rgba(const guint8 *pixels,
                                                               gint width,
                                                               gint height,
                                                               gint rowstride,
                                                               gint display_width_cells,
                                                               gint display_height_cells,
                                                               guint32 image_id);
KittyGraphicsFrame *kitty_graphics_animation_frame_new_shm_rgba(const guint8 *pixels,
                                                                gint width,
                                                                gint height,
                                                                gint rowstride,
                                                                gint display_width_cells,
                                                                gint display_height_cells,
                                                                guint32 image_id,
                                                                gint delay_ms);

void kitty_graphics_frame_free(KittyGraphicsFrame *frame);

void kitty_graphics_shm_unlink(const gchar *shm_name);

GString *kitty_graphics_build_shm_command(const gchar *shm_name,
                                          gint width,
                                          gint height,
                                          gint display_width_cells,
                                          gint display_height_cells,
                                          gsize payload_size);

GString *kitty_graphics_build_animation_root_shm_command(const gchar *shm_name,
                                                         gint width,
                                                         gint height,
                                                         gint display_width_cells,
                                                         gint display_height_cells,
                                                         gsize payload_size,
                                                         guint32 image_id);
GString *kitty_graphics_build_animation_frame_shm_command(const gchar *shm_name,
                                                          gint width,
                                                          gint height,
                                                          gsize payload_size,
                                                          guint32 image_id,
                                                          gint delay_ms);
GString *kitty_graphics_build_animation_frame_delay_command(guint32 image_id,
                                                            guint32 frame_number,
                                                            gint delay_ms);
GString *kitty_graphics_build_animation_loading_command(guint32 image_id);
GString *kitty_graphics_build_animation_play_command(guint32 image_id);
GString *kitty_graphics_build_animation_stop_command(guint32 image_id);
GString *kitty_graphics_build_delete_image_command(guint32 image_id);

#endif // KITTY_GRAPHICS_H
