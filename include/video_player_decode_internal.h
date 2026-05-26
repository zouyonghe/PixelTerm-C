#ifndef VIDEO_PLAYER_DECODE_INTERNAL_H
#define VIDEO_PLAYER_DECODE_INTERNAL_H

#include "video_player.h"

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

/*
 * Internal decoder module — owns FFmpeg decode resources.
 * Extracted from the VideoPlayer struct to isolate the decode layer
 * from render layout and playback-state concerns.
 *
 * All functions accept a VideoPlayer* and access only the decoder
 * fields (format_context, codec_context, sws_context, etc.).
 */

/* One-shot FFmpeg log/callback init (idempotent) */
void video_player_ffmpeg_init_once(void);

/* Create sws context from codec context dimensions */
struct SwsContext *video_player_create_sws_context(const struct AVCodecContext *codec_context,
                                                    gint width,
                                                    gint height);

/* Allocate RGBA buffer into rgba_frame, set *rgba_buffer_out.
 * Returns buffer size on success, -1 on failure. */
gint video_player_alloc_rgba_buffer(struct AVFrame *rgba_frame,
                                     gint width,
                                     gint height,
                                     guint8 **rgba_buffer_out);

/* Clear / free all decoder resources on player */
void video_player_clear_decode(VideoPlayer *player);

/* Decoder utility functions */
gboolean video_player_frame_buffer_size(gint height, gint rowstride, gsize *buffer_size_out);
gboolean video_player_rgba_layout_within_limits(gint width, gint height, gint rowstride, gsize *buffer_size_out);
gboolean video_player_dimensions_within_limits(gint width, gint height);

/* Decode PTS rescaling (needs player->time_base from playback module) */
gint64 video_player_rescale_pts_ms(VideoPlayer *player, int64_t pts);

/* Worker-thread decoder helpers */
gboolean video_player_get_draining(VideoPlayer *player);
void video_player_set_draining(VideoPlayer *player, gboolean draining);
gboolean video_player_has_renderer(VideoPlayer *player);

/* Slot helpers — exported for seek and debugging code that touches decoder fields */
void video_player_clear_decode_parts(VideoPlayer *player);

#endif /* VIDEO_PLAYER_DECODE_INTERNAL_H */
