#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include "common.h"
#include "renderer.h"

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

// Video playback structure
typedef struct {
    gboolean is_playing;
    gboolean has_video;
    gboolean draining;
    gint frame_delay_ms;
    gint time_base_num;
    gint time_base_den;
    gint64 clock_start_us;
    gint64 clock_start_pts_ms;
    gint64 fallback_pts_ms;
    gboolean clock_started;
    gint64 smooth_last_pts_ms;
    gint64 smooth_pts_ms;
    gboolean smooth_valid;
    guint timer_id;
    gchar *filepath;
    gint max_queue_size;

    // Renderer reference
    ImageRenderer *renderer;
    gboolean owns_renderer;
    GMutex render_mutex;
    GMutex state_mutex;

    // Render layout for single image mode
    gint render_area_top_row;
    gint render_area_height;
    gint render_max_width;
    gint render_max_height;
    gint render_term_width;
    gint render_term_height;
    gboolean render_layout_valid;
    gint last_frame_top_row;
    gint last_frame_height;
    gint fixed_frame_top_row;
    gboolean fixed_frame_valid;
    GPtrArray *last_frame_lines;
    gdouble io_avg_ms;
    gboolean io_avg_valid;
    gint64 last_present_us;
    gdouble present_fps;
    gboolean present_fps_valid;
    gboolean show_stats;

    // FFmpeg state
    struct AVFormatContext *format_context;
    struct AVCodecContext *codec_context;
    struct SwsContext *sws_context;
    struct AVFrame *decode_frame;
    struct AVFrame *rgba_frame;
    struct AVPacket *packet;
    gint video_stream_index;
    gint video_width;
    gint video_height;
    guint8 *rgba_buffer;
    gint rgba_buffer_size;

    // Pre-rendered frame queue (worker thread)
    GThread *worker_thread;
    GMutex queue_mutex;
    GQueue *frame_queue;
    gboolean worker_stop;
} VideoPlayer;

VideoPlayer* video_player_new(gint work_factor, gboolean force_sixel, gboolean force_kitty);
void video_player_destroy(VideoPlayer *player);
void video_player_set_renderer(VideoPlayer *player, ImageRenderer *renderer);
void video_player_set_render_area(VideoPlayer *player,
                                  gint term_width,
                                  gint term_height,
                                  gint area_top_row,
                                  gint area_height,
                                  gint max_width,
                                  gint max_height);
ErrorCode video_player_load(VideoPlayer *player, const gchar *filepath);
ErrorCode video_player_play(VideoPlayer *player);
ErrorCode video_player_pause(VideoPlayer *player);
ErrorCode video_player_stop(VideoPlayer *player);
gboolean video_player_is_playing(const VideoPlayer *player);
gboolean video_player_has_video(const VideoPlayer *player);
ErrorCode video_player_update_terminal_size(VideoPlayer *player);
ErrorCode video_player_get_dimensions(const gchar *filepath, gint *width, gint *height);
ErrorCode video_player_get_first_frame(const gchar *filepath,
                                       guint8 **pixels,
                                       gint *width,
                                       gint *height,
                                       gint *rowstride);

#endif // VIDEO_PLAYER_H
