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
/* Main context only; reserves an invisible pixel using a terminal-assigned ID.
 * Returns 0 on unsupported transport, malformed reply or timeout. */
guint32 kitty_graphics_allocate_image_id(void);
/* Main context only. Deferred deletes outlive players and retain ownership until
 * a checked write/flush succeeds. Retry from the event loop and before allocation. */
void kitty_graphics_defer_image_delete(guint32 image_id);
gboolean kitty_graphics_retry_pending_deletes(void);

/* Bound direct pixel data before base64/command allocations in each worker. */
#define KITTY_GRAPHICS_DIRECT_MAX_BYTES (8 * 1024 * 1024)
/* Fits without upscaling, preserving aspect to pixel rounding; nonpositive bounds are unknown.
 * A nonpositive source width or height produces 0x0 output. */
void kitty_graphics_get_direct_size(gint width, gint height, gint64 bound_width,
                                    gint64 bound_height, gint *width_out, gint *height_out);

KittyGraphicsFrame *kitty_graphics_frame_new_shm_rgba(const guint8 *pixels,
                                                       gint width,
                                                       gint height,
                                                       gint rowstride,
                                                       gint display_width_cells,
                                                       gint display_height_cells);
/* image_id must be nonzero; a=T replaces that image and displays its new data. */
KittyGraphicsFrame *kitty_graphics_frame_new_direct_rgb(const guint8 *pixels,
                                                        gint width,
                                                        gint height,
                                                        gint rowstride,
                                                        gint display_width_cells,
                                                        gint display_height_cells,
                                                        guint32 image_id);
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
