#define _GNU_SOURCE

#include "video_player.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef RenderedFrame VideoFrame;

static gint video_player_get_slow_level(VideoPlayer *player);
static void video_player_debug_log(VideoPlayer *player,
                                   const gchar *event,
                                   gint64 a,
                                   gint64 b,
                                   gint64 c,
                                   gint64 d);
gint video_player_calc_delay_ms(VideoPlayer *player);
static gboolean video_player_tick(gpointer user_data);
static gint64 video_player_current_position_ms(VideoPlayer *player);
static gint64 video_player_seek_target_ms(VideoPlayer *player, gint64 delta_ms, gint64 duration_ms);
static gint video_player_live_instances = 0;

typedef void (*VideoPlayerQueueWaitHook)(void *user_data);
typedef int (*VideoPlayerSeekHook)(AVFormatContext *format_context,
                                   int stream_index,
                                   int64_t timestamp,
                                   int flags);
typedef gboolean (*VideoPlayerSeekPreviewHook)(VideoPlayer *player, gint64 target_ms);
typedef enum {
    VIDEO_PLAYER_TEST_QUEUE_RENDER,
    VIDEO_PLAYER_TEST_QUEUE_DECODE,
} VideoPlayerTestQueueKind;

enum {
    VIDEO_PLAYER_LATE_DROP_BACKLOG_THRESHOLD = 5,
    VIDEO_PLAYER_MAX_SILENCE_US = 1000000,
    VIDEO_PLAYER_QUEUE_DEPTH_MEDIUM_AREA = 1500,
    VIDEO_PLAYER_QUEUE_DEPTH_LARGE_AREA = 3000,
    VIDEO_PLAYER_QUEUE_DEPTH_LARGE_SIZE = 4,
    VIDEO_PLAYER_QUEUE_DEPTH_MEDIUM_SIZE = 6,
    VIDEO_PLAYER_QUEUE_DEPTH_SMALL_SIZE = 8
};

gboolean video_player_debug_has_current_stream_for_test(void);
gboolean video_player_debug_should_log_for_test(const gchar *event);
void video_player_set_queue_wait_hook_for_test(VideoPlayer *player,
                                               VideoPlayerTestQueueKind queue_kind,
                                               VideoPlayerQueueWaitHook hook,
                                               void *user_data);

static GMutex video_player_queue_wait_hook_mutex;
static VideoPlayer *video_player_queue_wait_hook_player = NULL;
static VideoPlayerTestQueueKind video_player_queue_wait_hook_kind = VIDEO_PLAYER_TEST_QUEUE_RENDER;
static VideoPlayerQueueWaitHook video_player_queue_wait_hook = NULL;
static void *video_player_queue_wait_hook_data = NULL;
static VideoPlayerSeekHook video_player_seek_hook = NULL;
static VideoPlayerSeekPreviewHook video_player_seek_preview_hook = NULL;
static const gint k_video_player_default_preview_decode_attempts = 64;
static const gint k_video_player_max_preview_receive_invaliddata_attempts = 8;
static gint video_player_max_preview_decode_attempts = -1;

static void video_player_notify_queue_wait_hook(VideoPlayer *player, VideoPlayerTestQueueKind queue_kind) {
    g_mutex_lock(&video_player_queue_wait_hook_mutex);
    VideoPlayer *hook_player = video_player_queue_wait_hook_player;
    VideoPlayerTestQueueKind hook_kind = video_player_queue_wait_hook_kind;
    VideoPlayerQueueWaitHook hook = video_player_queue_wait_hook;
    void *hook_data = video_player_queue_wait_hook_data;
    g_mutex_unlock(&video_player_queue_wait_hook_mutex);

    if (hook && hook_player == player && hook_kind == queue_kind) {
        hook(hook_data);
    }
}

void video_player_set_queue_wait_hook_for_test(VideoPlayer *player,
                                               VideoPlayerTestQueueKind queue_kind,
                                               VideoPlayerQueueWaitHook hook,
                                               void *user_data) {
    g_mutex_lock(&video_player_queue_wait_hook_mutex);
    video_player_queue_wait_hook_player = player;
    video_player_queue_wait_hook_kind = queue_kind;
    video_player_queue_wait_hook = hook;
    video_player_queue_wait_hook_data = user_data;
    g_mutex_unlock(&video_player_queue_wait_hook_mutex);
}

void video_player_set_seek_hook_for_test(VideoPlayerSeekHook hook) {
    video_player_seek_hook = hook;
}

void video_player_set_seek_preview_hook_for_test(VideoPlayerSeekPreviewHook hook) {
    video_player_seek_preview_hook = hook;
}

void video_player_set_max_preview_decode_attempts_for_test(gint max_attempts) {
    video_player_max_preview_decode_attempts = max_attempts;
}

gint video_player_get_max_preview_decode_attempts_for_test(void) {
    if (video_player_max_preview_decode_attempts >= 0) {
        return video_player_max_preview_decode_attempts;
    }
    return k_video_player_default_preview_decode_attempts;
}

static gint video_player_get_frame_delay_ms(VideoPlayer *player) {
    if (!player) {
        return 0;
    }

    g_mutex_lock(&player->state_mutex);
    gint frame_delay = player->frame_delay_ms;
    g_mutex_unlock(&player->state_mutex);
    return frame_delay;
}

static gint64 video_player_current_position_ms(VideoPlayer *player) {
    if (!player) {
        return 0;
    }

    gint64 last_presented_pts_ms = G_MININT64;
    gint64 fallback_pts_ms = 0;
    gboolean clock_started = FALSE;
    gint64 clock_start_us = 0;
    gint64 clock_start_pts_ms = 0;
    gboolean is_playing = FALSE;

    g_mutex_lock(&player->state_mutex);
    last_presented_pts_ms = player->last_presented_pts_ms;
    fallback_pts_ms = player->fallback_pts_ms;
    clock_started = player->clock_started;
    clock_start_us = player->clock_start_us;
    clock_start_pts_ms = player->clock_start_pts_ms;
    is_playing = player->is_playing;
    g_mutex_unlock(&player->state_mutex);

    if (last_presented_pts_ms != G_MININT64) {
        return last_presented_pts_ms < 0 ? 0 : last_presented_pts_ms;
    }

    if (!clock_started) {
        /* fallback_pts_ms is only assigned from known seek/decoded frame positions,
         * and sentinel states are represented by G_MININT64 in the PTS fields above. */
        return fallback_pts_ms < 0 ? 0 : fallback_pts_ms;
    }

    gint64 position_ms = clock_start_pts_ms;
    if (is_playing && clock_start_us > 0) {
        gint64 elapsed_us = g_get_monotonic_time() - clock_start_us;
        if (elapsed_us > 0) {
            position_ms += elapsed_us / 1000;
        }
    }

    return position_ms < 0 ? 0 : position_ms;
}

static gint64 video_player_seek_target_ms(VideoPlayer *player, gint64 delta_ms, gint64 duration_ms) {
    gint64 target_ms = video_player_current_position_ms(player) + delta_ms;
    if (target_ms < 0) {
        target_ms = 0;
    }
    if (duration_ms > 0 && target_ms > duration_ms) {
        target_ms = duration_ms;
    }
    return target_ms;
}

gint64 video_player_current_position_ms_for_test(VideoPlayer *player) {
    return video_player_current_position_ms(player);
}

gint64 video_player_seek_target_ms_for_test(VideoPlayer *player, gint64 delta_ms, gint64 duration_ms) {
    return video_player_seek_target_ms(player, delta_ms, duration_ms);
}

void video_player_reset_timing_state(VideoPlayer *player) {
    if (!player) {
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->clock_started = FALSE;
    player->rewind_needs_resync = FALSE;
    player->clock_start_us = 0;
    player->clock_start_pts_ms = 0;
    player->smooth_last_pts_ms = 0;
    player->smooth_pts_ms = 0;
    player->smooth_valid = FALSE;
    player->io_avg_ms = 0.0;
    player->io_avg_valid = FALSE;
    player->last_present_us = 0;
    player->last_presented_pts_ms = G_MININT64;
    player->present_fps = 0.0;
    player->present_fps_valid = FALSE;
    g_mutex_unlock(&player->state_mutex);

    player->fallback_pts_ms = 0;
    player->draining = FALSE;
}

static const char *video_player_pixel_mode_label(ChafaPixelMode mode) {
    switch (mode) {
        case CHAFA_PIXEL_MODE_KITTY:
            return "kitty";
        case CHAFA_PIXEL_MODE_ITERM2:
            return "iterm2";
        case CHAFA_PIXEL_MODE_SIXELS:
            return "sixel";
        case CHAFA_PIXEL_MODE_SYMBOLS:
        default:
            return "text";
    }
}

static void video_player_clear_line_cache(VideoPlayer *player) {
    if (!player || !player->last_frame_lines) {
        return;
    }
    g_ptr_array_free(player->last_frame_lines, TRUE);
    player->last_frame_lines = NULL;
}

static void video_player_update_io_avg(VideoPlayer *player, gint64 io_ms) {
    if (!player || io_ms < 0) {
        return;
    }
    const gdouble alpha = 0.2;
    g_mutex_lock(&player->state_mutex);
    if (!player->io_avg_valid) {
        player->io_avg_ms = (gdouble)io_ms;
        player->io_avg_valid = TRUE;
    } else {
        player->io_avg_ms = player->io_avg_ms * (1.0 - alpha) + (gdouble)io_ms * alpha;
    }
    g_mutex_unlock(&player->state_mutex);
}

static void video_player_update_present_fps(VideoPlayer *player, gint64 now_us) {
    if (!player || now_us <= 0) {
        return;
    }

    g_mutex_lock(&player->state_mutex);
    if (player->last_present_us > 0) {
        gint64 delta_us = now_us - player->last_present_us;
        if (delta_us > 0) {
            gdouble fps = 1000000.0 / (gdouble)delta_us;
            const gdouble alpha = 0.2;
            if (!player->present_fps_valid) {
                player->present_fps = fps;
                player->present_fps_valid = TRUE;
            } else {
                player->present_fps = player->present_fps * (1.0 - alpha) + fps * alpha;
            }
        }
    }
    player->last_present_us = now_us;
    g_mutex_unlock(&player->state_mutex);
}

void video_player_update_queue_depth(VideoPlayer *player, gint rendered_w, gint rendered_h) {
    if (!player) {
        return;
    }

    gint area = rendered_w * rendered_h;
    g_mutex_lock(&player->queue_mutex);
    if (area >= VIDEO_PLAYER_QUEUE_DEPTH_LARGE_AREA) {
        player->max_queue_size = VIDEO_PLAYER_QUEUE_DEPTH_LARGE_SIZE;
    } else if (area >= VIDEO_PLAYER_QUEUE_DEPTH_MEDIUM_AREA) {
        player->max_queue_size = VIDEO_PLAYER_QUEUE_DEPTH_MEDIUM_SIZE;
    } else {
        player->max_queue_size = VIDEO_PLAYER_QUEUE_DEPTH_SMALL_SIZE;
    }
    g_cond_broadcast(&player->frame_queue_has_space);
    g_mutex_unlock(&player->queue_mutex);
}

static guint video_player_generation_get(VideoPlayer *player) {
    if (!player) {
        return 0;
    }
    return (guint)g_atomic_int_get(&player->playback_generation);
}

static guint video_player_generation_bump(VideoPlayer *player) {
    if (!player) {
        return 0;
    }
    return (guint)(g_atomic_int_add(&player->playback_generation, 1) + 1);
}

static void video_player_schedule_tick(VideoPlayer *player) {
    if (!player || !player->is_playing) {
        return;
    }

    int delay = video_player_calc_delay_ms(player);
    video_player_debug_log(player, "tick-reschedule", delay, 0, 0, 0);
    if (delay < 1) {
        delay = 1;
    }

    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
    }
    player->timer_id = g_timeout_add(delay, video_player_tick, player);
}

void decoded_frame_destroy(DecodedFrame *frame) {
    if (!frame) {
        return;
    }
    g_free(frame->pixels);
    g_free(frame);
}

void video_frame_destroy(VideoFrame *frame) {
    if (!frame) {
        return;
    }
    if (frame->rendered) {
        g_string_free(frame->rendered, TRUE);
    }
    g_free(frame);
}

void video_player_queue_clear(VideoPlayer *player) {
    if (!player || !player->frame_queue) {
        return;
    }
    g_mutex_lock(&player->queue_mutex);
    while (!g_queue_is_empty(player->frame_queue)) {
        VideoFrame *frame = (VideoFrame *)g_queue_pop_head(player->frame_queue);
        video_frame_destroy(frame);
    }
    g_cond_broadcast(&player->frame_queue_has_space);
    g_mutex_unlock(&player->queue_mutex);
}

void video_player_decode_queue_clear(VideoPlayer *player) {
    if (!player || !player->decode_queue) {
        return;
    }
    g_mutex_lock(&player->queue_mutex);
    while (!g_queue_is_empty(player->decode_queue)) {
        DecodedFrame *frame = (DecodedFrame *)g_queue_pop_head(player->decode_queue);
        decoded_frame_destroy(frame);
    }
    g_cond_broadcast(&player->decode_queue_has_space);
    g_cond_broadcast(&player->decode_queue_has_items);
    g_mutex_unlock(&player->queue_mutex);
}

void video_player_queue_push(VideoPlayer *player, VideoFrame *frame) {
    if (!player || !player->frame_queue || !frame) {
        video_frame_destroy(frame);
        return;
    }

    gboolean logged_full = FALSE;
    g_mutex_lock(&player->queue_mutex);
    while (!player->worker_stop &&
           player->max_queue_size > 0 &&
           g_queue_get_length(player->frame_queue) >= (guint)player->max_queue_size) {
        if (!logged_full) {
            gint64 pts_ms = frame->pts_ms;
            g_mutex_unlock(&player->queue_mutex);
            video_player_debug_log(player, "worker-skip-full", pts_ms, 0, 0, 0);
            g_mutex_lock(&player->queue_mutex);
            logged_full = TRUE;
            continue;
        }
        video_player_notify_queue_wait_hook(player, VIDEO_PLAYER_TEST_QUEUE_RENDER);
        g_cond_wait(&player->frame_queue_has_space, &player->queue_mutex);
    }
    if (player->worker_stop) {
        g_mutex_unlock(&player->queue_mutex);
        video_frame_destroy(frame);
        return;
    }

    g_queue_push_tail(player->frame_queue, frame);
    g_mutex_unlock(&player->queue_mutex);
}

RenderedFrame *video_player_queue_take_first(VideoPlayer *player) {
    if (!player || !player->frame_queue) {
        return NULL;
    }
    g_mutex_lock(&player->queue_mutex);
    if (g_queue_is_empty(player->frame_queue)) {
        g_mutex_unlock(&player->queue_mutex);
        return NULL;
    }
    VideoFrame *frame = (VideoFrame *)g_queue_pop_head(player->frame_queue);
    g_cond_broadcast(&player->frame_queue_has_space);
    g_mutex_unlock(&player->queue_mutex);
    return frame;
}

void video_player_queue_insert_sorted(VideoPlayer *player, VideoFrame *frame) {
    if (!player || !player->frame_queue || !frame) {
        video_frame_destroy(frame);
        return;
    }

    gboolean logged_full = FALSE;
    g_mutex_lock(&player->queue_mutex);
    while (!player->worker_stop &&
           player->max_queue_size > 0 &&
           g_queue_get_length(player->frame_queue) >= (guint)player->max_queue_size) {
        if (!logged_full) {
            gint64 pts_ms = frame->pts_ms;
            g_mutex_unlock(&player->queue_mutex);
            video_player_debug_log(player, "worker-skip-full", pts_ms, 0, 0, 0);
            g_mutex_lock(&player->queue_mutex);
            logged_full = TRUE;
            continue;
        }
        video_player_notify_queue_wait_hook(player, VIDEO_PLAYER_TEST_QUEUE_RENDER);
        g_cond_wait(&player->frame_queue_has_space, &player->queue_mutex);
    }
    if (player->worker_stop) {
        g_mutex_unlock(&player->queue_mutex);
        video_frame_destroy(frame);
        return;
    }

    gint64 last_presented_pts_ms = G_MININT64;
    g_mutex_lock(&player->state_mutex);
    last_presented_pts_ms = player->last_presented_pts_ms;
    g_mutex_unlock(&player->state_mutex);
    if (last_presented_pts_ms != G_MININT64 && frame->pts_ms <= last_presented_pts_ms) {
        g_mutex_unlock(&player->queue_mutex);
        video_frame_destroy(frame);
        return;
    }

    GList *insert_before = player->frame_queue->head;
    while (insert_before) {
        VideoFrame *existing = (VideoFrame *)insert_before->data;
        if (existing && existing->pts_ms > frame->pts_ms) {
            break;
        }
        insert_before = insert_before->next;
    }
    if (insert_before) {
        g_queue_insert_before(player->frame_queue, insert_before, frame);
    } else {
        g_queue_push_tail(player->frame_queue, frame);
    }
    g_mutex_unlock(&player->queue_mutex);
}

static VideoFrame *video_player_queue_take_for_time(VideoPlayer *player,
                                                    gint64 target_pts_ms,
                                                    gint64 max_late_ms) {
    if (!player || !player->frame_queue) {
        return NULL;
    }
    g_mutex_lock(&player->queue_mutex);
    if (g_queue_is_empty(player->frame_queue)) {
        g_mutex_unlock(&player->queue_mutex);
        return NULL;
    }

    VideoFrame *head = (VideoFrame *)g_queue_peek_head(player->frame_queue);
    if (!head) {
        g_mutex_unlock(&player->queue_mutex);
        return NULL;
    }
    if (head->pts_ms > target_pts_ms) {
        g_mutex_unlock(&player->queue_mutex);
        return NULL;
    }

    if (max_late_ms < 0) {
        max_late_ms = 0;
    }
    VideoFrame *fallback = NULL;
    gboolean removed_frames = FALSE;
    while (!g_queue_is_empty(player->frame_queue)) {
        VideoFrame *peek = (VideoFrame *)g_queue_peek_head(player->frame_queue);
        if (!peek) {
            break;
        }
        if (peek->pts_ms > target_pts_ms) {
            break;
        }
        if ((target_pts_ms - peek->pts_ms) <= max_late_ms) {
            VideoFrame *frame = (VideoFrame *)g_queue_pop_head(player->frame_queue);
            removed_frames = TRUE;
            if (fallback) {
                video_frame_destroy(fallback);
            }
            if (removed_frames) {
                g_cond_broadcast(&player->frame_queue_has_space);
            }
            g_mutex_unlock(&player->queue_mutex);
            return frame;
        }
        VideoFrame *old = (VideoFrame *)g_queue_pop_head(player->frame_queue);
        removed_frames = TRUE;
        if (fallback) {
            video_frame_destroy(fallback);
        }
        fallback = old;
    }
    if (removed_frames) {
        g_cond_broadcast(&player->frame_queue_has_space);
    }
    g_mutex_unlock(&player->queue_mutex);
    return fallback;
}

RenderedFrame *video_player_queue_take_for_playback(VideoPlayer *player,
                                                    gint64 target_pts_ms,
                                                    gint64 max_late_ms) {
    return video_player_queue_take_for_time(player, target_pts_ms, max_late_ms);
}

void video_player_decode_queue_push(VideoPlayer *player, DecodedFrame *frame) {
    if (!player || !player->decode_queue || !frame) {
        decoded_frame_destroy(frame);
        return;
    }


    g_mutex_lock(&player->queue_mutex);
    while (!player->worker_stop && g_queue_get_length(player->decode_queue) >= 4) {
        video_player_notify_queue_wait_hook(player, VIDEO_PLAYER_TEST_QUEUE_DECODE);
        g_cond_wait(&player->decode_queue_has_space, &player->queue_mutex);
    }
    if (player->worker_stop) {
        g_mutex_unlock(&player->queue_mutex);
        decoded_frame_destroy(frame);
        return;
    }

    g_queue_push_tail(player->decode_queue, frame);
    g_cond_signal(&player->decode_queue_has_items);
    g_mutex_unlock(&player->queue_mutex);
}

DecodedFrame *video_player_decode_queue_take(VideoPlayer *player) {
    if (!player || !player->decode_queue) {
        return NULL;
    }

    g_mutex_lock(&player->queue_mutex);
    if (g_queue_is_empty(player->decode_queue)) {
        g_mutex_unlock(&player->queue_mutex);
        return NULL;
    }
    DecodedFrame *frame = (DecodedFrame *)g_queue_pop_head(player->decode_queue);
    g_cond_broadcast(&player->decode_queue_has_space);
    g_mutex_unlock(&player->queue_mutex);
    return frame;
}

DecodedFrame *video_player_decode_queue_wait_and_take(VideoPlayer *player) {
    if (!player || !player->decode_queue) {
        return NULL;
    }

    g_mutex_lock(&player->queue_mutex);
    while (!player->worker_stop && g_queue_is_empty(player->decode_queue)) {
        g_cond_wait(&player->decode_queue_has_items, &player->queue_mutex);
    }
    if (g_queue_is_empty(player->decode_queue)) {
        g_mutex_unlock(&player->queue_mutex);
        return NULL;
    }

    DecodedFrame *frame = (DecodedFrame *)g_queue_pop_head(player->decode_queue);
    g_cond_broadcast(&player->decode_queue_has_space);
    g_mutex_unlock(&player->queue_mutex);
    return frame;
}

static gboolean video_player_queue_peek_pts_ms(VideoPlayer *player, gint64 *pts_ms) {
    if (!player || !player->frame_queue || !pts_ms) {
        return FALSE;
    }
    g_mutex_lock(&player->queue_mutex);
    if (g_queue_is_empty(player->frame_queue)) {
        g_mutex_unlock(&player->queue_mutex);
        return FALSE;
    }
    VideoFrame *frame = (VideoFrame *)g_queue_peek_head(player->frame_queue);
    if (!frame) {
        g_mutex_unlock(&player->queue_mutex);
        return FALSE;
    }
    *pts_ms = frame->pts_ms;
    g_mutex_unlock(&player->queue_mutex);
    return TRUE;
}

static gboolean video_player_get_target_pts_ms(VideoPlayer *player, gint64 *target_pts_ms) {
    if (!player || !target_pts_ms) {
        return FALSE;
    }
    gint64 start_us = 0;
    gint64 start_pts_ms = 0;
    gboolean started = FALSE;
    g_mutex_lock(&player->state_mutex);
    started = player->clock_started;
    if (started) {
        start_us = player->clock_start_us;
        start_pts_ms = player->clock_start_pts_ms;
    }
    g_mutex_unlock(&player->state_mutex);
    if (!started) {
        return FALSE;
    }
    gint64 now_us = g_get_monotonic_time();
    gint64 elapsed_ms = (now_us - start_us) / 1000;
    *target_pts_ms = start_pts_ms + elapsed_ms;
    return TRUE;
}

static gint64 video_player_rescale_pts_ms(VideoPlayer *player, int64_t pts) {
    if (!player || pts == AV_NOPTS_VALUE) {
        return G_MININT64;
    }
    if (player->time_base_num <= 0 || player->time_base_den <= 0) {
        return G_MININT64;
    }
    AVRational time_base = (AVRational){ player->time_base_num, player->time_base_den };
    return av_rescale_q(pts, time_base, (AVRational){ 1, 1000 });
}

static gint video_player_get_slow_level(VideoPlayer *player) {
    if (!player) {
        return 0;
    }

    gint frame_delay = video_player_get_frame_delay_ms(player);
    if (frame_delay <= 0) {
        return 0;
    }

    g_mutex_lock(&player->state_mutex);
    gboolean io_valid = player->io_avg_valid;
    gdouble io_avg = player->io_avg_ms;
    g_mutex_unlock(&player->state_mutex);
    if (!io_valid || io_avg <= 0.0) {
        return 0;
    }
    gdouble ratio = io_avg / (gdouble)frame_delay;
    if (ratio > 1.6) {
        return 2;
    }
    if (ratio > 1.2) {
        return 1;
    }
    return 0;
}

static gint64 video_player_calc_late_window_ms(VideoPlayer *player, gint multiplier, gint min_ms) {
    if (!player || multiplier < 1) {
        return min_ms > 0 ? min_ms : 0;
    }
    gint frame_delay = video_player_get_frame_delay_ms(player);
    if (frame_delay <= 0) {
        frame_delay = 10;
    }
    gint64 base = (gint64)frame_delay * multiplier;
    if (min_ms > 0 && base < min_ms) {
        base = min_ms;
    }
    gint slow_level = video_player_get_slow_level(player);
    if (slow_level >= 2) {
        gint64 tight = frame_delay / 2;
        if (tight < 10) {
            tight = 10;
        }
        return tight;
    }
    if (slow_level == 1) {
        gint64 tight = frame_delay;
        if (min_ms > 0 && tight < min_ms) {
            tight = min_ms;
        }
        return tight;
    }
    return base;
}

static guint video_player_queue_length(VideoPlayer *player) {
    if (!player || !player->frame_queue) {
        return 0;
    }
    g_mutex_lock(&player->queue_mutex);
    guint length = g_queue_get_length(player->frame_queue);
    g_mutex_unlock(&player->queue_mutex);
    return length;
}

gboolean video_player_should_drop_late_frame(VideoPlayer *player, gint64 pts_ms) {
    if (!player) {
        return FALSE;
    }
    if (player->rewind_needs_resync) {
        return FALSE;
    }

    gint64 target_pts_ms = 0;
    if (!video_player_get_target_pts_ms(player, &target_pts_ms)) {
        return FALSE;
    }

    gint64 late_ms = target_pts_ms - pts_ms;
    gint64 late_threshold = video_player_calc_late_window_ms(player, 1, 10);
    if (late_ms <= late_threshold) {
        return FALSE;
    }

    guint backlog = video_player_queue_length(player);
    if (backlog < VIDEO_PLAYER_LATE_DROP_BACKLOG_THRESHOLD) {
        return FALSE;
    }

    gint64 last_presented_pts_ms = G_MININT64;
    g_mutex_lock(&player->state_mutex);
    last_presented_pts_ms = player->last_presented_pts_ms;
    g_mutex_unlock(&player->state_mutex);
    if (last_presented_pts_ms != G_MININT64 && pts_ms <= last_presented_pts_ms) {
        return TRUE;
    }

    gint64 now_us = g_get_monotonic_time();
    gint64 last_present_us = 0;
    g_mutex_lock(&player->state_mutex);
    last_present_us = player->last_present_us;
    g_mutex_unlock(&player->state_mutex);

    gint64 max_silence_us = VIDEO_PLAYER_MAX_SILENCE_US;
    return last_present_us > 0 && (now_us - last_present_us) < max_silence_us;
}

gint video_player_calc_delay_ms(VideoPlayer *player) {
    if (!player) {
        return 10;
    }
    gint64 target_pts_ms = 0;
    if (!video_player_get_target_pts_ms(player, &target_pts_ms)) {
        return 5;
    }
    gint64 next_pts_ms = 0;
    if (!video_player_queue_peek_pts_ms(player, &next_pts_ms)) {
        gint64 last_present_us = 0;
        g_mutex_lock(&player->state_mutex);
        last_present_us = player->last_present_us;
        g_mutex_unlock(&player->state_mutex);
        if (last_present_us > 0) {
            return 5;
        }
        gint delay = video_player_get_frame_delay_ms(player);
        if (delay <= 0) {
            delay = 10;
        }
        if (delay < 5) {
            delay = 5;
        }
        return delay;
    }
    gint64 wait_ms = next_pts_ms - target_pts_ms;
    if (wait_ms < 1) {
        wait_ms = 1;
    }
    return (gint)wait_ms;
}

static gboolean video_player_should_stop(VideoPlayer *player) {
    if (!player) {
        return TRUE;
    }
    g_mutex_lock(&player->queue_mutex);
    gboolean stop = player->worker_stop;
    g_mutex_unlock(&player->queue_mutex);
    return stop;
}

static void video_player_ffmpeg_log(void *ptr, int level, const char *fmt, va_list vl) {
    (void)ptr;
    (void)level;
    (void)fmt;
    (void)vl;
}

static void video_player_ffmpeg_init_once(void) {
    static gsize initialized = 0;
    if (g_once_init_enter(&initialized)) {
        av_log_set_callback(video_player_ffmpeg_log);
        av_log_set_level(AV_LOG_QUIET);
        avformat_network_init();
        g_once_init_leave(&initialized, 1);
    }
}

static struct SwsContext *video_player_create_sws_context(const AVCodecContext *codec_context,
                                                          gint width,
                                                          gint height) {
    enum AVPixelFormat src_pix_fmt = codec_context->pix_fmt;
    gboolean src_range_extended = FALSE;
    switch (src_pix_fmt) {
    case AV_PIX_FMT_YUVJ420P:
        src_pix_fmt = AV_PIX_FMT_YUV420P;
        src_range_extended = TRUE;
        break;
    case AV_PIX_FMT_YUVJ422P:
        src_pix_fmt = AV_PIX_FMT_YUV422P;
        src_range_extended = TRUE;
        break;
    case AV_PIX_FMT_YUVJ444P:
        src_pix_fmt = AV_PIX_FMT_YUV444P;
        src_range_extended = TRUE;
        break;
    case AV_PIX_FMT_YUVJ440P:
        src_pix_fmt = AV_PIX_FMT_YUV440P;
        src_range_extended = TRUE;
        break;
    default:
        break;
    }

    struct SwsContext *sws_context = sws_getContext(width, height, src_pix_fmt,
                                                    width, height, AV_PIX_FMT_RGBA,
                                                    SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_context) {
        return NULL;
    }

    if (src_range_extended) {
        int *inv_table = NULL;
        int *table = NULL;
        int src_range = 0;
        int dst_range = 0;
        int brightness = 0;
        int contrast = 0;
        int saturation = 0;
        sws_getColorspaceDetails(sws_context, &inv_table, &src_range,
                                 &table, &dst_range, &brightness, &contrast,
                                 &saturation);
        const int *coeffs = sws_getCoefficients(SWS_CS_DEFAULT);
        src_range = 1;
        sws_setColorspaceDetails(sws_context, coeffs, src_range,
                                 coeffs, dst_range, brightness,
                                 contrast, saturation);
    }

    return sws_context;
}

static void video_player_clear_decode(VideoPlayer *player) {
    if (!player) {
        return;
    }

    video_player_clear_line_cache(player);

    if (player->sws_context) {
        sws_freeContext(player->sws_context);
        player->sws_context = NULL;
    }

    if (player->codec_context) {
        avcodec_free_context(&player->codec_context);
    }

    if (player->format_context) {
        avformat_close_input(&player->format_context);
    }

    if (player->decode_frame) {
        av_frame_free(&player->decode_frame);
    }

    if (player->rgba_frame) {
        av_frame_free(&player->rgba_frame);
    }

    if (player->packet) {
        av_packet_free(&player->packet);
    }

    if (player->rgba_buffer) {
        av_freep(&player->rgba_buffer);
        player->rgba_buffer = NULL;
    }

    player->rgba_buffer_size = 0;
    player->video_stream_index = -1;
    player->video_width = 0;
    player->video_height = 0;
    player->time_base_num = 0;
    player->time_base_den = 0;
    player->has_video = FALSE;
    video_player_reset_timing_state(player);
}

VideoPlayer* video_player_new(gint work_factor, gboolean force_text, gboolean force_sixel, gboolean force_kitty,
                              gboolean force_iterm2, gdouble gamma) {
    VideoPlayer *player = g_new0(VideoPlayer, 1);
    if (!player) {
        return NULL;
    }

    player->is_playing = FALSE;
    player->has_video = FALSE;
    player->draining = FALSE;
    player->frame_delay_ms = 33;
    player->time_base_num = 0;
    player->time_base_den = 0;
    player->clock_start_us = 0;
    player->clock_start_pts_ms = 0;
    player->fallback_pts_ms = 0;
    player->clock_started = FALSE;
    player->rewind_needs_resync = FALSE;
    player->smooth_last_pts_ms = 0;
    player->smooth_pts_ms = 0;
    player->smooth_valid = FALSE;
    player->timer_id = 0;
    player->filepath = NULL;
    player->max_queue_size = VIDEO_PLAYER_QUEUE_DEPTH_SMALL_SIZE;
    player->playback_generation = 1;
    player->render_layout_generation = 1;
    player->renderer = renderer_create();
    player->owns_renderer = FALSE;
    g_mutex_init(&player->render_mutex);
    g_mutex_init(&player->state_mutex);
    g_mutex_init(&player->queue_mutex);
    g_cond_init(&player->frame_queue_has_space);
    g_cond_init(&player->decode_queue_has_space);
    g_cond_init(&player->decode_queue_has_items);
    player->frame_queue = g_queue_new();
    player->decode_queue = g_queue_new();
    player->worker_thread = NULL;
    player->render_workers[0] = NULL;
    player->render_workers[1] = NULL;
    player->worker_stop = FALSE;
    player->render_workers_started = FALSE;

    player->render_area_top_row = 0;
    player->render_area_height = 0;
    player->render_max_width = 0;
    player->render_max_height = 0;
    player->render_term_width = 0;
    player->render_term_height = 0;
    player->render_layout_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    player->fixed_frame_top_row = 0;
    player->fixed_frame_valid = FALSE;
    player->last_frame_lines = NULL;
    player->io_avg_ms = 0.0;
    player->io_avg_valid = FALSE;
    player->last_present_us = 0;
    player->last_presented_pts_ms = G_MININT64;
    player->present_fps = 0.0;
    player->present_fps_valid = FALSE;
    player->show_stats = FALSE;

    if (work_factor < 1) {
        work_factor = 1;
    } else if (work_factor > 9) {
        work_factor = 9;
    }

    if (player->renderer) {
        RendererConfig config = {
            .max_width = 80,
            .max_height = 24,
            .preserve_aspect_ratio = TRUE,
            .dither = FALSE,
            .color_space = CHAFA_COLOR_SPACE_RGB,
            .work_factor = work_factor,
            .force_text = force_text,
            .force_sixel = force_sixel,
            .force_kitty = force_kitty,
            .force_iterm2 = force_iterm2,
            .gamma = gamma,
            .dither_mode = CHAFA_DITHER_MODE_NONE,
            .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
            .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
        };
        if (renderer_initialize(player->renderer, &config) == ERROR_NONE) {
            player->owns_renderer = TRUE;
        } else {
            renderer_destroy(player->renderer);
            player->renderer = NULL;
        }
    }

    g_atomic_int_inc(&video_player_live_instances);

    return player;
}

void video_player_set_renderer(VideoPlayer *player, ImageRenderer *renderer) {
    if (!player) {
        return;
    }

    g_mutex_lock(&player->render_mutex);
    if (player->renderer && player->renderer != renderer && player->owns_renderer) {
        renderer_destroy(player->renderer);
    }
    player->renderer = renderer;
    player->owns_renderer = FALSE;
    g_mutex_unlock(&player->render_mutex);
}

void video_player_set_render_area(VideoPlayer *player,
                                  gint term_width,
                                  gint term_height,
                                  gint area_top_row,
                                  gint area_height,
                                  gint max_width,
                                  gint max_height) {
    if (!player) {
        return;
    }

    g_mutex_lock(&player->state_mutex);
    gboolean layout_changed = (player->render_term_width != term_width ||
                               player->render_term_height != term_height ||
                               player->render_area_top_row != area_top_row ||
                               player->render_area_height != area_height ||
                               player->render_max_width != max_width ||
                               player->render_max_height != max_height);

    player->render_term_width = term_width;
    player->render_term_height = term_height;
    player->render_area_top_row = area_top_row;
    player->render_area_height = area_height;
    player->render_max_width = max_width;
    player->render_max_height = max_height;
    player->render_layout_valid = (area_top_row > 0 && area_height > 0 && max_width > 0 && max_height > 0);

    if (layout_changed) {
        player->fixed_frame_valid = FALSE;
        player->last_frame_top_row = area_top_row;
        player->last_frame_height = area_height;
        video_player_generation_bump(player);
        player->render_layout_generation++;
        video_player_clear_line_cache(player);
    }
    g_mutex_unlock(&player->state_mutex);
}

void video_player_clear_render_area(VideoPlayer *player) {
    if (!player) {
        return;
    }

    gint area_top = 0;
    gint area_height = 0;
    gint term_h = 0;
    gint last_top = 0;
    gint last_height = 0;
    gboolean layout_valid = FALSE;

    g_mutex_lock(&player->state_mutex);
    area_top = player->render_area_top_row;
    area_height = player->render_area_height;
    term_h = player->render_term_height;
    last_top = player->last_frame_top_row;
    last_height = player->last_frame_height;
    layout_valid = player->render_layout_valid;
    g_mutex_unlock(&player->state_mutex);

    gint clear_top = 0;
    gint clear_height = 0;
    if (layout_valid && area_top > 0 && area_height > 0) {
        clear_top = area_top;
        clear_height = area_height;
    } else if (last_top > 0 && last_height > 0) {
        clear_top = last_top;
        clear_height = last_height;
    }

    if (clear_top <= 0 || clear_height <= 0) {
        return;
    }

    gint bottom = clear_top + clear_height - 1;
    if (term_h > 0 && bottom > term_h) {
        bottom = term_h;
    }

    for (gint row = clear_top; row <= bottom; row++) {
        printf("\033[%d;1H\033[2K", row);
    }
    fflush(stdout);
    video_player_clear_line_cache(player);
}

static gboolean video_player_render_frame(VideoPlayer *player);
static gpointer video_player_worker_thread(gpointer user_data);
static gpointer video_player_render_worker_thread(gpointer user_data);

static FILE *video_player_debug_stream = NULL;
static GMutex video_player_debug_mutex;
static void video_player_debug_register_atexit(void);

static void video_player_debug_close_stream_unlocked(void) {
    if (!video_player_debug_stream) {
        return;
    }

    fflush(video_player_debug_stream);
    fclose(video_player_debug_stream);
    video_player_debug_stream = NULL;
}

static void video_player_debug_close_stream(void) {
    g_mutex_lock(&video_player_debug_mutex);
    video_player_debug_close_stream_unlocked();
    g_mutex_unlock(&video_player_debug_mutex);
}

typedef struct {
    gboolean initialized;
    gboolean enabled;
    gchar *path;
} VideoPlayerDebugConfig;

static VideoPlayerDebugConfig video_player_debug_config = {0};

static void video_player_debug_config_init_unlocked(void) {
    if (video_player_debug_config.initialized) {
        return;
    }

    const gchar *enabled_env = g_getenv("PIXELTERM_DEBUG_VIDEO");
    video_player_debug_config.enabled =
        enabled_env && *enabled_env && g_strcmp0(enabled_env, "0") != 0;

    const gchar *path_env = g_getenv("PIXELTERM_DEBUG_VIDEO_LOG");
    if (!path_env || !*path_env) {
        path_env = "/tmp/pixelterm-video.log";
    }
    video_player_debug_config.path = g_strdup(path_env);
    video_player_debug_config.initialized = TRUE;
}

gboolean video_player_debug_enabled(void) {
    g_mutex_lock(&video_player_debug_mutex);
    video_player_debug_config_init_unlocked();
    gboolean enabled = video_player_debug_config.enabled;
    g_mutex_unlock(&video_player_debug_mutex);
    return enabled;
}

static void video_player_debug_register_atexit(void) {
    static gsize registered = 0;

    if (g_once_init_enter(&registered)) {
        atexit(video_player_debug_close_stream);
        g_once_init_leave(&registered, 1);
    }
}

static FILE *video_player_debug_get_stream_unlocked(void) {
    video_player_debug_config_init_unlocked();
    if (!video_player_debug_config.enabled) {
        return NULL;
    }

    video_player_debug_register_atexit();
    if (!video_player_debug_stream) {
        video_player_debug_stream = fopen(video_player_debug_config.path, "a");
    }
    return video_player_debug_stream;
}

FILE *video_player_debug_get_stream(void) {
    g_mutex_lock(&video_player_debug_mutex);
    FILE *stream = video_player_debug_get_stream_unlocked();
    g_mutex_unlock(&video_player_debug_mutex);
    return stream;
}

gboolean video_player_debug_has_current_stream_for_test(void) {
    g_mutex_lock(&video_player_debug_mutex);
    gboolean has_stream = (video_player_debug_stream != NULL);
    g_mutex_unlock(&video_player_debug_mutex);
    return has_stream;
}

void video_player_debug_reset_for_test(void) {
    g_mutex_lock(&video_player_debug_mutex);
    video_player_debug_close_stream_unlocked();
    g_clear_pointer(&video_player_debug_config.path, g_free);
    video_player_debug_config.initialized = FALSE;
    video_player_debug_config.enabled = FALSE;
    g_mutex_unlock(&video_player_debug_mutex);
}

static gboolean video_player_debug_should_log(const gchar *event) {
    if (!event) {
        return FALSE;
    }
    return g_strcmp0(event, "play-start") == 0 ||
           g_strcmp0(event, "tick-stop") == 0 ||
           g_strcmp0(event, "tick-reschedule") == 0 ||
           g_strcmp0(event, "worker-eof-rewind") == 0 ||
           g_strcmp0(event, "worker-frame-ready") == 0 ||
           g_strcmp0(event, "worker-decode-time") == 0 ||
           g_strcmp0(event, "worker-render-time") == 0 ||
           g_strcmp0(event, "worker-push") == 0 ||
           g_strcmp0(event, "worker-skip-full") == 0 ||
           g_strcmp0(event, "worker-drop-late") == 0 ||
           g_strcmp0(event, "worker-render-null") == 0 ||
           g_strcmp0(event, "render-first") == 0 ||
           g_strcmp0(event, "render-time") == 0 ||
           g_strcmp0(event, "render-frame") == 0 ||
           g_strcmp0(event, "render-draw-time") == 0 ||
           g_strcmp0(event, "render-wait") == 0;
}

gboolean video_player_debug_should_log_for_test(const gchar *event) {
    return video_player_debug_should_log(event);
}

static void video_player_debug_log(VideoPlayer *player,
                                   const gchar *event,
                                   gint64 a,
                                   gint64 b,
                                   gint64 c,
                                   gint64 d) {
    if (!video_player_debug_enabled()) {
        return;
    }
    if (!video_player_debug_should_log(event)) {
        return;
    }
    guint backlog = video_player_queue_length(player);
    gint frame_delay = video_player_get_frame_delay_ms(player);
    gint slow_level = video_player_get_slow_level(player);
    g_mutex_lock(&video_player_debug_mutex);
    FILE *stream = video_player_debug_get_stream_unlocked();
    if (!stream) {
        g_mutex_unlock(&video_player_debug_mutex);
        return;
    }
    fprintf(stream,
            "[video-debug] %s a=%lld b=%lld c=%lld d=%lld backlog=%u frame_delay=%d slow=%d\n",
            event ? event : "event",
            (long long)a,
            (long long)b,
            (long long)c,
            (long long)d,
            backlog,
            frame_delay,
            slow_level);
    fflush(stream);
    g_mutex_unlock(&video_player_debug_mutex);
}

static void video_player_start_worker(VideoPlayer *player) {
    if (!player || player->worker_thread || !player->renderer) {
        return;
    }
    g_mutex_lock(&player->queue_mutex);
    player->worker_stop = FALSE;
    g_mutex_unlock(&player->queue_mutex);
    player->draining = FALSE;
    player->worker_thread = g_thread_new("video-decode", video_player_worker_thread, player);
    if (!player->render_workers_started) {
        player->render_workers[0] = g_thread_new("video-render-0", video_player_render_worker_thread, player);
        player->render_workers[1] = g_thread_new("video-render-1", video_player_render_worker_thread, player);
        player->render_workers_started = TRUE;
    }
}

static RendererConfig video_player_render_worker_config(VideoPlayer *player) {
    RendererConfig config = {
        .max_width = 80,
        .max_height = 24,
        .preserve_aspect_ratio = TRUE,
        .dither = FALSE,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = 4,
        .force_text = TRUE,
        .force_sixel = FALSE,
        .force_kitty = FALSE,
        .force_iterm2 = FALSE,
        .gamma = 1.0,
        .dither_mode = CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };

    if (!player || !player->renderer) {
        return config;
    }

    g_mutex_lock(&player->render_mutex);
    config = player->renderer->config;
    g_mutex_unlock(&player->render_mutex);
    return config;
}

static void video_player_render_worker_refresh_layout(VideoPlayer *player,
                                                      ImageRenderer *renderer,
                                                      guint *layout_generation_inout) {
    if (!player || !renderer || !layout_generation_inout) {
        return;
    }

    guint current_generation = 0;
    gint max_width = 0;
    gint max_height = 0;
    gboolean layout_valid = FALSE;
    RendererConfig config = video_player_render_worker_config(player);

    g_mutex_lock(&player->state_mutex);
    current_generation = player->render_layout_generation;
    if (*layout_generation_inout != current_generation) {
        max_width = player->render_max_width;
        max_height = player->render_max_height;
        layout_valid = player->render_layout_valid;
    }
    g_mutex_unlock(&player->state_mutex);

    if (*layout_generation_inout == current_generation) {
        return;
    }

    renderer_update_terminal_size(renderer);
    renderer->config = config;

    if (layout_valid) {
        renderer->config.max_width = max_width;
        renderer->config.max_height = max_height;
    }

    *layout_generation_inout = current_generation;
}

static void video_player_stop_worker_internal(VideoPlayer *player, gboolean clear_queues) {
    if (!player) {
        return;
    }
    g_mutex_lock(&player->queue_mutex);
    player->worker_stop = TRUE;
    g_cond_broadcast(&player->frame_queue_has_space);
    g_cond_broadcast(&player->decode_queue_has_space);
    g_cond_broadcast(&player->decode_queue_has_items);
    g_mutex_unlock(&player->queue_mutex);
    if (player->worker_thread) {
        g_thread_join(player->worker_thread);
        player->worker_thread = NULL;
    }
    if (player->render_workers_started) {
        if (player->render_workers[0]) {
            g_thread_join(player->render_workers[0]);
            player->render_workers[0] = NULL;
        }
        if (player->render_workers[1]) {
            g_thread_join(player->render_workers[1]);
            player->render_workers[1] = NULL;
        }
        player->render_workers_started = FALSE;
    }
    g_mutex_lock(&player->queue_mutex);
    player->worker_stop = FALSE;
    g_mutex_unlock(&player->queue_mutex);
    if (clear_queues) {
        video_player_queue_clear(player);
        video_player_decode_queue_clear(player);
    }
}

static void video_player_stop_worker(VideoPlayer *player) {
    video_player_stop_worker_internal(player, TRUE);
}

static void video_player_resume_playback_loop(VideoPlayer *player) {
    if (!player || !player->is_playing) {
        return;
    }

    video_player_start_worker(player);
    video_player_render_frame(player);
    video_player_schedule_tick(player);
}

static int video_player_seek_frame(VideoPlayer *player, int64_t target_ts, int flags) {
    if (!player || !player->format_context) {
        return AVERROR(EINVAL);
    }
    if (video_player_seek_hook) {
        return video_player_seek_hook(player->format_context, player->video_stream_index, target_ts, flags);
    }
    return av_seek_frame(player->format_context, player->video_stream_index, target_ts, flags);
}

static VideoFrame *video_player_build_rendered_frame(ImageRenderer *renderer,
                                                     const guint8 *pixels,
                                                     gint width,
                                                     gint height,
                                                     gint rowstride,
                                                     gint64 pts_ms,
                                                     guint generation) {
    if (!renderer || !pixels || width <= 0 || height <= 0 || rowstride <= 0) {
        return NULL;
    }

    GString *rendered = renderer_render_image_data(renderer, pixels, width, height, rowstride, 4);
    if (!rendered) {
        return NULL;
    }

    gint rendered_w = 0;
    gint rendered_h = 0;
    renderer_get_rendered_dimensions(renderer, &rendered_w, &rendered_h);
    ChafaPixelMode pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
    if (renderer->canvas_config) {
        pixel_mode = chafa_canvas_config_get_pixel_mode(renderer->canvas_config);
    }

    VideoFrame *frame = g_new0(VideoFrame, 1);
    if (!frame) {
        g_string_free(rendered, TRUE);
        return NULL;
    }
    frame->rendered = rendered;
    frame->rendered_width = rendered_w;
    frame->rendered_height = rendered_h;
    frame->pts_ms = pts_ms;
    frame->pixel_mode = pixel_mode;
    frame->generation = generation;
    return frame;
}

static gboolean video_player_seek_preview_read_and_send_packet(VideoPlayer *player) {
    if (!player) {
        return FALSE;
    }
    if (player->draining) {
        return TRUE;
    }

    int read_result = av_read_frame(player->format_context, player->packet);
    if (read_result < 0) {
        player->draining = TRUE;
        return avcodec_send_packet(player->codec_context, NULL) >= 0;
    }

    if (player->packet->stream_index != player->video_stream_index) {
        av_packet_unref(player->packet);
        return FALSE;
    }

    int send_result = avcodec_send_packet(player->codec_context, player->packet);
    av_packet_unref(player->packet);
    return send_result >= 0;
}

static gboolean video_player_seek_preview_receive_and_convert_frame(VideoPlayer *player,
                                                                   gint64 *preview_pts_ms,
                                                                   gboolean *frame_ready) {
    if (!player || !preview_pts_ms || !frame_ready) {
        return FALSE;
    }

    gint invaliddata_attempts = 0;
    for (;;) {
        int receive_result = avcodec_receive_frame(player->codec_context, player->decode_frame);
        if (receive_result == 0) {
            int64_t best_pts = player->decode_frame->best_effort_timestamp;
            gint64 decoded_pts_ms = video_player_rescale_pts_ms(player, best_pts);
            if (decoded_pts_ms != G_MININT64) {
                *preview_pts_ms = decoded_pts_ms;
            }
            sws_scale(player->sws_context,
                      (const uint8_t * const *)player->decode_frame->data,
                      player->decode_frame->linesize,
                      0,
                      player->codec_context->height,
                      player->rgba_frame->data,
                      player->rgba_frame->linesize);
            *frame_ready = TRUE;
            return TRUE;
        }
        if (receive_result == AVERROR(EAGAIN)) {
            return FALSE;
        }
        if (receive_result == AVERROR_EOF) {
            return FALSE;
        }
        if (receive_result == AVERROR_INVALIDDATA && !player->draining) {
            invaliddata_attempts++;
            if (invaliddata_attempts >= k_video_player_max_preview_receive_invaliddata_attempts) {
                return FALSE;
            }
            continue;
        }
        return FALSE;
    }
}

static gboolean video_player_decode_seek_preview_frame(VideoPlayer *player, gint64 target_ms, gint64 *preview_pts_ms) {
    if (!player || !preview_pts_ms || !player->format_context || !player->codec_context || !player->packet ||
        !player->decode_frame || !player->rgba_frame || !player->sws_context) {
        return FALSE;
    }

    gboolean frame_ready = FALSE;
    gint attempts = 0;
    gint max_attempts = video_player_get_max_preview_decode_attempts_for_test();
    *preview_pts_ms = target_ms;
    while (!frame_ready) {
        if (attempts >= max_attempts) {
            break;
        }
        attempts++;

        if (!video_player_seek_preview_read_and_send_packet(player)) {
            if (!player->draining) {
                continue;
            }
            break;
        }

        (void)video_player_seek_preview_receive_and_convert_frame(player, preview_pts_ms, &frame_ready);

        if (player->draining) {
            break;
        }
    }

    return frame_ready;
}

static gboolean video_player_render_seek_preview(VideoPlayer *player, gint64 target_ms) {
    if (!player) {
        return FALSE;
    }
    if (video_player_seek_preview_hook) {
        return video_player_seek_preview_hook(player, target_ms);
    }
    if (!player->renderer || !player->format_context || !player->codec_context || !player->packet ||
        !player->decode_frame || !player->rgba_frame || !player->sws_context) {
        return FALSE;
    }

    gint64 preview_pts_ms = target_ms;
    if (!video_player_decode_seek_preview_frame(player, target_ms, &preview_pts_ms)) {
        return FALSE;
    }

    VideoFrame *frame = NULL;
    g_mutex_lock(&player->render_mutex);
    if (renderer_update_terminal_size(player->renderer) == ERROR_NONE) {
        gboolean layout_valid = FALSE;
        gint max_width = 0;
        gint max_height = 0;
        g_mutex_lock(&player->state_mutex);
        layout_valid = player->render_layout_valid;
        max_width = player->render_max_width;
        max_height = player->render_max_height;
        g_mutex_unlock(&player->state_mutex);
        if (layout_valid) {
            player->renderer->config.max_width = max_width;
            player->renderer->config.max_height = max_height;
        }
        frame = video_player_build_rendered_frame(player->renderer,
                                                  player->rgba_frame->data[0],
                                                  player->video_width,
                                                  player->video_height,
                                                  player->rgba_frame->linesize[0],
                                                  preview_pts_ms,
                                                  video_player_generation_get(player));
    }
    g_mutex_unlock(&player->render_mutex);

    if (!frame) {
        return FALSE;
    }
    video_player_queue_push(player, frame);
    return video_player_render_frame(player);
}

static gboolean video_player_tick(gpointer user_data) {
    VideoPlayer *player = (VideoPlayer *)user_data;
    if (!player) {
        return G_SOURCE_REMOVE;
    }

    if (!player->is_playing) {
        player->timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    if (!video_player_render_frame(player)) {
        video_player_debug_log(player, "tick-stop", 0, 0, 0, 0);
        player->timer_id = 0;
        player->is_playing = FALSE;
        return G_SOURCE_REMOVE;
    }

    int delay = video_player_calc_delay_ms(player);
    video_player_debug_log(player, "tick-reschedule", delay, 0, 0, 0);
    if (delay < 1) {
        delay = 1;
    }

    player->timer_id = g_timeout_add(delay, video_player_tick, player);
    return G_SOURCE_REMOVE;
}

static gpointer video_player_worker_thread(gpointer user_data) {
    VideoPlayer *player = (VideoPlayer *)user_data;
    if (!player) {
        return NULL;
    }

    for (;;) {
        if (video_player_should_stop(player)) {
            break;
        }

        if (!player->format_context || !player->codec_context || !player->renderer) {
            g_usleep(10000);
            continue;
        }

        gboolean frame_ready = FALSE;
        gint64 decode_start_us = g_get_monotonic_time();
        while (!frame_ready && !video_player_should_stop(player)) {
            if (!player->draining) {
                int read_result = av_read_frame(player->format_context, player->packet);
                if (read_result < 0) {
                    player->draining = TRUE;
                    avcodec_send_packet(player->codec_context, NULL);
                } else if (player->packet->stream_index == player->video_stream_index) {
                    avcodec_send_packet(player->codec_context, player->packet);
                    av_packet_unref(player->packet);
                } else {
                    av_packet_unref(player->packet);
                }
            }

            int receive_result = avcodec_receive_frame(player->codec_context, player->decode_frame);
            if (receive_result == 0) {
                sws_scale(player->sws_context,
                          (const uint8_t * const *)player->decode_frame->data,
                          player->decode_frame->linesize,
                          0,
                          player->codec_context->height,
                          player->rgba_frame->data,
                          player->rgba_frame->linesize);
                frame_ready = TRUE;
                break;
            }

            if (receive_result == AVERROR_EOF) {
                av_seek_frame(player->format_context, player->video_stream_index, 0, AVSEEK_FLAG_ANY);
                avcodec_flush_buffers(player->codec_context);
                video_player_queue_clear(player);
                video_player_reset_timing_state(player);
                player->rewind_needs_resync = TRUE;
                video_player_debug_log(player, "worker-eof-rewind", 0, 0, 0, 0);
                continue;
            }

            if (receive_result == AVERROR(EAGAIN)) {
                if (player->draining) {
                    break;
                }
                continue;
            }

            if (receive_result == AVERROR_INVALIDDATA && !player->draining) {
                continue;
            }

            break;
        }

        if (!frame_ready || video_player_should_stop(player)) {
            continue;
        }
        gint64 decode_elapsed_us = g_get_monotonic_time() - decode_start_us;

        gint frame_delay = player->frame_delay_ms;
        if (frame_delay < 1) {
            frame_delay = 1;
        }
        int64_t best_pts = player->decode_frame->best_effort_timestamp;
        gint64 raw_pts_ms = video_player_rescale_pts_ms(player, best_pts);
        if (raw_pts_ms == G_MININT64) {
            raw_pts_ms = player->fallback_pts_ms;
        }
        player->fallback_pts_ms = raw_pts_ms + frame_delay;

        gint64 pts_ms = raw_pts_ms;
        gint64 min_step = frame_delay / 2;
        if (min_step < 1) {
            min_step = 1;
        }
        gint64 max_step = frame_delay * 2;
        if (max_step < min_step) {
            max_step = min_step;
        }
        if (player->smooth_valid) {
            gint64 delta = raw_pts_ms - player->smooth_last_pts_ms;
            if (delta < min_step) {
                delta = min_step;
            } else if (delta > max_step) {
                delta = max_step;
            }
            pts_ms = player->smooth_pts_ms + delta;
        } else {
            player->smooth_valid = TRUE;
            pts_ms = raw_pts_ms;
        }
        player->smooth_last_pts_ms = raw_pts_ms;
        player->smooth_pts_ms = pts_ms;
        video_player_debug_log(player, "worker-frame-ready", raw_pts_ms, pts_ms, player->fallback_pts_ms, 0);
        video_player_debug_log(player, "worker-decode-time", pts_ms, decode_elapsed_us, 0, 0);
        if (video_player_should_drop_late_frame(player, pts_ms)) {
            gint64 target_pts_ms = 0;
            (void)video_player_get_target_pts_ms(player, &target_pts_ms);
            video_player_debug_log(player,
                                   "worker-drop-late",
                                   pts_ms,
                                   target_pts_ms,
                                   target_pts_ms - pts_ms,
                                   video_player_calc_late_window_ms(player, 1, 10));
            continue;
        }

        DecodedFrame *decoded = g_new0(DecodedFrame, 1);
        if (!decoded) {
            continue;
        }
        gint buffer_size = player->video_height * player->rgba_frame->linesize[0];
        decoded->pixels = g_memdup2(player->rgba_frame->data[0], buffer_size);
        if (!decoded->pixels) {
            decoded_frame_destroy(decoded);
            continue;
        }
        decoded->width = player->video_width;
        decoded->height = player->video_height;
        decoded->rowstride = player->rgba_frame->linesize[0];
        decoded->pts_ms = pts_ms;
        decoded->generation = video_player_generation_get(player);
        video_player_decode_queue_push(player, decoded);
    }

    return NULL;
}

static gpointer video_player_render_worker_thread(gpointer user_data) {
    VideoPlayer *player = (VideoPlayer *)user_data;
    if (!player) {
        return NULL;
    }

    ImageRenderer *renderer = renderer_create();
    if (!renderer) {
        return NULL;
    }

    RendererConfig config = video_player_render_worker_config(player);
    if (renderer_initialize(renderer, &config) != ERROR_NONE) {
        renderer_destroy(renderer);
        return NULL;
    }
    guint layout_generation = 0;

    while (!video_player_should_stop(player)) {
        DecodedFrame *decoded = video_player_decode_queue_wait_and_take(player);
        if (!decoded) {
            continue;
        }
        if (decoded->generation != video_player_generation_get(player)) {
            decoded_frame_destroy(decoded);
            continue;
        }

        gint64 render_start_us = g_get_monotonic_time();
        video_player_render_worker_refresh_layout(player, renderer, &layout_generation);

        GString *rendered = renderer_render_image_data(renderer,
                                                       decoded->pixels,
                                                       decoded->width,
                                                       decoded->height,
                                                       decoded->rowstride,
                                                       4);
        gint rendered_w = 0;
        gint rendered_h = 0;
        ChafaPixelMode pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
        if (rendered) {
            renderer_get_rendered_dimensions(renderer, &rendered_w, &rendered_h);
            if (renderer->canvas_config) {
                pixel_mode = chafa_canvas_config_get_pixel_mode(renderer->canvas_config);
            }
        }
        gint64 render_elapsed_us = g_get_monotonic_time() - render_start_us;
        video_player_debug_log(player, "worker-render-time", decoded->pts_ms, render_elapsed_us, rendered_w, rendered_h);

        if (!rendered) {
            video_player_debug_log(player, "worker-render-null", decoded->pts_ms, renderer->config.max_width, renderer->config.max_height, 0);
            decoded_frame_destroy(decoded);
            continue;
        }

        VideoFrame *frame = g_new0(VideoFrame, 1);
        if (!frame) {
            g_string_free(rendered, TRUE);
            decoded_frame_destroy(decoded);
            continue;
        }
        frame->rendered = rendered;
        frame->rendered_width = rendered_w;
        frame->rendered_height = rendered_h;
        frame->pts_ms = decoded->pts_ms;
        frame->pixel_mode = pixel_mode;
        frame->generation = decoded->generation;
        video_player_update_queue_depth(player, rendered_w, rendered_h);
        video_player_queue_insert_sorted(player, frame);
        video_player_debug_log(player, "worker-push", decoded->pts_ms, rendered_w, rendered_h, pixel_mode);
        decoded_frame_destroy(decoded);
    }

    renderer_destroy(renderer);
    return NULL;
}

static gboolean video_player_render_frame(VideoPlayer *player) {
    if (!player || !player->renderer || !player->format_context || !player->codec_context) {
        return FALSE;
    }

    gint64 target_pts_ms = 0;
    gboolean clock_started = video_player_get_target_pts_ms(player, &target_pts_ms);
    VideoFrame *frame = NULL;
    if (!clock_started) {
        frame = video_player_queue_take_first(player);
        video_player_debug_log(player, "render-first", frame ? frame->pts_ms : -1, 0, 0, 0);
    } else {
        gint slow_level = video_player_get_slow_level(player);
        gint64 max_late_ms = video_player_calc_late_window_ms(player, 2, 20);
        frame = video_player_queue_take_for_playback(player, target_pts_ms, max_late_ms);
        video_player_debug_log(player, "render-time", target_pts_ms, frame ? frame->pts_ms : -1, max_late_ms, slow_level);
        if (!frame) {
            gint64 next_pts_ms = 0;
            gboolean have_next = video_player_queue_peek_pts_ms(player, &next_pts_ms);
            video_player_debug_log(player,
                                   "render-wait",
                                   target_pts_ms,
                                   have_next ? next_pts_ms : -1,
                                   video_player_calc_delay_ms(player),
                                   0);
            return TRUE;
        }
    }
    if (!frame || !frame->rendered) {
        video_frame_destroy(frame);
        return TRUE;
    }

    if (!clock_started) {
        gint64 now_us = g_get_monotonic_time();
        g_mutex_lock(&player->state_mutex);
        player->clock_start_us = now_us;
        player->clock_start_pts_ms = frame->pts_ms;
        player->clock_started = TRUE;
        player->rewind_needs_resync = FALSE;
        player->last_presented_pts_ms = G_MININT64;
        g_mutex_unlock(&player->state_mutex);
    }

    GString *result = frame->rendered;
    gint rendered_w = frame->rendered_width;
    gint rendered_h = frame->rendered_height;
    gboolean graphics_mode = (frame->pixel_mode != CHAFA_PIXEL_MODE_SYMBOLS);
    video_player_debug_log(player, "render-frame", frame->pts_ms, rendered_w, rendered_h, graphics_mode ? 1 : 0);

    gint64 io_start_us = g_get_monotonic_time();
    if (player->render_layout_valid && player->render_area_top_row > 0 && player->render_area_height > 0) {
        gint term_w = player->render_term_width > 0 ? player->render_term_width : player->render_max_width;
        gint term_h = player->render_term_height;
        gint area_top = player->render_area_top_row;
        gint area_bottom = area_top + player->render_area_height - 1;
        if (term_h > 0 && area_bottom > term_h) {
            area_bottom = term_h;
        }

        gint effective_w = rendered_w > 0 ? rendered_w : player->render_max_width;
        if (term_w > 0 && effective_w > term_w) {
            effective_w = term_w;
        }
        gint left_pad = (term_w > effective_w) ? (term_w - effective_w) / 2 : 0;
        if (left_pad < 0) left_pad = 0;

        gint image_top_row = area_top;
        if (player->fixed_frame_valid) {
            image_top_row = player->fixed_frame_top_row;
        } else if (player->render_area_height > 0 && rendered_h > 0 && rendered_h < player->render_area_height) {
            gint vpad = (player->render_area_height - rendered_h) / 2;
            if (vpad > 0) {
                image_top_row = area_top + vpad;
            }
            player->fixed_frame_top_row = image_top_row;
            player->fixed_frame_valid = TRUE;
        } else {
            player->fixed_frame_top_row = image_top_row;
            player->fixed_frame_valid = TRUE;
        }

        gint row = image_top_row;
        gint lines_printed = 0;
        gboolean has_newline = !graphics_mode && memchr(result->str, '\n', result->len) != NULL;

        if (graphics_mode) {
            video_player_clear_line_cache(player);
            gint col = 1 + left_pad;
            if (col < 1) {
                col = 1;
            }
            printf("\033[%d;%dH", row, col);
            if (result->len > 0) {
                fwrite(result->str, 1, result->len, stdout);
            }
            lines_printed = rendered_h > 0 ? rendered_h : 1;
        } else if (!has_newline) {
            video_player_clear_line_cache(player);
            if (left_pad > 0) {
                gchar *pad_buffer = g_malloc(left_pad);
                memset(pad_buffer, ' ', left_pad);
                printf("\033[%d;1H", row);
                fwrite(pad_buffer, 1, left_pad, stdout);
                g_free(pad_buffer);
            } else {
                printf("\033[%d;1H", row);
            }
            if (result->len > 0) {
                fwrite(result->str, 1, result->len, stdout);
            }
            lines_printed = rendered_h > 0 ? rendered_h : 1;
        } else {
            GPtrArray *new_lines = g_ptr_array_new_with_free_func(g_free);
            const gchar *line_ptr = result->str;
            const gchar *end = result->str + result->len;
            gint line_index = 0;
            while (line_ptr && line_ptr < end && row <= area_bottom) {
                const gchar *newline = memchr(line_ptr, '\n', end - line_ptr);
                gint line_len = newline ? (gint)(newline - line_ptr) : (gint)(end - line_ptr);
                gint full_len = left_pad + line_len;
                gchar *full_line = g_malloc(full_len + 1);
                if (left_pad > 0) {
                    memset(full_line, ' ', left_pad);
                }
                if (line_len > 0) {
                    memcpy(full_line + left_pad, line_ptr, line_len);
                }
                full_line[full_len] = '\0';

                const gchar *prev_line = NULL;
                if (player->last_frame_lines &&
                    line_index < (gint)player->last_frame_lines->len) {
                    prev_line = g_ptr_array_index(player->last_frame_lines, line_index);
                }
                if (!prev_line || strcmp(prev_line, full_line) != 0) {
                    printf("\033[%d;1H\033[2K", row);
                    if (full_len > 0) {
                        fwrite(full_line, 1, full_len, stdout);
                    }
                }
                g_ptr_array_add(new_lines, full_line);
                lines_printed++;
                line_index++;
                if (!newline) {
                    break;
                }
                line_ptr = newline + 1;
                row++;
            }

            if (player->last_frame_lines) {
                g_ptr_array_free(player->last_frame_lines, TRUE);
            }
            player->last_frame_lines = new_lines;
        }

        if (player->last_frame_height > 0) {
            gint prev_top = player->last_frame_top_row;
            gint prev_bottom = prev_top + player->last_frame_height - 1;
            gint new_top = image_top_row;
            gint new_bottom = image_top_row + (lines_printed > 0 ? (lines_printed - 1) : -1);
            if (prev_top < area_top) prev_top = area_top;
            if (prev_bottom > area_bottom) prev_bottom = area_bottom;
            for (gint r = prev_top; r <= prev_bottom; r++) {
                if (r < new_top || r > new_bottom) {
                    printf("\033[%d;1H\033[2K", r);
                }
            }
        }

        player->last_frame_top_row = image_top_row;
        player->last_frame_height = lines_printed > 0 ? lines_printed : 0;

        if (player->show_stats) {
            gint stats_row = 3;
            if (stats_row >= 1 && (term_h <= 0 || stats_row <= term_h)) {
                char proto_line[24];
                char fps_line[24];
                const char *proto = video_player_pixel_mode_label(frame->pixel_mode);
                g_snprintf(proto_line, sizeof(proto_line), "%s", proto);
                if (player->present_fps_valid) {
                    g_snprintf(fps_line, sizeof(fps_line), "%5.1f", player->present_fps);
                } else {
                    g_snprintf(fps_line, sizeof(fps_line), " --.-");
                }
                gint proto_len = (gint)strlen(proto_line);
                gint fps_len = (gint)strlen(fps_line);
                gint fps_col = 1;
                if (term_w > 0) {
                    fps_col = term_w - fps_len + 1;
                }
                if (fps_col < 1) {
                    fps_col = 1;
                }
                gboolean show_proto = TRUE;
                if (term_w > 0 && fps_col <= proto_len + 1) {
                    show_proto = FALSE;
                }
                if (show_proto) {
                    printf("\033[%d;1H%s", stats_row, proto_line);
                    for (gint pad = proto_len; pad < 12; pad++) {
                        putchar(' ');
                    }
                }
                printf("\033[%d;%dH%s", stats_row, fps_col, fps_line);
            }
        }
    } else {
        video_player_clear_line_cache(player);
        printf("\033[H");
        printf("%s", result->str);
        printf("\033[J");
        player->last_frame_top_row = 0;
        player->last_frame_height = 0;
    }

    fflush(stdout);
    gint64 io_end_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = frame->pts_ms;
    g_mutex_unlock(&player->state_mutex);
    video_player_debug_log(player, "render-draw-time", frame->pts_ms, io_end_us - io_start_us, rendered_w, rendered_h);
    video_player_update_present_fps(player, io_end_us);
    video_player_update_io_avg(player, (io_end_us - io_start_us) / 1000);

    video_frame_destroy(frame);
    return TRUE;
}

ErrorCode video_player_load(VideoPlayer *player, const gchar *filepath) {
    if (!player || !filepath) {
        return ERROR_INVALID_IMAGE;
    }

    video_player_ffmpeg_init_once();
    video_player_stop(player);
    video_player_clear_decode(player);

    g_free(player->filepath);
    player->filepath = NULL;

    if (!file_exists(filepath)) {
        return ERROR_FILE_NOT_FOUND;
    }

    AVFormatContext *format_context = NULL;
    if (avformat_open_input(&format_context, filepath, NULL, NULL) != 0) {
        return ERROR_INVALID_IMAGE;
    }

    if (avformat_find_stream_info(format_context, NULL) < 0) {
        avformat_close_input(&format_context);
        return ERROR_INVALID_IMAGE;
    }

    int video_stream_index = -1;
    const AVCodec *decoder = NULL;
    for (unsigned int i = 0; i < format_context->nb_streams; i++) {
        AVCodecParameters *params = format_context->streams[i]->codecpar;
        if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
            decoder = avcodec_find_decoder(params->codec_id);
            if (decoder) {
                video_stream_index = (int)i;
                break;
            }
        }
    }

    if (video_stream_index < 0 || !decoder) {
        avformat_close_input(&format_context);
        return ERROR_INVALID_IMAGE;
    }

    AVCodecContext *codec_context = avcodec_alloc_context3(decoder);
    if (!codec_context) {
        avformat_close_input(&format_context);
        return ERROR_MEMORY_ALLOC;
    }

    if (avcodec_parameters_to_context(codec_context, format_context->streams[video_stream_index]->codecpar) < 0) {
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return ERROR_INVALID_IMAGE;
    }

    if (avcodec_open2(codec_context, decoder, NULL) < 0) {
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return ERROR_INVALID_IMAGE;
    }

    AVFrame *decode_frame = av_frame_alloc();
    AVFrame *rgba_frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    if (!decode_frame || !rgba_frame || !packet) {
        if (decode_frame) av_frame_free(&decode_frame);
        if (rgba_frame) av_frame_free(&rgba_frame);
        if (packet) av_packet_free(&packet);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return ERROR_MEMORY_ALLOC;
    }

    int width = codec_context->width;
    int height = codec_context->height;
    int rgba_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width, height, 1);
    if (rgba_buffer_size <= 0) {
        av_frame_free(&decode_frame);
        av_frame_free(&rgba_frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return ERROR_INVALID_IMAGE;
    }

    uint8_t *rgba_buffer = av_malloc(rgba_buffer_size);
    if (!rgba_buffer) {
        av_frame_free(&decode_frame);
        av_frame_free(&rgba_frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return ERROR_MEMORY_ALLOC;
    }

    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgba_buffer,
                         AV_PIX_FMT_RGBA, width, height, 1);

    struct SwsContext *sws_context = video_player_create_sws_context(codec_context, width, height);
    if (!sws_context) {
        av_freep(&rgba_buffer);
        av_frame_free(&decode_frame);
        av_frame_free(&rgba_frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return ERROR_INVALID_IMAGE;
    }

    AVStream *video_stream = format_context->streams[video_stream_index];
    AVRational rate = av_guess_frame_rate(format_context, video_stream, NULL);
    gint frame_delay = 33;
    if (rate.num > 0 && rate.den > 0) {
        frame_delay = (gint)(1000.0 * rate.den / rate.num);
        if (frame_delay < 1) {
            frame_delay = 1;
        }
    }

    player->format_context = format_context;
    player->codec_context = codec_context;
    player->sws_context = sws_context;
    player->decode_frame = decode_frame;
    player->rgba_frame = rgba_frame;
    player->packet = packet;
    player->rgba_buffer = rgba_buffer;
    player->rgba_buffer_size = rgba_buffer_size;
    player->video_stream_index = video_stream_index;
    player->video_width = width;
    player->video_height = height;
    player->frame_delay_ms = frame_delay;
    player->time_base_num = video_stream->time_base.num;
    player->time_base_den = video_stream->time_base.den;
    if (player->time_base_num <= 0 || player->time_base_den <= 0) {
        player->time_base_num = 1;
        player->time_base_den = 1000;
    }
    player->fallback_pts_ms = 0;
    player->filepath = g_strdup(filepath);
    player->has_video = TRUE;
    player->draining = FALSE;

    return ERROR_NONE;
}

ErrorCode video_player_play(VideoPlayer *player) {
    if (!player || !player->has_video) {
        return ERROR_INVALID_IMAGE;
    }

    if (player->is_playing) {
        return ERROR_NONE;
    }

    player->is_playing = TRUE;
    video_player_generation_bump(player);
    player->fixed_frame_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    video_player_clear_line_cache(player);
    video_player_reset_timing_state(player);

    video_player_queue_clear(player);
    video_player_start_worker(player);

    video_player_render_frame(player);

    int delay = video_player_calc_delay_ms(player);
    video_player_debug_log(player, "play-start", delay, player->video_width, player->video_height, player->frame_delay_ms);
    video_player_schedule_tick(player);

    return ERROR_NONE;
}

ErrorCode video_player_pause(VideoPlayer *player) {
    if (!player) {
        return ERROR_INVALID_IMAGE;
    }

    player->is_playing = FALSE;
    video_player_generation_bump(player);
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }
    video_player_stop_worker(player);

    return ERROR_NONE;
}

ErrorCode video_player_stop(VideoPlayer *player) {
    if (!player) {
        return ERROR_INVALID_IMAGE;
    }

    player->is_playing = FALSE;
    video_player_generation_bump(player);
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }
    video_player_stop_worker(player);

    return ERROR_NONE;
}

ErrorCode video_player_seek_relative_ms(VideoPlayer *player, gint64 delta_ms) {
    if (!player || !player->has_video || !player->format_context || !player->codec_context ||
        player->video_stream_index < 0 || !player->format_context->streams ||
        !player->format_context->streams[player->video_stream_index]) {
        return ERROR_INVALID_ARGS;
    }

    if (delta_ms == 0) {
        return ERROR_NONE;
    }

    gint64 duration_ms = 0;
    if (player->format_context->duration != AV_NOPTS_VALUE && player->format_context->duration > 0) {
        duration_ms = av_rescale_q(player->format_context->duration, AV_TIME_BASE_Q, (AVRational){1, 1000});
    }

    gint64 current_ms = video_player_current_position_ms(player);
    gint64 target_ms = video_player_seek_target_ms(player, delta_ms, duration_ms);
    if (target_ms == current_ms) {
        return ERROR_NONE;
    }
    gboolean was_playing = video_player_is_playing(player);

    if (was_playing) {
        if (player->timer_id != 0) {
            g_source_remove(player->timer_id);
            player->timer_id = 0;
        }
        video_player_stop_worker_internal(player, FALSE);
    }

    AVStream *stream = player->format_context->streams[player->video_stream_index];
    int flags = target_ms <= current_ms ? AVSEEK_FLAG_BACKWARD : 0;
    int64_t target_ts = av_rescale_q(target_ms, (AVRational){1, 1000}, stream->time_base);
    if (video_player_seek_frame(player, target_ts, flags) < 0) {
        if (was_playing) {
            video_player_resume_playback_loop(player);
        }
        return ERROR_INVALID_IMAGE;
    }

    video_player_queue_clear(player);
    video_player_decode_queue_clear(player);
    if (player->packet) {
        av_packet_unref(player->packet);
    }
    if (avcodec_is_open(player->codec_context)) {
        avcodec_flush_buffers(player->codec_context);
    }
    video_player_reset_timing_state(player);
    g_mutex_lock(&player->state_mutex);
    player->fallback_pts_ms = target_ms;
    player->rewind_needs_resync = TRUE;
    player->fixed_frame_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    g_mutex_unlock(&player->state_mutex);
    video_player_clear_line_cache(player);

    if (was_playing) {
        video_player_resume_playback_loop(player);
    } else {
        (void)video_player_render_seek_preview(player, target_ms);
    }

    return ERROR_NONE;
}

gboolean video_player_is_playing(const VideoPlayer *player) {
    return player && player->is_playing;
}

gboolean video_player_has_video(const VideoPlayer *player) {
    return player && player->has_video;
}

ErrorCode video_player_update_terminal_size(VideoPlayer *player) {
    if (!player || !player->renderer) {
        return ERROR_INVALID_IMAGE;
    }

    g_mutex_lock(&player->render_mutex);
    ErrorCode result = renderer_update_terminal_size(player->renderer);
    g_mutex_unlock(&player->render_mutex);
    return result;
}

ErrorCode video_player_get_dimensions(const gchar *filepath, gint *width, gint *height) {
    if (!filepath || !width || !height) {
        return ERROR_INVALID_IMAGE;
    }

    video_player_ffmpeg_init_once();
    AVFormatContext *format_context = NULL;
    if (avformat_open_input(&format_context, filepath, NULL, NULL) != 0) {
        return ERROR_INVALID_IMAGE;
    }

    if (avformat_find_stream_info(format_context, NULL) < 0) {
        avformat_close_input(&format_context);
        return ERROR_INVALID_IMAGE;
    }

    gint found_w = 0;
    gint found_h = 0;
    for (unsigned int i = 0; i < format_context->nb_streams; i++) {
        AVCodecParameters *params = format_context->streams[i]->codecpar;
        if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
            found_w = params->width;
            found_h = params->height;
            break;
        }
    }

    avformat_close_input(&format_context);

    if (found_w <= 0 || found_h <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    *width = found_w;
    *height = found_h;
    return ERROR_NONE;
}

ErrorCode video_player_get_first_frame(const gchar *filepath,
                                       guint8 **pixels,
                                       gint *width,
                                       gint *height,
                                       gint *rowstride) {
    if (!filepath || !pixels || !width || !height || !rowstride) {
        return ERROR_INVALID_IMAGE;
    }

    *pixels = NULL;
    *width = 0;
    *height = 0;
    *rowstride = 0;

    video_player_ffmpeg_init_once();

    ErrorCode error = ERROR_INVALID_IMAGE;
    AVFormatContext *format_context = NULL;
    AVCodecContext *codec_context = NULL;
    AVFrame *decode_frame = NULL;
    AVFrame *rgba_frame = NULL;
    AVPacket *packet = NULL;
    struct SwsContext *sws_context = NULL;
    guint8 *rgba_buffer = NULL;
    gint video_w = 0;
    gint video_h = 0;
    gboolean frame_ready = FALSE;

    if (avformat_open_input(&format_context, filepath, NULL, NULL) != 0) {
        return ERROR_INVALID_IMAGE;
    }

    if (avformat_find_stream_info(format_context, NULL) < 0) {
        goto cleanup;
    }

    int video_stream_index = -1;
    const AVCodec *decoder = NULL;
    for (unsigned int i = 0; i < format_context->nb_streams; i++) {
        AVCodecParameters *params = format_context->streams[i]->codecpar;
        if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
            decoder = avcodec_find_decoder(params->codec_id);
            if (decoder) {
                video_stream_index = (int)i;
                break;
            }
        }
    }

    if (video_stream_index < 0 || !decoder) {
        goto cleanup;
    }

    codec_context = avcodec_alloc_context3(decoder);
    if (!codec_context) {
        error = ERROR_MEMORY_ALLOC;
        goto cleanup;
    }

    if (avcodec_parameters_to_context(codec_context,
                                      format_context->streams[video_stream_index]->codecpar) < 0) {
        goto cleanup;
    }

    if (avcodec_open2(codec_context, decoder, NULL) < 0) {
        goto cleanup;
    }

    video_w = codec_context->width;
    video_h = codec_context->height;
    if (video_w <= 0 || video_h <= 0) {
        goto cleanup;
    }

    decode_frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!decode_frame || !rgba_frame || !packet) {
        error = ERROR_MEMORY_ALLOC;
        goto cleanup;
    }

    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, video_w, video_h, 1);
    if (buffer_size <= 0) {
        goto cleanup;
    }

    rgba_buffer = g_malloc(buffer_size);
    if (!rgba_buffer) {
        error = ERROR_MEMORY_ALLOC;
        goto cleanup;
    }

    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgba_buffer,
                         AV_PIX_FMT_RGBA, video_w, video_h, 1);

    sws_context = video_player_create_sws_context(codec_context, video_w, video_h);
    if (!sws_context) {
        goto cleanup;
    }

    while (!frame_ready) {
        int read_result = av_read_frame(format_context, packet);
        if (read_result < 0) {
            avcodec_send_packet(codec_context, NULL);
        } else if (packet->stream_index == video_stream_index) {
            avcodec_send_packet(codec_context, packet);
            av_packet_unref(packet);
        } else {
            av_packet_unref(packet);
        }

        for (;;) {
            int receive_result = avcodec_receive_frame(codec_context, decode_frame);
            if (receive_result == 0) {
                sws_scale(sws_context,
                          (const uint8_t * const *)decode_frame->data,
                          decode_frame->linesize,
                          0,
                          codec_context->height,
                          rgba_frame->data,
                          rgba_frame->linesize);
                frame_ready = TRUE;
                break;
            }
            if (receive_result == AVERROR(EAGAIN)) {
                break;
            }
            if (receive_result == AVERROR_EOF) {
                break;
            }
            if (receive_result == AVERROR_INVALIDDATA) {
                break;
            }
            break;
        }

        if (read_result < 0) {
            break;
        }
    }

    if (frame_ready) {
        *pixels = rgba_buffer;
        *width = video_w;
        *height = video_h;
        *rowstride = rgba_frame->linesize[0];
        rgba_buffer = NULL;
        error = ERROR_NONE;
    }

cleanup:
    if (sws_context) {
        sws_freeContext(sws_context);
    }
    if (packet) {
        av_packet_free(&packet);
    }
    if (rgba_frame) {
        av_frame_free(&rgba_frame);
    }
    if (decode_frame) {
        av_frame_free(&decode_frame);
    }
    if (codec_context) {
        avcodec_free_context(&codec_context);
    }
    if (format_context) {
        avformat_close_input(&format_context);
    }
    if (rgba_buffer) {
        g_free(rgba_buffer);
    }

    return error;
}

void video_player_destroy(VideoPlayer *player) {
    if (!player) {
        return;
    }

    video_player_stop(player);
    video_player_clear_decode(player);

    g_free(player->filepath);

    if (player->renderer && player->owns_renderer) {
        renderer_destroy(player->renderer);
    }

    if (player->frame_queue) {
        video_player_queue_clear(player);
        g_queue_free(player->frame_queue);
    }
    if (player->decode_queue) {
        video_player_decode_queue_clear(player);
        g_queue_free(player->decode_queue);
    }
    g_cond_clear(&player->decode_queue_has_items);
    g_cond_clear(&player->decode_queue_has_space);
    g_cond_clear(&player->frame_queue_has_space);
    g_mutex_clear(&player->queue_mutex);
    g_mutex_clear(&player->state_mutex);
    g_mutex_clear(&player->render_mutex);

    if (g_atomic_int_dec_and_test(&video_player_live_instances)) {
        video_player_debug_close_stream();
    }

    g_free(player);
}
