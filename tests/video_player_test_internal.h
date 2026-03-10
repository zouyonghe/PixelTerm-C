#ifndef TESTS_VIDEO_PLAYER_TEST_INTERNAL_H
#define TESTS_VIDEO_PLAYER_TEST_INTERNAL_H

#include <stdio.h>

#include "video_player.h"

/* Test-only declarations for video_player internals.
 * These are not part of the supported production API surface.
 */

void decoded_frame_destroy(DecodedFrame *frame);
void video_frame_destroy(RenderedFrame *frame);

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
DecodedFrame *video_player_decode_queue_wait_and_take(VideoPlayer *player);

gboolean video_player_should_drop_late_frame(VideoPlayer *player, gint64 pts_ms);
gint video_player_calc_delay_ms(VideoPlayer *player);
void video_player_update_queue_depth(VideoPlayer *player, gint rendered_w, gint rendered_h);
gint64 video_player_current_position_ms_for_test(VideoPlayer *player);
gint64 video_player_seek_target_ms_for_test(VideoPlayer *player, gint64 delta_ms, gint64 duration_ms);

gboolean video_player_debug_enabled(void);
FILE *video_player_debug_get_stream(void);
gboolean video_player_debug_has_current_stream_for_test(void);
gboolean video_player_debug_should_log_for_test(const gchar *event);
void video_player_debug_reset_for_test(void);

typedef enum {
    VIDEO_PLAYER_TEST_QUEUE_RENDER,
    VIDEO_PLAYER_TEST_QUEUE_DECODE,
} VideoPlayerTestQueueKind;

typedef void (*VideoPlayerQueueWaitHook)(void *user_data);
void video_player_set_queue_wait_hook_for_test(VideoPlayer *player,
                                               VideoPlayerTestQueueKind queue_kind,
                                               VideoPlayerQueueWaitHook hook,
                                               void *user_data);

#endif
