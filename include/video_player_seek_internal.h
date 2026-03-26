#ifndef VIDEO_PLAYER_SEEK_INTERNAL_H
#define VIDEO_PLAYER_SEEK_INTERNAL_H

#include "video_player.h"

typedef gboolean (*VideoPlayerSeekPreviewHook)(VideoPlayer *player, gint64 target_ms);

gint64 video_player_seek_target_ms(VideoPlayer *player, gint64 delta_ms, gint64 duration_ms);
gboolean video_player_render_seek_preview(VideoPlayer *player, gint64 target_ms);
void video_player_set_seek_preview_hook_for_test(VideoPlayerSeekPreviewHook hook);
void video_player_set_max_preview_decode_attempts_for_test(gint max_attempts);
gint video_player_get_max_preview_decode_attempts_for_test(void);

#endif
