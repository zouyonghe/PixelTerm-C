#ifndef GIF_PLAYER_TEST_INTERNAL_H
#define GIF_PLAYER_TEST_INTERNAL_H

#include "gif_player.h"

typedef gboolean (*GifPlayerAdvanceHook)(GdkPixbufAnimationIter *iter,
                                         gconstpointer current_time);

void gif_player_present_rendered_frame_for_test(GifPlayer *player,
                                                const GString *rendered,
                                                gint rendered_width,
                                                gint rendered_height,
                                                gboolean graphics_mode);
guint gif_player_count_gif_frames_for_test(const guint8 *data, gsize length);
guint gif_player_count_webp_frames_for_test(const guint8 *data, gsize length);
gboolean gif_player_render_next_frame_for_test(GifPlayer *player);
gboolean gif_player_native_geometry_matches_for_test(const GifPlayer *player,
                                                     gint width,
                                                     gint height);
void gif_player_set_advance_hook_for_test(GifPlayerAdvanceHook hook);

#endif
