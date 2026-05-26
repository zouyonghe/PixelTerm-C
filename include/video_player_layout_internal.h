#ifndef VIDEO_PLAYER_LAYOUT_INTERNAL_H
#define VIDEO_PLAYER_LAYOUT_INTERNAL_H

#include "video_player.h"

/*
 * Internal render-layout module — owns terminal area sizing, line caching,
 * stats/io-tracking fields.
 *
 * All functions accept a VideoPlayer* and access only the layout fields:
 * render_area_*, render_term_*, last_frame_*, fixed_frame_*, last_frame_lines,
 * io_avg_ms/valid, last_present_us, last_presented_pts_ms, present_fps/valid,
 * show_stats, color_enhance, render_layout_generation.
 */

/* Queue depth constants */
#define VIDEO_PLAYER_QUEUE_DEPTH_SMALL_SIZE 8

/* Line cache */
void video_player_clear_line_cache(VideoPlayer *player);

/* I/O timing */
void video_player_update_io_avg(VideoPlayer *player, gint64 io_ms);
void video_player_update_present_fps(VideoPlayer *player, gint64 now_us);

/* Queue depth (reads render dimensions) */
void video_player_update_queue_depth(VideoPlayer *player, gint rendered_w, gint rendered_h);

/* Pixel mode label */
const char *video_player_pixel_mode_label(ChafaPixelMode mode);

#endif /* VIDEO_PLAYER_LAYOUT_INTERNAL_H */
