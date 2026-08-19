#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include "common.h"
#include "kitty_transfer.h"
#include "renderer.h"

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

typedef struct {
    guint8 *pixels;
    gint width;
    gint height;
    gint rowstride;
    gint64 pts_ms;
    guint generation;
} DecodedFrame;

typedef struct {
    GString *rendered;
    gchar *kitty_shm_name;
    gint rendered_width;
    gint rendered_height;
    gint64 pts_ms;
    ChafaPixelMode pixel_mode;
    guint generation;
} RenderedFrame;

/* Convenience alias used internally for the frame-queue head type.
 * Centralized here so all internal modules share a single definition. */
typedef RenderedFrame VideoFrame;

#define VIDEO_PLAYER_STATS_ROW 3

// Video playback structure
typedef struct {
    gboolean is_playing;
    gboolean has_video;
    gboolean draining;
    gboolean eof_pending;
    gboolean eof_ended;
    gint frame_delay_ms;
    gint time_base_num;
    gint time_base_den;
    gint64 clock_start_us;
    gint64 clock_start_pts_ms;
    gint64 fallback_pts_ms;
    gboolean clock_started;
    gboolean rewind_needs_resync;
    gint64 smooth_last_pts_ms;
    gint64 smooth_pts_ms;
    gboolean smooth_valid;
    guint timer_id;
    gchar *filepath;
    gint max_queue_size;
    gint playback_generation;
    guint render_layout_generation;

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
    gint64 last_presented_pts_ms;
    gdouble present_fps;
    gboolean present_fps_valid;
    gboolean show_stats;
    ColorEnhanceMode color_enhance;
    KittyTransferMode kitty_transfer;
    gboolean kitty_shm_enabled;

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
    GThread *render_workers[2];
    GMutex queue_mutex;
    GCond frame_queue_has_space;
    GCond decode_queue_has_space;
    GCond decode_queue_has_items;
    GQueue *frame_queue;
    GQueue *decode_queue;
    gint render_in_flight;
    /* Atomic stop flag read by decode/render workers. */
    gint worker_stop;
    gboolean render_workers_started;
  } VideoPlayer;

/**
 * @brief Creates a video player and its default renderer.
 *
 * @return A caller-owned player, or NULL when allocation fails. Destroy the
 * player with video_player_destroy().
 * @note Create and control the player from its owning GLib main context.
 */
VideoPlayer* video_player_new(gint work_factor, gboolean force_text, gboolean force_sixel, gboolean force_kitty,
                              gboolean force_iterm2, TextSymbolMode text_symbol_mode, gdouble gamma,
                              KittyTransferMode kitty_transfer);

/**
 * @brief Stops workers and releases all player resources.
 *
 * @note No callback or other thread may use the player after this call begins.
 */
void video_player_destroy(VideoPlayer *player);

/**
 * @brief Replaces the renderer used by the player.
 *
 * Active workers are stopped before replacement and restarted when playback
 * was active. The player borrows @p renderer and never destroys it.
 */
void video_player_set_renderer(VideoPlayer *player, ImageRenderer *renderer);

/**
 * @brief Sets the terminal layout available to video rendering.
 *
 * Layout state is synchronized internally. Terminal coordinates are one-based.
 */
void video_player_set_render_area(VideoPlayer *player,
                                  gint term_width,
                                  gint term_height,
                                  gint area_top_row,
                                  gint area_height,
                                  gint max_width,
                                  gint max_height);

/**
 * @brief Clears the most recently rendered video area from the terminal.
 *
 * @note Call from the player-owning main context because this writes stdout.
 */
void video_player_clear_render_area(VideoPlayer *player);

/**
 * @brief Enables or disables the playback statistics overlay.
 *
 * This function is thread-safe and snapshots the value under the player state
 * mutex. Callers do not need to stop playback first.
 */
void video_player_set_show_stats(VideoPlayer *player, gboolean show_stats);

/**
 * @brief Updates color enhancement for the player and its renderer.
 *
 * This function is thread-safe and serializes renderer configuration changes
 * with render workers.
 */
void video_player_set_color_enhance(VideoPlayer *player, ColorEnhanceMode color_enhance);

/**
 * @brief Advances the renderer protocol override to the next display mode.
 *
 * The cycle order is text, sixel, iTerm2, kitty, then text. Renderer state is
 * synchronized internally and its terminal size is refreshed before return.
 *
 * @return TRUE when the caller should clear stale text output before drawing
 * the newly selected graphics protocol; FALSE otherwise or without a renderer.
 * @note Call from the player-owning main context while playback is stopped.
 */
gboolean video_player_cycle_protocol(VideoPlayer *player);

/**
 * @brief Returns a consistent snapshot of the last rendered frame bounds.
 *
 * Either output pointer may be NULL. Returns FALSE when no rendered frame has
 * positive bounds.
 */
gboolean video_player_get_last_frame_bounds(VideoPlayer *player,
                                             gint *top_row,
                                             gint *height);

/**
 * @brief Copies the cached text corresponding to a terminal row.
 *
 * The returned string is owned by the caller and must be freed with g_free().
 * Returns NULL when the row is outside the cached text frame.
 */
gchar *video_player_dup_cached_line_at_row(VideoPlayer *player, gint terminal_row);

/**
 * @brief Loads a video and initializes its FFmpeg decode state.
 *
 * @param filepath Borrowed path valid for the duration of the call.
 * @return ERROR_NONE on success, otherwise an ErrorCode describing failure.
 * @note Call from the player-owning main context while no competing control
 * operation is in progress.
 */
ErrorCode video_player_load(VideoPlayer *player, const gchar *filepath);

/** @brief Starts or resumes playback from the owning main context. */
ErrorCode video_player_play(VideoPlayer *player);

/** @brief Pauses playback and its GLib timer from the owning main context. */
ErrorCode video_player_pause(VideoPlayer *player);

/** @brief Stops playback, joins workers, and clears queued frames. */
ErrorCode video_player_stop(VideoPlayer *player);

/**
 * @brief Seeks relative to the current playback position.
 * @param delta_ms Signed seek offset in milliseconds.
 * @note Call from the player-owning main context.
 */
ErrorCode video_player_seek_relative_ms(VideoPlayer *player, gint64 delta_ms);

/**
 * @brief Returns whether playback is active.
 * @note Thread-safe; do not call while already holding state_mutex.
 */
gboolean video_player_is_playing(const VideoPlayer *player);

/**
 * @brief Returns whether a valid video stream is loaded.
 * @note Thread-safe; do not call while already holding state_mutex.
 */
gboolean video_player_has_video(const VideoPlayer *player);

/**
 * @brief Refreshes the attached renderer's terminal size.
 * @note Serialized with renderer replacement; call from the owning main
 * context because renderer terminal probing may perform terminal I/O.
 */
ErrorCode video_player_update_terminal_size(VideoPlayer *player);

/**
 * @brief Reads video dimensions without creating a persistent player.
 * @param filepath Borrowed input path.
 * @param width Output width in pixels.
 * @param height Output height in pixels.
 */
ErrorCode video_player_get_dimensions(const gchar *filepath, gint *width, gint *height);

/**
 * @brief Decodes the first video frame as caller-owned RGBA pixels.
 * @param pixels Receives memory that must be freed with g_free().
 * @param width Receives frame width in pixels.
 * @param height Receives frame height in pixels.
 * @param rowstride Receives the byte distance between rows.
 */
ErrorCode video_player_get_first_frame(const gchar *filepath,
                                       guint8 **pixels,
                                       gint *width,
                                       gint *height,
                                       gint *rowstride);

#endif // VIDEO_PLAYER_H
