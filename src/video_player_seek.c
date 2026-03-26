#include "video_player_seek_internal.h"

#include "video_player_clock_internal.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>

typedef RenderedFrame VideoFrame;

void video_player_queue_push(VideoPlayer *player, RenderedFrame *frame);

static VideoPlayerSeekPreviewHook video_player_seek_preview_hook = NULL;
static const gint k_video_player_default_preview_decode_attempts = 64;
static const gint k_video_player_max_preview_receive_invaliddata_attempts = 8;
static gint video_player_max_preview_decode_attempts = -1;

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

gint64 video_player_seek_target_ms(VideoPlayer *player, gint64 delta_ms, gint64 duration_ms) {
    gint64 target_ms = video_player_current_position_ms(player) + delta_ms;
    if (target_ms < 0) {
        target_ms = 0;
    }
    if (duration_ms > 0 && target_ms > duration_ms) {
        target_ms = duration_ms;
    }
    return target_ms;
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
    ChafaPixelMode pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
    renderer_get_rendered_dimensions(renderer, &rendered_w, &rendered_h);
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

static gboolean video_player_decode_seek_preview_frame(VideoPlayer *player,
                                                       gint64 target_ms,
                                                       gint64 *preview_pts_ms) {
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

gboolean video_player_render_seek_preview(VideoPlayer *player, gint64 target_ms) {
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
                                                  (guint)g_atomic_int_get(&player->playback_generation));
    }
    g_mutex_unlock(&player->render_mutex);

    if (!frame) {
        return FALSE;
    }
    video_player_queue_push(player, frame);
    return TRUE;
}
