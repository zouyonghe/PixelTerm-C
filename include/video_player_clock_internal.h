#ifndef VIDEO_PLAYER_CLOCK_INTERNAL_H
#define VIDEO_PLAYER_CLOCK_INTERNAL_H

#include "video_player.h"

void video_player_set_fallback_pts_ms(VideoPlayer *player, gint64 pts_ms);
gint64 video_player_resolve_and_advance_fallback_pts_ms(VideoPlayer *player,
                                                        gint64 raw_pts_ms,
                                                        gint frame_delay,
                                                        gint64 *next_fallback_pts_ms);
gint64 video_player_current_position_ms(VideoPlayer *player);

#endif
