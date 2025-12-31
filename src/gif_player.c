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

// 设置渲染区域，避免覆盖 UI
void gif_player_set_render_area(GifPlayer *player,
                                gint term_width,
                                gint term_height,
                                gint area_top_row,
                                gint area_height,
                                gint max_width,
                                gint max_height) {
    if (!player) {
        return;
    }

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
    player->fixed_frame_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    
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
    if (player->render_layout_valid) {
        player->renderer->config.max_width = player->render_max_width;
        player->renderer->config.max_height = player->render_max_height;
    }

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
        if (player->render_layout_valid && player->render_area_top_row > 0 && player->render_area_height > 0) {
            gint term_w = player->render_term_width > 0 ? player->render_term_width : player->render_max_width;
            gint term_h = player->render_term_height;
            gint area_top = player->render_area_top_row;
            gint area_bottom = area_top + player->render_area_height - 1;
            if (term_h > 0 && area_bottom > term_h) {
                area_bottom = term_h;
            }

            gint rendered_w = 0, rendered_h = 0;
            renderer_get_rendered_dimensions(player->renderer, &rendered_w, &rendered_h);
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
            // Clear only rows that were previously used but are no longer covered.
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
            // Fallback to full-frame rendering when layout is unavailable.
            printf("\033[H");
            printf("%s", result->str);
            printf("\033[J");
            player->last_frame_top_row = 0;
            player->last_frame_height = 0;
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
    player->fixed_frame_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    
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
