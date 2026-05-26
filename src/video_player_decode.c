#include "video_player_decode_internal.h"
#include "video_player_debug_internal.h"
#include "media_buffer.h"

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

/* ───── FFmpeg init ───── */

static void video_player_ffmpeg_log(void *ptr, int level, const char *fmt, va_list vl) {
    (void)ptr; (void)level; (void)fmt; (void)vl;
}

void video_player_ffmpeg_init_once(void) {
    static gsize initialized = 0;
    if (g_once_init_enter(&initialized)) {
        av_log_set_callback(video_player_ffmpeg_log);
        av_log_set_level(AV_LOG_QUIET);
        avformat_network_init();
        g_once_init_leave(&initialized, 1);
    }
}

/* ───── SWS context ───── */

struct SwsContext *video_player_create_sws_context(const AVCodecContext *codec_context,
                                                      gint width,
                                                      gint height) {
    if (!codec_context) {
        return NULL;
    }

    if (!video_player_dimensions_within_limits(width, height)) {
        return NULL;
    }

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
        int *inv_table = NULL, *table = NULL;
        int src_range = 0, dst_range = 0;
        int brightness = 0, contrast = 0, saturation = 0;
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

/* ───── RGBA buffer allocation ───── */

gint video_player_alloc_rgba_buffer(AVFrame *rgba_frame,
                                     gint width,
                                     gint height,
                                     guint8 **rgba_buffer_out) {
    if (!rgba_frame || !rgba_buffer_out || !video_player_dimensions_within_limits(width, height)) {
        return -1;
    }

    *rgba_buffer_out = NULL;
    gint buffer_size = av_image_alloc(rgba_frame->data,
                                       rgba_frame->linesize,
                                       width, height,
                                       AV_PIX_FMT_RGBA, 32);
    if (buffer_size < 0) {
        return -1;
    }
    if (!video_player_rgba_layout_within_limits(width, height, rgba_frame->linesize[0], NULL)) {
        av_freep(&rgba_frame->data[0]);
        return -1;
    }

    *rgba_buffer_out = rgba_frame->data[0];
    return buffer_size;
}

/* ───── Buffer validation ───── */

gboolean video_player_dimensions_within_limits(gint width, gint height) {
    return media_buffer_dimensions_within_limits(width, height);
}

gboolean video_player_frame_buffer_size(gint height, gint rowstride, gsize *buffer_size_out) {
    return media_buffer_size_within_limits(height, rowstride, buffer_size_out);
}

gboolean video_player_rgba_layout_within_limits(gint width, gint height, gint rowstride, gsize *buffer_size_out) {
    return media_buffer_validate_layout(width, height, rowstride, 4, 1, buffer_size_out);
}

/* ───── Decode resources teardown ───── */

void video_player_clear_decode(VideoPlayer *player) {
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
    player->has_video = FALSE;
}

/* ───── PTS rescaling ───── */

gint64 video_player_rescale_pts_ms(VideoPlayer *player, int64_t pts) {
    if (!player || pts == AV_NOPTS_VALUE) {
        return G_MININT64;
    }
    if (player->time_base_num <= 0 || player->time_base_den <= 0) {
        return G_MININT64;
    }
    AVRational time_base = (AVRational){ player->time_base_num, player->time_base_den };
    return av_rescale_q(pts, time_base, (AVRational){ 1, 1000 });
}

/* ───── Draining flag (accessed from worker threads + seek) ───── */

gboolean video_player_get_draining(VideoPlayer *player) {
    if (!player) {
        return FALSE;
    }
    g_mutex_lock(&player->state_mutex);
    gboolean draining = player->draining;
    g_mutex_unlock(&player->state_mutex);
    return draining;
}

void video_player_set_draining(VideoPlayer *player, gboolean draining) {
    if (!player) {
        return;
    }
    g_mutex_lock(&player->state_mutex);
    player->draining = draining;
    g_mutex_unlock(&player->state_mutex);
}

/* ───── Renderer presence (moved to video_player.c core) ───── */
/* video_player_has_renderer() now lives in video_player.c as a static
 * function since it operates on render_mutex/renderer fields which are
 * core VideoPlayer concerns, not decode-layer resources. */
