#define _GNU_SOURCE

#include "gif_player.h"
#include "common.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <chafa.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <gio/gio.h>

// Suppress deprecation warnings for GdkPixbufAnimation and GTimeVal
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

// 创建新的 GIF 播放器实例
GifPlayer* gif_player_new(gint work_factor, gboolean force_sixel) {
    GifPlayer *player = g_new0(GifPlayer, 1);
    if (!player) {
        return NULL;
    }

    player->is_playing = FALSE;
    player->is_animated = FALSE;
    player->current_frame = 0;
    player->total_frames = 0;
    player->frame_delay = 100;
    player->loop_count = 0;
    player->current_loop = 0;
    player->timer_id = 0;
    player->canvas = NULL;
    player->filepath = NULL;
    player->animation = NULL;
    player->iter = NULL;
    
    // Initialize internal renderer
    player->renderer = renderer_create();
    if (work_factor < 1) {
        work_factor = 1;
    } else if (work_factor > 9) {
        work_factor = 9;
    }

    if (player->renderer) {
        RendererConfig config = {
            .max_width = 80, // Will be updated on play
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
        renderer_initialize(player->renderer, &config);
    }

    return player;
}

// 设置渲染器 (Optional, allows sharing/overriding)
void gif_player_set_renderer(GifPlayer *player, ImageRenderer *renderer) {
    if (player) {
        // If we had an internal renderer and we're replacing it, we should arguably destroy the old one
        // IF we knew we owned it. For simplicity, let's assume this is called right after new() if at all.
        if (player->renderer && player->renderer != renderer) {
            renderer_destroy(player->renderer);
        }
        player->renderer = renderer;
    }
}

// 销毁 GIF 播放器
void gif_player_destroy(GifPlayer *player) {
    if (!player) {
        return;
    }

    gif_player_stop(player);
    
    g_free(player->filepath);
    if (player->canvas) {
        chafa_canvas_unref(player->canvas);
    }
    
    if (player->iter) {
        g_object_unref(player->iter);
    }
    
    if (player->animation) {
        g_object_unref(player->animation);
    }
    
    if (player->renderer) {
        renderer_destroy(player->renderer);
    }
    
    g_free(player);
}

// 加载 GIF 文件并分析动画信息
ErrorCode gif_player_load(GifPlayer *player, const gchar *filepath) {
    if (!player || !filepath) {
        return ERROR_INVALID_IMAGE;
    }
    
    // 停止之前的播放
    gif_player_stop(player);
    
    // 清理旧资源
    if (player->iter) {
        g_object_unref(player->iter);
        player->iter = NULL;
    }
    if (player->animation) {
        g_object_unref(player->animation);
        player->animation = NULL;
    }
    g_free(player->filepath);
    player->filepath = NULL;

    // 检查文件是否存在
    if (!file_exists(filepath)) {
        return ERROR_FILE_NOT_FOUND;
    }
    
    // 使用 GdkPixbuf 加载动画
    GError *error = NULL;
    GFile *file = g_file_new_for_path(filepath);
    GFileInputStream *stream = g_file_read(file, NULL, &error);
    g_object_unref(file);

    if (!stream) {
        if (error) g_error_free(error);
        return ERROR_FILE_NOT_FOUND;
    }

    player->animation = gdk_pixbuf_animation_new_from_stream(G_INPUT_STREAM(stream), NULL, &error);
    g_object_unref(stream);

    if (error) {
        g_error_free(error);
        return ERROR_INVALID_IMAGE;
    }
    
    player->is_animated = !gdk_pixbuf_animation_is_static_image(player->animation);
    player->filepath = g_strdup(filepath);
    
    // 初始化迭代器
    player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    
    return ERROR_NONE;
}

// 渲染当前帧的辅助函数
static void render_current_frame_internal(GifPlayer *player) {
    if (!player || !player->iter || !player->renderer) {
        return;
    }

    // Update terminal size for renderer to ensure correct scaling
    renderer_update_terminal_size(player->renderer);

    GdkPixbuf *frame = gdk_pixbuf_animation_iter_get_pixbuf(player->iter);
    if (!frame) return;

    // 获取图像属性
    gint width = gdk_pixbuf_get_width(frame);
    gint height = gdk_pixbuf_get_height(frame);
    gint rowstride = gdk_pixbuf_get_rowstride(frame);
    gint n_channels = gdk_pixbuf_get_n_channels(frame);
    guchar *pixels = gdk_pixbuf_get_pixels(frame);

    // 使用共享渲染器渲染图像数据
    GString *result = renderer_render_image_data(player->renderer, pixels, width, height, rowstride, n_channels);
    
    if (result) {
        // 移动光标到顶部，而不是清屏，减少闪烁
        printf("\033[H");
        printf("%s", result->str);
        
        // 清除图片下方的剩余内容（防止调整大小时出现残影）
        printf("\033[J");
        
        // 显示文件名
        gint term_w, term_h;
        renderer_get_rendered_dimensions(player->renderer, &term_w, &term_h);
        
        gchar *basename = g_path_get_basename(player->filepath);
        if (basename) {
            // 清理文件名中的控制字符
            for (gchar *p = basename; *p; ++p) {
                if ((unsigned char)*p < 0x20 || *p == 0x7f) *p = '?';
            }
            
            gint name_len = strlen(basename);
            gint start_col = (term_w - name_len) / 2;
            if (start_col < 0) start_col = 0;
            
            // 定位到图片下方
            printf("\033[%d;%dH", term_h + 1, start_col + 1);
            printf("\033[34m%s\033[0m\n", basename);
            
            g_free(basename);
        }
        
        g_string_free(result, TRUE);
        fflush(stdout);
    }
}

// 定时器回调函数
static gboolean render_next_frame(gpointer user_data) {
    GifPlayer *player = (GifPlayer *)user_data;
    
    if (!player || !player->is_playing || !player->iter) {
        player->timer_id = 0;
        return G_SOURCE_REMOVE;
    }
    
    // 更新迭代器到当前时间
    gdk_pixbuf_animation_iter_advance(player->iter, NULL);
    
    // 渲染新帧
    render_current_frame_internal(player);
    
    // 获取下一帧的延迟
    int delay = gdk_pixbuf_animation_iter_get_delay_time(player->iter);
    if (delay < 10) delay = 10; // 最小延迟保护
    
    // 重新安排定时器
    player->timer_id = g_timeout_add(delay, render_next_frame, player);
    
    // 返回 G_SOURCE_REMOVE 因为我们已经手动添加了新的定时器
    return G_SOURCE_REMOVE;
}

// 播放 GIF
ErrorCode gif_player_play(GifPlayer *player) {
    if (!player || !player->is_animated || !player->animation) {
        return ERROR_INVALID_IMAGE;
    }
    
    if (player->is_playing) {
        return ERROR_NONE;
    }
    
    // 确保迭代器存在
    if (!player->iter) {
        player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    }
    
    player->is_playing = TRUE;
    
    // 立即渲染第一帧
    render_current_frame_internal(player);
    
    // 安排下一帧
    int delay = gdk_pixbuf_animation_iter_get_delay_time(player->iter);
    if (delay < 10) delay = 10;
    
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
    }
    player->timer_id = g_timeout_add(delay, render_next_frame, player);
    
    return ERROR_NONE;
}

// 暂停播放
ErrorCode gif_player_pause(GifPlayer *player) {
    if (!player) {
        return ERROR_INVALID_IMAGE;
    }
    
    player->is_playing = FALSE;
    
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }
    
    return ERROR_NONE;
}

// 停止播放
ErrorCode gif_player_stop(GifPlayer *player) {
    if (!player) {
        return ERROR_INVALID_IMAGE;
    }
    
    player->is_playing = FALSE;
    
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }
    
    // 重置迭代器（如果需要回到开头）
    // 下次 play 或 load 会处理迭代器重置
    
    return ERROR_NONE;
}

// 检查是否正在播放
gboolean gif_player_is_playing(const GifPlayer *player) {
    return player && player->is_playing;
}

// 检查是否为动画 GIF
gboolean gif_player_is_animated(const GifPlayer *player) {
    return player && player->is_animated;
}

// Update terminal size for the internal renderer
ErrorCode gif_player_update_terminal_size(GifPlayer *player) {
    if (!player || !player->renderer) {
        return ERROR_INVALID_IMAGE;
    }
    return renderer_update_terminal_size(player->renderer);
}
