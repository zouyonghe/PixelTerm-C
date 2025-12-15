#ifndef GIF_PLAYER_H
#define GIF_PLAYER_H

#include "common.h"
#include "renderer.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

// GIF 动画播放器结构
typedef struct {
    gboolean is_playing;
    gboolean is_animated;
    gint current_frame;
    gint total_frames;
    gint frame_delay;  // in milliseconds
    gint loop_count;
    gint current_loop;
    guint timer_id;    // For storing timer source ID
    ChafaCanvas *canvas;  // Canvas for rendering frames
    gchar *filepath;
    
    // Animation state
    GdkPixbufAnimation *animation;
    GdkPixbufAnimationIter *iter;
    
    // Renderer reference
    ImageRenderer *renderer;
} GifPlayer;

// GIF 播放器函数
GifPlayer* gif_player_new(void);
void gif_player_destroy(GifPlayer *player);
void gif_player_set_renderer(GifPlayer *player, ImageRenderer *renderer);
ErrorCode gif_player_load(GifPlayer *player, const gchar *filepath);
ErrorCode gif_player_play(GifPlayer *player);
ErrorCode gif_player_pause(GifPlayer *player);
ErrorCode gif_player_stop(GifPlayer *player);
gboolean gif_player_is_playing(const GifPlayer *player);
gboolean gif_player_is_animated(const GifPlayer *player);

#endif // GIF_PLAYER_H