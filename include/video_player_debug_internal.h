#ifndef VIDEO_PLAYER_DEBUG_INTERNAL_H
#define VIDEO_PLAYER_DEBUG_INTERNAL_H

#include <stdint.h>
#include <stdio.h>

#include "video_player.h"

typedef enum {
    VIDEO_PLAYER_TEST_QUEUE_RENDER,
    VIDEO_PLAYER_TEST_QUEUE_DECODE,
} VideoPlayerTestQueueKind;

typedef void (*VideoPlayerQueueWaitHook)(void *user_data);
typedef int (*VideoPlayerSeekHook)(struct AVFormatContext *format_context,
                                   int stream_index,
                                   int64_t timestamp,
                                   int flags);

gboolean video_player_debug_enabled(void);
FILE *video_player_debug_get_stream(void);
gboolean video_player_debug_has_current_stream_for_test(void);
gboolean video_player_debug_should_log_for_test(const gchar *event);
gboolean video_player_debug_should_write(const gchar *event);
void video_player_debug_reset_for_test(void);
void video_player_debug_close_stream(void);
void video_player_debug_write_log(const gchar *event,
                                  gint64 a,
                                  gint64 b,
                                  gint64 c,
                                  gint64 d,
                                  guint backlog,
                                  gint frame_delay,
                                  gint slow_level);

void video_player_set_queue_wait_hook_for_test(VideoPlayer *player,
                                               VideoPlayerTestQueueKind queue_kind,
                                               VideoPlayerQueueWaitHook hook,
                                               void *user_data);
void video_player_notify_queue_wait_hook(VideoPlayer *player, VideoPlayerTestQueueKind queue_kind);

void video_player_set_seek_hook_for_test(VideoPlayerSeekHook hook);
int video_player_seek_frame_with_test_hook(VideoPlayer *player, int64_t target_ts, int flags);

#endif
