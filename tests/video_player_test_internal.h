#ifndef TESTS_VIDEO_PLAYER_TEST_INTERNAL_H
#define TESTS_VIDEO_PLAYER_TEST_INTERNAL_H

#include <stdio.h>

#include "video_player.h"
#include "video_player_debug_internal.h"
#include "video_player_seek_internal.h"

/* Test-only declarations for video_player internals.
 * These are not part of the supported production API surface.
 */

void decoded_frame_destroy(DecodedFrame *frame);
void video_frame_destroy(RenderedFrame *frame);

void video_player_reset_timing_state(VideoPlayer *player);
void video_player_set_fallback_pts_ms(VideoPlayer *player, gint64 pts_ms);
gint64 video_player_resolve_and_advance_fallback_pts_ms(VideoPlayer *player,
                                                        gint64 raw_pts_ms,
                                                        gint frame_delay,
                                                        gint64 *next_fallback_pts_ms);

void video_player_queue_clear(VideoPlayer *player);
void video_player_queue_push(VideoPlayer *player, RenderedFrame *frame);
RenderedFrame *video_player_queue_take_first(VideoPlayer *player);
void video_player_queue_insert_sorted(VideoPlayer *player, RenderedFrame *frame);
RenderedFrame *video_player_queue_take_for_playback(VideoPlayer *player,
                                                    gint64 target_pts_ms,
                                                    gint64 max_late_ms);

void video_player_decode_queue_clear(VideoPlayer *player);
void video_player_decode_queue_push(VideoPlayer *player, DecodedFrame *frame);
DecodedFrame *video_player_decode_queue_take(VideoPlayer *player);
DecodedFrame *video_player_decode_queue_wait_and_take(VideoPlayer *player);

gboolean video_player_should_drop_late_frame(VideoPlayer *player, gint64 pts_ms);
gint video_player_calc_delay_ms(VideoPlayer *player);
void video_player_update_queue_depth(VideoPlayer *player, gint rendered_w, gint rendered_h);
gint64 video_player_current_position_ms_for_test(VideoPlayer *player);
gint64 video_player_seek_target_ms_for_test(VideoPlayer *player, gint64 delta_ms, gint64 duration_ms);

#endif
