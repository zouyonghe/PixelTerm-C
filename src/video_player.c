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
#include <string.h>

typedef struct {
    GString *rendered;
    gint rendered_width;
    gint rendered_height;
    gint64 pts_ms;
} VideoFrame;

static void video_frame_destroy(VideoFrame *frame) {
    if (!frame) {
        return;
    }
    if (frame->rendered) {
        g_string_free(frame->rendered, TRUE);
    }
    g_free(frame);
}

static void video_player_queue_clear(VideoPlayer *player) {
    if (!player || !player->frame_queue) {
        return;
    }
    g_mutex_lock(&player->queue_mutex);
    while (!g_queue_is_empty(player->frame_queue)) {
        VideoFrame *frame = (VideoFrame *)g_queue_pop_head(player->frame_queue);
        video_frame_destroy(frame);
    }
    g_mutex_unlock(&player->queue_mutex);
}

static void video_player_queue_push(VideoPlayer *player, VideoFrame *frame) {
    if (!player || !player->frame_queue || !frame) {
        video_frame_destroy(frame);
        return;
    }
    g_mutex_lock(&player->queue_mutex);
    if (player->worker_stop) {
        g_mutex_unlock(&player->queue_mutex);
        video_frame_destroy(frame);
        return;
    }
    while (player->max_queue_size > 0 &&
           g_queue_get_length(player->frame_queue) >= (guint)player->max_queue_size) {
        VideoFrame *old = (VideoFrame *)g_queue_pop_head(player->frame_queue);
        video_frame_destroy(old);
    }
    g_queue_push_tail(player->frame_queue, frame);
    g_mutex_unlock(&player->queue_mutex);
}

static VideoFrame *video_player_queue_take_first(VideoPlayer *player) {
    if (!player || !player->frame_queue) {
        return NULL;
    }
    g_mutex_lock(&player->queue_mutex);
    if (g_queue_is_empty(player->frame_queue)) {
        g_mutex_unlock(&player->queue_mutex);
        return NULL;
    }
    VideoFrame *frame = (VideoFrame *)g_queue_pop_head(player->frame_queue);
    g_mutex_unlock(&player->queue_mutex);
    return frame;
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
            if (fallback) {
                video_frame_destroy(fallback);
            }
            g_mutex_unlock(&player->queue_mutex);
            return frame;
        }
        VideoFrame *old = (VideoFrame *)g_queue_pop_head(player->frame_queue);
        if (fallback) {
            video_frame_destroy(fallback);
        }
        fallback = old;
    }
    g_mutex_unlock(&player->queue_mutex);
    return fallback;
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

static guint video_player_queue_length(VideoPlayer *player) {
    if (!player || !player->frame_queue) {
        return 0;
    }
    g_mutex_lock(&player->queue_mutex);
    guint length = g_queue_get_length(player->frame_queue);
    g_mutex_unlock(&player->queue_mutex);
    return length;
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

static gboolean video_player_should_drop_late_frame(VideoPlayer *player, gint64 pts_ms) {
    gint64 target_pts_ms = 0;
    if (!video_player_get_target_pts_ms(player, &target_pts_ms)) {
        return FALSE;
    }
    gint64 threshold = player->frame_delay_ms * 3;
    if (threshold < 20) {
        threshold = 20;
    }
    return (target_pts_ms - pts_ms) > threshold;
}

static gint video_player_calc_delay_ms(VideoPlayer *player) {
    if (!player) {
        return 10;
    }
    gint64 target_pts_ms = 0;
    if (!video_player_get_target_pts_ms(player, &target_pts_ms)) {
        return 5;
    }
    gint64 next_pts_ms = 0;
    if (!video_player_queue_peek_pts_ms(player, &next_pts_ms)) {
        gint delay = player->frame_delay_ms > 0 ? player->frame_delay_ms : 10;
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
    player->fallback_pts_ms = 0;
    player->clock_start_us = 0;
    player->clock_start_pts_ms = 0;
    player->clock_started = FALSE;
    player->smooth_last_pts_ms = 0;
    player->smooth_pts_ms = 0;
    player->smooth_valid = FALSE;
    player->has_video = FALSE;
    player->draining = FALSE;
}

VideoPlayer* video_player_new(gint work_factor, gboolean force_sixel) {
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
    player->smooth_last_pts_ms = 0;
    player->smooth_pts_ms = 0;
    player->smooth_valid = FALSE;
    player->timer_id = 0;
    player->filepath = NULL;
    player->max_queue_size = 8;
    player->renderer = renderer_create();
    player->owns_renderer = FALSE;
    g_mutex_init(&player->render_mutex);
    g_mutex_init(&player->state_mutex);
    g_mutex_init(&player->queue_mutex);
    player->frame_queue = g_queue_new();
    player->worker_thread = NULL;
    player->worker_stop = FALSE;

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
            .force_sixel = force_sixel,
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
        player->last_frame_top_row = 0;
        player->last_frame_height = 0;
    }
    g_mutex_unlock(&player->state_mutex);
}

static gboolean video_player_render_frame(VideoPlayer *player);
static gpointer video_player_worker_thread(gpointer user_data);

static void video_player_start_worker(VideoPlayer *player) {
    if (!player || player->worker_thread || !player->renderer) {
        return;
    }
    g_mutex_lock(&player->queue_mutex);
    player->worker_stop = FALSE;
    g_mutex_unlock(&player->queue_mutex);
    player->draining = FALSE;
    player->worker_thread = g_thread_new("video-render", video_player_worker_thread, player);
}

static void video_player_stop_worker(VideoPlayer *player) {
    if (!player || !player->worker_thread) {
        return;
    }
    g_mutex_lock(&player->queue_mutex);
    player->worker_stop = TRUE;
    g_mutex_unlock(&player->queue_mutex);
    g_thread_join(player->worker_thread);
    player->worker_thread = NULL;
    g_mutex_lock(&player->queue_mutex);
    player->worker_stop = FALSE;
    g_mutex_unlock(&player->queue_mutex);
    video_player_queue_clear(player);
}

static gboolean video_player_tick(gpointer user_data) {
    VideoPlayer *player = (VideoPlayer *)user_data;
    if (!player || !player->is_playing) {
        player->timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    if (!video_player_render_frame(player)) {
        player->timer_id = 0;
        player->is_playing = FALSE;
        return G_SOURCE_REMOVE;
    }

    int delay = video_player_calc_delay_ms(player);
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
                player->draining = FALSE;
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
        if (video_player_queue_length(player) > 1 &&
            video_player_should_drop_late_frame(player, pts_ms)) {
            continue;
        }

        gint max_width = 0;
        gint max_height = 0;
        gboolean layout_valid = FALSE;
        g_mutex_lock(&player->state_mutex);
        max_width = player->render_max_width;
        max_height = player->render_max_height;
        layout_valid = player->render_layout_valid;
        g_mutex_unlock(&player->state_mutex);

        GString *rendered = NULL;
        gint rendered_w = 0;
        gint rendered_h = 0;
        g_mutex_lock(&player->render_mutex);
        renderer_update_terminal_size(player->renderer);
        if (layout_valid) {
            player->renderer->config.max_width = max_width;
            player->renderer->config.max_height = max_height;
        }
        rendered = renderer_render_image_data(player->renderer,
                                              player->rgba_frame->data[0],
                                              player->video_width,
                                              player->video_height,
                                              player->rgba_frame->linesize[0],
                                              4);
        if (rendered) {
            renderer_get_rendered_dimensions(player->renderer, &rendered_w, &rendered_h);
        }
        g_mutex_unlock(&player->render_mutex);

        if (!rendered) {
            continue;
        }

        VideoFrame *frame = g_new0(VideoFrame, 1);
        if (!frame) {
            g_string_free(rendered, TRUE);
            continue;
        }
        frame->rendered = rendered;
        frame->rendered_width = rendered_w;
        frame->rendered_height = rendered_h;
        frame->pts_ms = pts_ms;
        video_player_queue_push(player, frame);
    }

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
    } else {
        gint64 max_late_ms = player->frame_delay_ms * 2;
        if (max_late_ms < 20) {
            max_late_ms = 20;
        }
        frame = video_player_queue_take_for_time(player, target_pts_ms, max_late_ms);
        if (!frame) {
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
        g_mutex_unlock(&player->state_mutex);
    }

    GString *result = frame->rendered;
    gint rendered_w = frame->rendered_width;
    gint rendered_h = frame->rendered_height;

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

        gchar *pad_buffer = NULL;
        if (left_pad > 0) {
            pad_buffer = g_malloc(left_pad);
            memset(pad_buffer, ' ', left_pad);
        }

        const gchar *line_ptr = result->str;
        gint row = image_top_row;
        gint lines_printed = 0;
        if (!strchr(result->str, '\n')) {
            printf("\033[%d;1H", row);
            if (left_pad > 0) {
                fwrite(pad_buffer, 1, left_pad, stdout);
            }
            fwrite(result->str, 1, result->len, stdout);
            lines_printed = rendered_h > 0 ? rendered_h : 1;
        } else {
            while (line_ptr && *line_ptr && row <= area_bottom) {
                const gchar *newline = strchr(line_ptr, '\n');
                gint line_len = newline ? (gint)(newline - line_ptr) : (gint)strlen(line_ptr);
                printf("\033[%d;1H\033[2K", row);
                if (left_pad > 0) {
                    fwrite(pad_buffer, 1, left_pad, stdout);
                }
                if (line_len > 0) {
                    fwrite(line_ptr, 1, line_len, stdout);
                }
                lines_printed++;
                if (!newline) {
                    break;
                }
                line_ptr = newline + 1;
                row++;
            }
        }
        g_free(pad_buffer);

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
    } else {
        printf("\033[H");
        printf("%s", result->str);
        printf("\033[J");
        player->last_frame_top_row = 0;
        player->last_frame_height = 0;
    }

    video_frame_destroy(frame);
    fflush(stdout);
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
    player->fixed_frame_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    g_mutex_lock(&player->state_mutex);
    player->clock_started = FALSE;
    player->clock_start_us = 0;
    player->clock_start_pts_ms = 0;
    player->smooth_last_pts_ms = 0;
    player->smooth_pts_ms = 0;
    player->smooth_valid = FALSE;
    g_mutex_unlock(&player->state_mutex);

    video_player_queue_clear(player);
    video_player_start_worker(player);

    video_player_render_frame(player);

    int delay = video_player_calc_delay_ms(player);
    if (delay < 1) {
        delay = 1;
    }

    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
    }
    player->timer_id = g_timeout_add(delay, video_player_tick, player);

    return ERROR_NONE;
}

ErrorCode video_player_pause(VideoPlayer *player) {
    if (!player) {
        return ERROR_INVALID_IMAGE;
    }

    player->is_playing = FALSE;
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
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }
    video_player_stop_worker(player);

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
    g_mutex_clear(&player->queue_mutex);
    g_mutex_clear(&player->state_mutex);
    g_mutex_clear(&player->render_mutex);

    g_free(player);
}
