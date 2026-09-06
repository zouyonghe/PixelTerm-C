#ifndef GIF_PLAYER_TEST_INTERNAL_H
#define GIF_PLAYER_TEST_INTERNAL_H

#include "gif_player.h"

void gif_player_present_rendered_frame_for_test(GifPlayer *player,
                                                const GString *rendered,
                                                gint rendered_width,
                                                gint rendered_height,
                                                gboolean graphics_mode);
guint gif_player_count_gif_frames_for_test(const guint8 *data, gsize length);
gboolean gif_player_render_next_frame_for_test(GifPlayer *player);

#endif
