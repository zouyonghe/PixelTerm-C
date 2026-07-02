#include "kitty_graphics.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean kitty_graphics_env_truthy(const gchar *value) {
    if (!value || value[0] == '\0') {
        return FALSE;
    }
    return g_ascii_strcasecmp(value, "0") != 0 &&
           g_ascii_strcasecmp(value, "false") != 0 &&
           g_ascii_strcasecmp(value, "no") != 0 &&
           g_ascii_strcasecmp(value, "off") != 0;
}

gboolean kitty_graphics_shm_auto_enabled(void) {
    if (kitty_graphics_env_truthy(g_getenv("PIXELTERM_KITTY_SHM"))) {
        return TRUE;
    }

    if (g_getenv("SSH_CONNECTION") || g_getenv("SSH_CLIENT") || g_getenv("TMUX") || g_getenv("STY")) {
        return FALSE;
    }

    const gchar *term = g_getenv("TERM");
    const gchar *term_program = g_getenv("TERM_PROGRAM");
    return (term && g_strcmp0(term, "xterm-kitty") == 0) ||
           (term_program && g_ascii_strcasecmp(term_program, "kitty") == 0);
}

gboolean kitty_graphics_should_use_shm(KittyTransferMode mode) {
    if (mode == KITTY_TRANSFER_DIRECT) {
        return FALSE;
    }
    if (mode == KITTY_TRANSFER_SHM) {
        return TRUE;
    }
    return kitty_graphics_shm_auto_enabled();
}

GString *kitty_graphics_build_shm_command(const gchar *shm_name,
                                          gint width,
                                          gint height,
                                          gint display_width_cells,
                                          gint display_height_cells,
                                          gsize payload_size) {
    if (!shm_name || shm_name[0] == '\0' || width <= 0 || height <= 0 ||
        display_width_cells <= 0 || display_height_cells <= 0 || payload_size == 0) {
        return NULL;
    }

    gchar *encoded_name = g_base64_encode((const guchar *)shm_name, strlen(shm_name));
    if (!encoded_name) {
        return NULL;
    }

    GString *command = g_string_new(NULL);
    g_string_printf(command,
                    "\033_Ga=T,f=32,s=%d,v=%d,t=s,S=%" G_GSIZE_FORMAT ",c=%d,r=%d,C=1,q=2;%s\033\\",
                    width,
                    height,
                    payload_size,
                    display_width_cells,
                    display_height_cells,
                    encoded_name);
    g_free(encoded_name);
    return command;
}

static gchar *kitty_graphics_make_shm_name(void) {
    static gint counter = 0;
    gint serial = g_atomic_int_add(&counter, 1);
    return g_strdup_printf("/pixelterm-kitty-%ld-%d", (long)getpid(), serial);
}

static void kitty_graphics_get_transfer_size(gint src_width,
                                             gint src_height,
                                             gint display_width_cells,
                                             gint display_height_cells,
                                             gint *width_out,
                                             gint *height_out) {
    gint cell_width = 0;
    gint cell_height = 0;
    get_terminal_cell_geometry(&cell_width, &cell_height);

    gint target_width = src_width;
    gint target_height = src_height;
    if (cell_width > 0 && cell_height > 0 && display_width_cells > 0 && display_height_cells > 0) {
        target_width = display_width_cells * cell_width;
        target_height = display_height_cells * cell_height;
        if (target_width <= 0 || target_height <= 0) {
            target_width = src_width;
            target_height = src_height;
        }
    }

    /* Let the terminal upscale if needed; avoid increasing shm traffic beyond
     * the decoded frame's native pixel count.
     */
    if (target_width > src_width) target_width = src_width;
    if (target_height > src_height) target_height = src_height;
    if (target_width < 1) target_width = 1;
    if (target_height < 1) target_height = 1;

    if (width_out) *width_out = target_width;
    if (height_out) *height_out = target_height;
}

static void kitty_graphics_copy_scaled_rgba(guint8 *dest,
                                            gint dest_width,
                                            gint dest_height,
                                            const guint8 *src,
                                            gint src_width,
                                            gint src_height,
                                            gint src_rowstride) {
    for (gint y = 0; y < dest_height; y++) {
        gint src_y = (gint)(((gint64)y * src_height) / dest_height);
        if (src_y >= src_height) src_y = src_height - 1;
        const guint8 *src_row = src + ((gsize)src_y * (gsize)src_rowstride);
        guint8 *dest_row = dest + ((gsize)y * (gsize)dest_width * 4);
        for (gint x = 0; x < dest_width; x++) {
            gint src_x = (gint)(((gint64)x * src_width) / dest_width);
            if (src_x >= src_width) src_x = src_width - 1;
            memcpy(dest_row + ((gsize)x * 4), src_row + ((gsize)src_x * 4), 4);
        }
    }
}

void kitty_graphics_shm_unlink(const gchar *shm_name) {
    if (shm_name && shm_name[0] != '\0') {
        shm_unlink(shm_name);
    }
}

KittyGraphicsFrame *kitty_graphics_frame_new_shm_rgba(const guint8 *pixels,
                                                       gint width,
                                                       gint height,
                                                       gint rowstride,
                                                       gint display_width_cells,
                                                       gint display_height_cells) {
    if (!pixels || width <= 0 || height <= 0 || rowstride < width * 4 ||
        display_width_cells <= 0 || display_height_cells <= 0) {
        return NULL;
    }

    gint transfer_width = 0;
    gint transfer_height = 0;
    kitty_graphics_get_transfer_size(width,
                                     height,
                                     display_width_cells,
                                     display_height_cells,
                                     &transfer_width,
                                     &transfer_height);

    gsize payload_size = 0;
    if (!g_size_checked_mul(&payload_size, (gsize)transfer_width, (gsize)transfer_height) ||
        !g_size_checked_mul(&payload_size, payload_size, (gsize)4)) {
        return NULL;
    }

    gchar *shm_name = kitty_graphics_make_shm_name();
    if (!shm_name) {
        return NULL;
    }

    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        g_free(shm_name);
        return NULL;
    }

    if (ftruncate(fd, (off_t)payload_size) != 0) {
        shm_unlink(shm_name);
        close(fd);
        g_free(shm_name);
        return NULL;
    }

    void *mapped = mmap(NULL, payload_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        shm_unlink(shm_name);
        close(fd);
        g_free(shm_name);
        return NULL;
    }

    if (transfer_width == width && transfer_height == height) {
        guint8 *dest = mapped;
        for (gint y = 0; y < height; y++) {
            memcpy(dest + ((gsize)y * (gsize)width * 4), pixels + ((gsize)y * (gsize)rowstride), (gsize)width * 4);
        }
    } else {
        kitty_graphics_copy_scaled_rgba(mapped,
                                        transfer_width,
                                        transfer_height,
                                        pixels,
                                        width,
                                        height,
                                        rowstride);
    }
    munmap(mapped, payload_size);
    close(fd);

    GString *command = kitty_graphics_build_shm_command(shm_name,
                                                        transfer_width,
                                                        transfer_height,
                                                        display_width_cells,
                                                        display_height_cells,
                                                        payload_size);
    if (!command) {
        kitty_graphics_shm_unlink(shm_name);
        g_free(shm_name);
        return NULL;
    }

    KittyGraphicsFrame *frame = g_new0(KittyGraphicsFrame, 1);
    if (!frame) {
        kitty_graphics_shm_unlink(shm_name);
        g_free(shm_name);
        g_string_free(command, TRUE);
        return NULL;
    }
    frame->command = command;
    frame->shm_name = shm_name;
    frame->display_width_cells = display_width_cells;
    frame->display_height_cells = display_height_cells;
    return frame;
}

void kitty_graphics_frame_free(KittyGraphicsFrame *frame) {
    if (!frame) {
        return;
    }
    if (frame->command) {
        g_string_free(frame->command, TRUE);
    }
    if (frame->shm_name) {
        kitty_graphics_shm_unlink(frame->shm_name);
        g_free(frame->shm_name);
    }
    g_free(frame);
}
