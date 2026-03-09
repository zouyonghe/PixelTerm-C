#ifndef VIDEO_PLAYER_INTERNAL_H
#define VIDEO_PLAYER_INTERNAL_H

#include <stdio.h>

#include "video_player.h"

typedef RenderedFrame VideoFrame;

void decoded_frame_destroy(DecodedFrame *frame);
void video_frame_destroy(VideoFrame *frame);

void video_player_reset_timing_state(VideoPlayer *player);

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

gboolean video_player_should_drop_late_frame(VideoPlayer *player, gint64 pts_ms);
gint video_player_calc_delay_ms(VideoPlayer *player);
void video_player_update_queue_depth(VideoPlayer *player, gint rendered_w, gint rendered_h);

gboolean video_player_debug_enabled(void);
FILE *video_player_debug_get_stream(void);
FILE *video_player_debug_current_stream_for_test(void);
void video_player_debug_reset_for_test(void);

#endif
