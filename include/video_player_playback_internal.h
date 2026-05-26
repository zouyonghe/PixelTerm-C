#ifndef VIDEO_PLAYER_PLAYBACK_INTERNAL_H
#define VIDEO_PLAYER_PLAYBACK_INTERNAL_H

#include "video_player.h"

/*
 * Internal playback-state module — owns is_playing, EOF flags, clock/timing,
 * playback generation, and timing-derived helpers.
 *
 * Functions in this module access only the playback/state fields:
 * is_playing, has_video, draining (read-only here, see decode for write helpers),
 * eof_pending, eof_ended, frame_delay_ms, time_base_num/den, clock_start_us,
 * clock_start_pts_ms, fallback_pts_ms, clock_started, rewind_needs_resync,
 * smooth_*, timer_id, playback_generation. They also peek the frame queue length
 * (read-only over queue_mutex) for delay-derivation purposes.
 *
 * Threading: every accessor takes player->state_mutex internally so callers
 * may invoke them without holding the mutex (and MUST NOT hold it).
 */

/* Playback generation (atomic counter) */
guint video_player_generation_get(VideoPlayer *player);
guint video_player_generation_bump(VideoPlayer *player);

/* Per-frame timing */
gint video_player_get_frame_delay_ms(VideoPlayer *player);

/* EOF state */
gboolean video_player_is_eof_pending(VideoPlayer *player);
gboolean video_player_is_eof_ended(VideoPlayer *player);

/* Returns FALSE if clock is not started; otherwise sets *target_pts_ms to
 * the wall-clock-derived target PTS in ms. */
gboolean video_player_get_target_pts_ms(VideoPlayer *player, gint64 *target_pts_ms);

/* Frame queue helpers used by the playback timing logic */
guint video_player_queue_length(VideoPlayer *player);
gboolean video_player_queue_peek_pts_ms(VideoPlayer *player, gint64 *pts_ms);

/* Calc next tick delay, ms */
gint video_player_calc_delay_ms(VideoPlayer *player);

/* Slow level / late window — derived from frame_delay_ms (playback) and
 * io_avg_ms (read-only access). Used to widen/tighten the late-frame
 * threshold and to gate "should we drop?" decisions. */
gint video_player_get_slow_level(VideoPlayer *player);
gint64 video_player_calc_late_window_ms(VideoPlayer *player, gint multiplier, gint min_ms);

/* Should this frame be dropped because it's too late? */
gboolean video_player_should_drop_late_frame(VideoPlayer *player, gint64 pts_ms);

#endif /* VIDEO_PLAYER_PLAYBACK_INTERNAL_H */
