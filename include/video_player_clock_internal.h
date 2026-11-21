#ifndef VIDEO_PLAYER_CLOCK_INTERNAL_H
#define VIDEO_PLAYER_CLOCK_INTERNAL_H

#include "video_player.h"

gint video_player_frame_delay_from_rate(gint numerator, gint denominator);
gint64 video_player_smooth_pts_ms(VideoPlayer *player, gint64 raw_pts_ms, gint frame_delay);

void video_player_set_fallback_pts_ms(VideoPlayer *player, gint64 pts_ms);
gint64 video_player_resolve_and_advance_fallback_pts_ms(VideoPlayer *player,
                                                        gint64 raw_pts_ms,
                                                        gint frame_delay,
                                                        gint64 *next_fallback_pts_ms);
gint64 video_player_current_position_ms(VideoPlayer *player);
/* Returns FALSE until the playback clock starts. */
gboolean video_player_get_target_pts_ms(VideoPlayer *player, gint64 *target_pts_ms);
void video_player_reset_timing_state(VideoPlayer *player);
void video_player_restore_paused_seek_target(VideoPlayer *player, gint64 target_ms);

#endif
