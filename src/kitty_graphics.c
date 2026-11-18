#include "kitty_graphics.h"
#include "process_env.h"
#include "media_buffer.h"
#include "terminal_probe.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __ANDROID__
/* Android (bionic) lacks POSIX shm_open/shm_unlink. Emulate them with
 * regular files in the process temp directory; behavior within this
 * process is equivalent (named file, ftruncate + MAP_SHARED mmap).
 *
 * oflag is passed through to open() on a regular file, which preserves
 * POSIX shm semantics: O_CREAT|O_EXCL still fails with EEXIST when the
 * file already exists, and O_TRUNC/O_APPEND behave as specified. */
static gchar *pixelterm_shm_path(const char *name) {
    /* shm names start with '/'; strip it and normalize any remaining
     * '/' to '_' so the file is always flat inside the temp directory
     * and no (possibly missing) subdirectory is ever referenced. */
    const char *name_ptr = (name && name[0] == '/') ? name + 1 : name;
    gchar *filename = g_strdup_printf("pixelterm-shm-%s", name_ptr ? name_ptr : "");
    g_strdelimit(filename, "/", '_');
    gchar *path = g_build_filename(g_get_tmp_dir(), filename, NULL);
    g_free(filename);
    return path;
}

static int pixelterm_shm_open(const char *name, int oflag, mode_t mode) {
    gchar *path = pixelterm_shm_path(name);
    int fd = open(path, oflag, mode);
    g_free(path);
    return fd;
}

static int pixelterm_shm_unlink(const char *name) {
    gchar *path = pixelterm_shm_path(name);
    int result = unlink(path);
    g_free(path);
    return result;
}
#else
static int pixelterm_shm_open(const char *name, int oflag, mode_t mode) {
    return shm_open(name, oflag, mode);
}

static int pixelterm_shm_unlink(const char *name) {
    return shm_unlink(name);
}
#endif

/* Keep shared-memory transfers bounded; larger frames fall back to direct kitty output. */
#define KITTY_GRAPHICS_SHM_MAX_PAYLOAD_BYTES (8 * 1024 * 1024)

#ifndef __ANDROID__
static gboolean kitty_graphics_env_truthy(const gchar *value) {
    if (!value || value[0] == '\0') {
        return FALSE;
    }
    return g_ascii_strcasecmp(value, "0") != 0 &&
           g_ascii_strcasecmp(value, "false") != 0 &&
           g_ascii_strcasecmp(value, "no") != 0 &&
           g_ascii_strcasecmp(value, "off") != 0;
}
#endif

gboolean kitty_graphics_shm_auto_enabled(void) {
#ifdef __ANDROID__
    return FALSE;
#else
    if (kitty_graphics_env_truthy(pixelterm_getenv("PIXELTERM_KITTY_SHM"))) {
        return TRUE;
    }

    if (pixelterm_getenv("SSH_CONNECTION") || pixelterm_getenv("SSH_CLIENT") ||
        pixelterm_getenv("TMUX") || pixelterm_getenv("STY")) {
        return FALSE;
    }

    const gchar *term = pixelterm_getenv("TERM");
    const gchar *term_program = pixelterm_getenv("TERM_PROGRAM");
    return (term && g_strcmp0(term, "xterm-kitty") == 0) ||
           (term_program && g_ascii_strcasecmp(term_program, "kitty") == 0);
#endif
}

gboolean kitty_graphics_should_use_shm(KittyTransferMode mode) {
    if (mode == KITTY_TRANSFER_DIRECT) {
        return FALSE;
    }
#ifdef __ANDROID__
    return FALSE;
#else
    if (mode == KITTY_TRANSFER_SHM) {
        return TRUE;
    }
    return kitty_graphics_shm_auto_enabled();
#endif
}

/* The main input/output owner services this queue; render workers never use it. */
static GQueue pending_deletes = G_QUEUE_INIT;
static gboolean cleanup_registered = FALSE;

gboolean kitty_graphics_retry_pending_deletes(void) {
    while (!g_queue_is_empty(&pending_deletes)) {
        GString *command = g_queue_peek_head(&pending_deletes);
        clearerr(stdout);
        gsize written = fwrite(command->str, 1, command->len, stdout);
        gint flushed = fflush(stdout);
        if (written != command->len || flushed != 0 || ferror(stdout)) return FALSE;
        g_string_free(g_queue_pop_head(&pending_deletes), TRUE);
    }
    return TRUE;
}

static void kitty_graphics_cleanup_at_exit(void) {
    kitty_graphics_retry_pending_deletes();
    /* Process exit is the last retry opportunity, even if the terminal is gone. */
    while (!g_queue_is_empty(&pending_deletes)) {
        g_string_free(g_queue_pop_head(&pending_deletes), TRUE);
    }
}

static void kitty_graphics_queue_delete(GString *command) {
    if (!cleanup_registered) {
        cleanup_registered = atexit(kitty_graphics_cleanup_at_exit) == 0;
    }
    g_queue_push_tail(&pending_deletes, command);
}

void kitty_graphics_defer_image_delete(guint32 image_id) {
    if (!image_id) return;
    GString *command = kitty_graphics_build_delete_image_command(image_id);
    g_string_prepend(command, "\033\\"); /* Finish an interrupted graphics command first. */
    kitty_graphics_queue_delete(command);
}

guint32 kitty_graphics_allocate_image_id(void) {
    if (!kitty_graphics_retry_pending_deletes()) return 0;
    /* I= requests a fresh terminal-assigned i=; the nonce only correlates replies. */
    guint32 nonce = g_random_int();
    if (!nonce) nonce = 1;
    gchar query[160], response[1024] = {0};
    gboolean query_written = FALSE;
    g_snprintf(query, sizeof(query),
               "\033_Ga=t,f=32,s=1,v=1,I=%u;AAAAAA==\033\\\033[5n", nonce);
    gint timeout = pixelterm_getenv("SSH_CONNECTION") || pixelterm_getenv("SSH_CLIENT") ?
        TERMINAL_PROBE_SSH_TIMEOUT_MS : TERMINAL_PROBE_DEFAULT_TIMEOUT_MS;
    if (fflush(stdout) != 0) {
        return 0;
    }
    gssize response_length = terminal_probe_query_response_tracked(query, timeout,
        TERMINAL_PROBE_POLL_TIMEOUT_MS, 'n', response, sizeof(response), &query_written);
    if (response_length <= 0) response[0] = '\0';
    const gchar *cursor = response;
    while ((cursor = strstr(cursor, "\033_G")) != NULL) {
        const gchar *end = strstr(cursor, "\033\\");
        const gchar *separator = strchr(cursor, ';');
        if (!end || !separator) break;
        if (separator < end && end - separator == 3 && memcmp(separator + 1, "OK", 2) == 0) {
            gchar *control = g_strndup(cursor + 3, separator - (cursor + 3));
            gchar **fields = g_strsplit(control, ",", -1);
            guint64 id = 0, number = 0;
            gboolean valid = TRUE;
            for (gsize i = 0; fields[i]; i++) {
                if (g_str_has_prefix(fields[i], "i=") || g_str_has_prefix(fields[i], "I=")) {
                    guint64 *value = fields[i][0] == 'i' ? &id : &number;
                    if (*value || !g_ascii_string_to_unsigned(fields[i] + 2, 10, 1,
                                                              G_MAXUINT32, value, NULL)) {
                        valid = FALSE;
                        break;
                    }
                }
            }
            g_strfreev(fields);
            g_free(control);
            if (valid && id && number == nonce) return (guint32)id;
        }
        cursor = end + 2;
    }
    if (query_written) {
        /* Image numbers are shared across clients; without a confirmed i=, a
         * d=N delete could target another client's newer image. Only end the APC. */
        kitty_graphics_queue_delete(g_string_new("\033\\"));
        kitty_graphics_retry_pending_deletes();
    }
    return 0;
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
                    // cppcheck-suppress unknownMacro
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

GString *kitty_graphics_build_animation_root_shm_command(const gchar *shm_name,
                                                         gint width,
                                                         gint height,
                                                         gint display_width_cells,
                                                         gint display_height_cells,
                                                         gsize payload_size,
                                                         guint32 image_id) {
    if (!shm_name || shm_name[0] == '\0' || width <= 0 || height <= 0 ||
        display_width_cells <= 0 || display_height_cells <= 0 ||
        payload_size == 0 || image_id == 0) {
        return NULL;
    }

    gchar *encoded_name = g_base64_encode((const guchar *)shm_name, strlen(shm_name));
    if (!encoded_name) {
        return NULL;
    }

    GString *command = g_string_new(NULL);
    g_string_printf(command,
                    // cppcheck-suppress unknownMacro
                    "\033_Ga=T,f=32,s=%d,v=%d,t=s,S=%" G_GSIZE_FORMAT ",c=%d,r=%d,C=1,i=%u,q=2;%s\033\\",
                    width,
                    height,
                    payload_size,
                    display_width_cells,
                    display_height_cells,
                    image_id,
                    encoded_name);
    g_free(encoded_name);
    return command;
}

GString *kitty_graphics_build_animation_frame_shm_command(const gchar *shm_name,
                                                          gint width,
                                                          gint height,
                                                          gsize payload_size,
                                                          guint32 image_id,
                                                          gint delay_ms) {
    if (!shm_name || shm_name[0] == '\0' || width <= 0 || height <= 0 ||
        payload_size == 0 || image_id == 0 || delay_ms <= 0) {
        return NULL;
    }

    gchar *encoded_name = g_base64_encode((const guchar *)shm_name, strlen(shm_name));
    if (!encoded_name) {
        return NULL;
    }

    GString *command = g_string_new(NULL);
    g_string_printf(command,
                    // cppcheck-suppress unknownMacro
                    "\033_Ga=f,f=32,s=%d,v=%d,t=s,S=%" G_GSIZE_FORMAT ",i=%u,z=%d,X=1,q=2;%s\033\\",
                    width,
                    height,
                    payload_size,
                    image_id,
                    delay_ms,
                    encoded_name);
    g_free(encoded_name);
    return command;
}

GString *kitty_graphics_build_animation_frame_delay_command(guint32 image_id,
                                                            guint32 frame_number,
                                                            gint delay_ms) {
    if (image_id == 0 || frame_number == 0 || delay_ms <= 0) {
        return NULL;
    }
    GString *command = g_string_new(NULL);
    g_string_printf(command,
                    "\033_Ga=a,i=%u,r=%u,z=%d,q=2\033\\",
                    image_id,
                    frame_number,
                    delay_ms);
    return command;
}

GString *kitty_graphics_build_animation_play_command(guint32 image_id) {
    if (image_id == 0) {
        return NULL;
    }
    GString *command = g_string_new(NULL);
    g_string_printf(command, "\033_Ga=a,i=%u,s=3,v=1,q=2\033\\", image_id);
    return command;
}

GString *kitty_graphics_build_animation_loading_command(guint32 image_id) {
    if (image_id == 0) {
        return NULL;
    }
    GString *command = g_string_new(NULL);
    g_string_printf(command, "\033_Ga=a,i=%u,s=2,q=2\033\\", image_id);
    return command;
}

GString *kitty_graphics_build_animation_stop_command(guint32 image_id) {
    if (image_id == 0) {
        return NULL;
    }
    GString *command = g_string_new(NULL);
    g_string_printf(command, "\033_Ga=a,i=%u,s=1,q=2\033\\", image_id);
    return command;
}

GString *kitty_graphics_build_delete_image_command(guint32 image_id) {
    if (image_id == 0) {
        return NULL;
    }
    GString *command = g_string_new(NULL);
    g_string_printf(command, "\033_Ga=d,d=I,i=%u,q=2\033\\", image_id);
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
        pixelterm_shm_unlink(shm_name);
    }
}

static KittyGraphicsFrame *kitty_graphics_frame_new_shm_rgba_internal(const guint8 *pixels,
                                                                      gint width,
                                                                      gint height,
                                                                      gint rowstride,
                                                                      gint display_width_cells,
                                                                      gint display_height_cells,
                                                                      guint32 image_id,
                                                                      gint delay_ms,
                                                                      gboolean animation_frame) {
    gsize required_rowstride = 0;
    if (!pixels || width <= 0 || height <= 0 || rowstride <= 0 ||
        !g_size_checked_mul(&required_rowstride, (gsize)width, (gsize)4) ||
        required_rowstride > (gsize)G_MAXINT || (gsize)rowstride < required_rowstride ||
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
    if (payload_size > KITTY_GRAPHICS_SHM_MAX_PAYLOAD_BYTES) {
        return NULL;
    }

    gchar *shm_name = kitty_graphics_make_shm_name();
    if (!shm_name) {
        return NULL;
    }

    int flags = O_CREAT | O_EXCL | O_RDWR;
#if defined(O_CLOEXEC) && !defined(__APPLE__)
    flags |= O_CLOEXEC;
#endif

    int fd = pixelterm_shm_open(shm_name, flags, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        g_free(shm_name);
        return NULL;
    }

#if !defined(O_CLOEXEC) || defined(__APPLE__)
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
        pixelterm_shm_unlink(shm_name);
        close(fd);
        g_free(shm_name);
        return NULL;
    }
#endif

    if (ftruncate(fd, (off_t)payload_size) != 0) {
        pixelterm_shm_unlink(shm_name);
        close(fd);
        g_free(shm_name);
        return NULL;
    }

    void *mapped = mmap(NULL, payload_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        pixelterm_shm_unlink(shm_name);
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

    GString *command = NULL;
    if (animation_frame) {
        command = kitty_graphics_build_animation_frame_shm_command(shm_name,
                                                                   transfer_width,
                                                                   transfer_height,
                                                                   payload_size,
                                                                   image_id,
                                                                   delay_ms);
    } else if (image_id != 0) {
        command = kitty_graphics_build_animation_root_shm_command(shm_name,
                                                                  transfer_width,
                                                                  transfer_height,
                                                                  display_width_cells,
                                                                  display_height_cells,
                                                                  payload_size,
                                                                  image_id);
    } else {
        command = kitty_graphics_build_shm_command(shm_name,
                                                   transfer_width,
                                                   transfer_height,
                                                   display_width_cells,
                                                   display_height_cells,
                                                   payload_size);
    }
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

KittyGraphicsFrame *kitty_graphics_frame_new_shm_rgba(const guint8 *pixels,
                                                       gint width,
                                                       gint height,
                                                       gint rowstride,
                                                       gint display_width_cells,
                                                       gint display_height_cells) {
    return kitty_graphics_frame_new_shm_rgba_internal(pixels,
                                                      width,
                                                      height,
                                                      rowstride,
                                                      display_width_cells,
                                                      display_height_cells,
                                                      0,
                                                      0,
                                                      FALSE);
}

void kitty_graphics_get_direct_size(gint width, gint height, gint64 bound_width,
                                    gint64 bound_height, gint *width_out, gint *height_out) {
    gdouble scale = 1.0;
    if (width <= 0 || height <= 0) {
        *width_out = *height_out = 0;
        return;
    }
    if (bound_width > 0) scale = MIN(scale, (gdouble)bound_width / width);
    if (bound_height > 0) scale = MIN(scale, (gdouble)bound_height / height);
    scale = MIN(scale, sqrt((gdouble)KITTY_GRAPHICS_DIRECT_MAX_BYTES / (3.0 * width * height)));
    scale = MIN(scale, (KITTY_GRAPHICS_DIRECT_MAX_BYTES / 3.0) / MAX(width, height));
    *width_out = MAX(1, (gint)(width * scale));
    *height_out = MAX(1, (gint)(height * scale));
}

KittyGraphicsFrame *kitty_graphics_frame_new_direct_rgb(const guint8 *pixels,
                                                        gint width,
                                                        gint height,
                                                        gint rowstride,
                                                        gint display_width_cells,
                                                        gint display_height_cells,
                                                        guint32 image_id) {
    gsize min_rowstride = 0;
    gsize payload_size = 0;
    if (!pixels || width <= 0 || height <= 0 || rowstride <= 0 ||
        display_width_cells <= 0 || display_height_cells <= 0 || image_id == 0 ||
        !g_size_checked_mul(&min_rowstride, (gsize)width, (gsize)3) ||
        min_rowstride > G_MAXINT || (gsize)rowstride < min_rowstride ||
        !g_size_checked_mul(&payload_size, min_rowstride, (gsize)height) ||
        payload_size > KITTY_GRAPHICS_DIRECT_MAX_BYTES ||
        !media_buffer_validate_layout(width, height, rowstride, 3, 1, NULL)) {
        return NULL;
    }

    GByteArray *packed = NULL;
    if ((gsize)rowstride != min_rowstride) {
        packed = g_byte_array_sized_new(payload_size);
        for (gint y = 0; y < height; y++) {
            g_byte_array_append(packed, pixels + (gsize)y * rowstride, (guint)min_rowstride);
        }
        pixels = packed->data;
    }
    gchar *encoded = g_base64_encode(pixels, payload_size);
    if (packed) g_byte_array_unref(packed);
    if (!encoded) {
        return NULL;
    }

    GString *command = g_string_sized_new(strlen(encoded) + 128);
    gsize encoded_len = strlen(encoded);
    for (gsize offset = 0; offset < encoded_len;) {
        gsize remaining = encoded_len - offset;
        gsize chunk_size = MIN(remaining, (gsize)4096);
        gboolean more = offset + chunk_size < encoded_len;
        if (more) {
            chunk_size -= chunk_size % 4;
        }

        if (offset == 0) {
            g_string_append_printf(command,
                                   "\033_Ga=T,f=24,s=%d,v=%d,c=%d,r=%d,i=%u,C=1,m=%d,q=2;",
                                   width, height, display_width_cells,
                                   display_height_cells, image_id, more ? 1 : 0);
        } else {
            g_string_append_printf(command, "\033_Gm=%d;", more ? 1 : 0);
        }
        g_string_append_len(command, encoded + offset, chunk_size);
        g_string_append(command, "\033\\");
        offset += chunk_size;
    }
    g_free(encoded);

    KittyGraphicsFrame *frame = g_new0(KittyGraphicsFrame, 1);
    if (!frame) {
        g_string_free(command, TRUE);
        return NULL;
    }
    frame->command = command;
    frame->display_width_cells = display_width_cells;
    frame->display_height_cells = display_height_cells;
    return frame;
}

KittyGraphicsFrame *kitty_graphics_animation_root_new_shm_rgba(const guint8 *pixels,
                                                               gint width,
                                                               gint height,
                                                               gint rowstride,
                                                               gint display_width_cells,
                                                               gint display_height_cells,
                                                               guint32 image_id) {
    if (image_id == 0) {
        return NULL;
    }
    return kitty_graphics_frame_new_shm_rgba_internal(pixels,
                                                      width,
                                                      height,
                                                      rowstride,
                                                      display_width_cells,
                                                      display_height_cells,
                                                      image_id,
                                                      0,
                                                      FALSE);
}

KittyGraphicsFrame *kitty_graphics_animation_frame_new_shm_rgba(const guint8 *pixels,
                                                                gint width,
                                                                gint height,
                                                                gint rowstride,
                                                                gint display_width_cells,
                                                                gint display_height_cells,
                                                                guint32 image_id,
                                                                gint delay_ms) {
    if (image_id == 0 || delay_ms <= 0) {
        return NULL;
    }
    return kitty_graphics_frame_new_shm_rgba_internal(pixels,
                                                      width,
                                                      height,
                                                      rowstride,
                                                      display_width_cells,
                                                      display_height_cells,
                                                      image_id,
                                                      delay_ms,
                                                      TRUE);
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
