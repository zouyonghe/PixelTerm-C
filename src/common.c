#include "common.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <limits.h>

// Supported file format extensions
const char* const SUPPORTED_EXTENSIONS[] = {
    ".jpg", ".jpeg", ".png", ".apng", ".gif", ".webp", ".bmp", ".tiff", ".tif", NULL
};

const char* const SUPPORTED_VIDEO_EXTENSIONS[] = {
    ".mp4", ".mkv", ".avi", ".mov", ".webm", ".mpeg", ".mpg", ".m4v", NULL
};

const char* const SUPPORTED_BOOK_EXTENSIONS[] = {
    ".pdf", ".epub", ".cbz", NULL
};

typedef enum {
    IMAGE_MAGIC_UNKNOWN = 0,
    IMAGE_MAGIC_JPEG,
    IMAGE_MAGIC_PNG,
    IMAGE_MAGIC_GIF,
    IMAGE_MAGIC_WEBP,
    IMAGE_MAGIC_BMP,
    IMAGE_MAGIC_TIFF
} ImageMagicType;

static guint32 read_be32(const unsigned char *buf) {
    return ((guint32)buf[0] << 24) |
           ((guint32)buf[1] << 16) |
           ((guint32)buf[2] << 8) |
           (guint32)buf[3];
}

static guint32 read_le32(const unsigned char *buf) {
    return (guint32)buf[0] |
           ((guint32)buf[1] << 8) |
           ((guint32)buf[2] << 16) |
           ((guint32)buf[3] << 24);
}

static guint16 read_be16(const unsigned char *buf) {
    return (guint16)(((guint16)buf[0] << 8) | (guint16)buf[1]);
}

static guint16 read_le16(const unsigned char *buf) {
    return (guint16)((guint16)buf[0] | ((guint16)buf[1] << 8));
}

static ImageMagicType get_image_magic_type(const char *filepath) {
    if (!filepath) {
        return IMAGE_MAGIC_UNKNOWN;
    }

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return IMAGE_MAGIC_UNKNOWN;
    }

    unsigned char header[16];
    size_t bytes_read = fread(header, 1, sizeof(header), file);
    fclose(file);

    if (bytes_read < 4) {
        return IMAGE_MAGIC_UNKNOWN;
    }

    // JPEG (FF D8 FF)
    if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
        return IMAGE_MAGIC_JPEG;
    }

    // PNG (89 50 4E 47)
    if (bytes_read >= 8 &&
        header[0] == 0x89 && header[1] == 0x50 &&
        header[2] == 0x4E && header[3] == 0x47) {
        return IMAGE_MAGIC_PNG;
    }

    // GIF (GIF87a or GIF89a)
    if (bytes_read >= 6 &&
        header[0] == 'G' && header[1] == 'I' && header[2] == 'F' &&
        header[3] == '8' && (header[4] == '7' || header[4] == '9') &&
        header[5] == 'a') {
        return IMAGE_MAGIC_GIF;
    }

    // WebP (RIFF....WEBP)
    if (bytes_read >= 12 &&
        header[0] == 'R' && header[1] == 'I' &&
        header[2] == 'F' && header[3] == 'F' &&
        header[8] == 'W' && header[9] == 'E' &&
        header[10] == 'B' && header[11] == 'P') {
        return IMAGE_MAGIC_WEBP;
    }

    // BMP (BM)
    if (header[0] == 'B' && header[1] == 'M') {
        return IMAGE_MAGIC_BMP;
    }

    // TIFF (II*\0 or MM\0*)
    if (bytes_read >= 4 && (
        (header[0] == 'I' && header[1] == 'I' && header[2] == '*' && header[3] == '\0') ||
        (header[0] == 'M' && header[1] == 'M' && header[2] == '\0' && header[3] == '*')
    )) {
        return IMAGE_MAGIC_TIFF;
    }

    return IMAGE_MAGIC_UNKNOWN;
}

static gboolean png_has_animation(const char *filepath) {
    static const unsigned char png_sig[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return FALSE;
    }

    unsigned char sig[8];
    if (fread(sig, 1, sizeof(sig), file) != sizeof(sig)) {
        fclose(file);
        return FALSE;
    }
    if (memcmp(sig, png_sig, sizeof(png_sig)) != 0) {
        fclose(file);
        return FALSE;
    }

    while (TRUE) {
        unsigned char len_buf[4];
        unsigned char type_buf[4];
        if (fread(len_buf, 1, sizeof(len_buf), file) != sizeof(len_buf)) {
            break;
        }
        if (fread(type_buf, 1, sizeof(type_buf), file) != sizeof(type_buf)) {
            break;
        }

        guint32 len = read_be32(len_buf);
        if (memcmp(type_buf, "acTL", 4) == 0) {
            fclose(file);
            return TRUE;
        }
        if (memcmp(type_buf, "IDAT", 4) == 0) {
            fclose(file);
            return FALSE;
        }

        if (len > (guint32)(LONG_MAX - 4)) {
            break;
        }
        if (fseek(file, (long)len + 4, SEEK_CUR) != 0) {
            break;
        }
    }

    fclose(file);
    return FALSE;
}

static gboolean webp_has_animation(const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return FALSE;
    }

    unsigned char header[12];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return FALSE;
    }
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WEBP", 4) != 0) {
        fclose(file);
        return FALSE;
    }

    while (TRUE) {
        unsigned char chunk_hdr[8];
        if (fread(chunk_hdr, 1, sizeof(chunk_hdr), file) != sizeof(chunk_hdr)) {
            break;
        }

        if (memcmp(chunk_hdr, "ANIM", 4) == 0) {
            fclose(file);
            return TRUE;
        }

        guint32 chunk_size = read_le32(chunk_hdr + 4);
        guint32 skip = chunk_size + (chunk_size & 1);
        if (skip > (guint32)LONG_MAX) {
            break;
        }
        if (fseek(file, (long)skip, SEEK_CUR) != 0) {
            break;
        }
    }

    fclose(file);
    return FALSE;
}

static gboolean tiff_has_multiple_pages(const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return FALSE;
    }

    unsigned char header[8];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return FALSE;
    }

    gboolean little_endian = FALSE;
    if (header[0] == 'I' && header[1] == 'I') {
        little_endian = TRUE;
    } else if (header[0] == 'M' && header[1] == 'M') {
        little_endian = FALSE;
    } else {
        fclose(file);
        return FALSE;
    }

    guint16 magic = little_endian ? read_le16(header + 2) : read_be16(header + 2);
    if (magic != 42) {
        fclose(file);
        return FALSE;
    }

    guint32 ifd_offset = little_endian ? read_le32(header + 4) : read_be32(header + 4);
    if (ifd_offset == 0 || ifd_offset > (guint32)LONG_MAX) {
        fclose(file);
        return FALSE;
    }
    if (fseek(file, (long)ifd_offset, SEEK_SET) != 0) {
        fclose(file);
        return FALSE;
    }

    unsigned char count_buf[2];
    if (fread(count_buf, 1, sizeof(count_buf), file) != sizeof(count_buf)) {
        fclose(file);
        return FALSE;
    }
    guint16 count = little_endian ? read_le16(count_buf) : read_be16(count_buf);
    long next_offset_pos = (long)ifd_offset + 2L + (long)count * 12L;
    if (next_offset_pos < 0 || next_offset_pos > LONG_MAX - 4) {
        fclose(file);
        return FALSE;
    }
    if (fseek(file, next_offset_pos, SEEK_SET) != 0) {
        fclose(file);
        return FALSE;
    }

    unsigned char next_buf[4];
    if (fread(next_buf, 1, sizeof(next_buf), file) != sizeof(next_buf)) {
        fclose(file);
        return FALSE;
    }
    guint32 next_ifd = little_endian ? read_le32(next_buf) : read_be32(next_buf);
    fclose(file);
    return next_ifd != 0;
}

gboolean is_animated_image_candidate(const char *filepath) {
    if (!filepath) {
        return FALSE;
    }

    const char *ext = get_file_extension(filepath);
    if (ext) {
        if (g_ascii_strcasecmp(ext, ".gif") == 0) {
            return TRUE;
        }
        if (g_ascii_strcasecmp(ext, ".webp") == 0) {
            return webp_has_animation(filepath);
        }
        if (g_ascii_strcasecmp(ext, ".png") == 0 || g_ascii_strcasecmp(ext, ".apng") == 0) {
            return png_has_animation(filepath);
        }
        if (g_ascii_strcasecmp(ext, ".tif") == 0 || g_ascii_strcasecmp(ext, ".tiff") == 0) {
            return tiff_has_multiple_pages(filepath);
        }
        return FALSE;
    }

    ImageMagicType magic = get_image_magic_type(filepath);
    switch (magic) {
        case IMAGE_MAGIC_GIF:
            return TRUE;
        case IMAGE_MAGIC_WEBP:
            return webp_has_animation(filepath);
        case IMAGE_MAGIC_PNG:
            return png_has_animation(filepath);
        case IMAGE_MAGIC_TIFF:
            return tiff_has_multiple_pages(filepath);
        default:
            return FALSE;
    }
}

// Check if a file is an image based on its extension
gboolean is_image_file(const char *filename) {
    if (!filename) {
        return FALSE;
    }

    const char *ext = get_file_extension(filename);
    if (!ext) {
        // No extension, check by content
        return is_image_by_content(filename);
    }

    for (int i = 0; SUPPORTED_EXTENSIONS[i] != NULL; i++) {
        if (g_ascii_strcasecmp(ext, SUPPORTED_EXTENSIONS[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

// Check if a file is a video based on its extension
gboolean is_video_file(const char *filename) {
    if (!filename) {
        return FALSE;
    }

    const char *ext = get_file_extension(filename);
    if (!ext) {
        return FALSE;
    }

    for (int i = 0; SUPPORTED_VIDEO_EXTENSIONS[i] != NULL; i++) {
        if (g_ascii_strcasecmp(ext, SUPPORTED_VIDEO_EXTENSIONS[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

// Check if a file is an image or video based on its extension
gboolean is_media_file(const char *filename) {
    return is_image_file(filename) || is_video_file(filename);
}

// Check if a file is a book based on its extension
gboolean is_book_file(const char *filename) {
    if (!filename) {
        return FALSE;
    }

    const char *ext = get_file_extension(filename);
    if (!ext) {
        return FALSE;
    }

    for (int i = 0; SUPPORTED_BOOK_EXTENSIONS[i] != NULL; i++) {
        if (g_ascii_strcasecmp(ext, SUPPORTED_BOOK_EXTENSIONS[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

// Check if a file is a video by reading its magic numbers
static gboolean is_video_by_content(const char *filepath) {
    if (!filepath) {
        return FALSE;
    }

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return FALSE;
    }

    unsigned char header[16];
    size_t bytes_read = fread(header, 1, sizeof(header), file);
    fclose(file);

    if (bytes_read < 12) {
        return FALSE;
    }

    // WebM/Matroska EBML header (1A 45 DF A3)
    if (header[0] == 0x1A && header[1] == 0x45 && header[2] == 0xDF && header[3] == 0xA3) {
        return TRUE;
    }

    // MP4/MOV/ISO BMFF: 'ftyp' at offset 4
    if (header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p') {
        return TRUE;
    }

    // AVI: RIFF....AVI
    if (header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' &&
        header[8] == 'A' && header[9] == 'V' && header[10] == 'I') {
        return TRUE;
    }

    return FALSE;
}

// Check if a file is a valid video file (checks size, extension)
gboolean is_valid_video_file(const char *filepath) {
    if (!filepath) {
        return FALSE;
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        return FALSE;
    }

    if (st.st_size == 0) {
        return FALSE;
    }

    if (is_video_file(filepath)) {
        return TRUE;
    }

    return is_video_by_content(filepath);
}

// Check if a file is a valid image or video file
// NOTE: images still use content checks for robustness.
gboolean is_valid_media_file(const char *filepath) {
    if (!filepath) {
        return FALSE;
    }

    if (is_image_file(filepath)) {
        return is_valid_image_file(filepath);
    }

    return is_valid_video_file(filepath);
}

gboolean is_valid_book_file(const char *filepath) {
    if (!filepath) {
        return FALSE;
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        return FALSE;
    }

    if (st.st_size == 0) {
        return FALSE;
    }

    return is_book_file(filepath);
}

// Check if a file is an image by reading its magic numbers (for files without extensions)
gboolean is_image_by_content(const char *filepath) {
    return get_image_magic_type(filepath) != IMAGE_MAGIC_UNKNOWN;
}

// Check if a file is a valid image file (checks size, content, format)
gboolean is_valid_image_file(const char *filepath) {
    if (!filepath) {
        return FALSE;
    }

    // Check if file exists and get size
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return FALSE;
    }

    // Check if file size is 0 (invalid file)
    if (st.st_size == 0) {
        return FALSE;
    }

    // Check if it's an image file by extension or content
    if (!is_image_file(filepath)) {
        return FALSE;
    }

    // For non-zero files, we'll be more lenient and only check the file header
    // to avoid rejecting valid images that might have issues with gdk-pixbuf
    return is_image_by_content(filepath);
}

// Get file extension
const char* get_file_extension(const char *filename) {
    if (!filename) {
        return NULL;
    }

    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return NULL;
    }

    return dot;
}

// Check if file exists
gboolean file_exists(const char *path) {
    if (!path) {
        return FALSE;
    }

    struct stat st;
    return (stat(path, &st) == 0);
}

// Get file size
gint64 get_file_size(const char *path) {
    if (!path) {
        return -1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    return st.st_size;
}

// Get file modification time
gint64 get_file_mtime(const char *path) {
    if (!path) {
        return -1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    return st.st_mtime;
}

// Cleanup string helper
void cleanup_string(gchar **str) {
    if (str && *str) {
        g_free(*str);
        *str = NULL;
    }
}

// Cleanup GString helper
void cleanup_gstring(GString **str) {
    if (str && *str) {
        g_string_free(*str, TRUE);
        *str = NULL;
    }
}

// GString destroyer for GHashTable
void gstring_destroy(gpointer data) {
    if (data) {
        g_string_free((GString*)data, TRUE);
    }
}

// Terminal utilities
void get_terminal_size(gint *width, gint *height) {
    if (!width || !height) {
        return;
    }

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width = ws.ws_col > 0 ? ws.ws_col : 80;
        *height = ws.ws_row > 0 ? ws.ws_row : 24;
    } else {
        *width = 80;
        *height = 24;
    }
}

// Enhanced terminal size detection including pixel dimensions
void get_terminal_size_pixels(gint *width, gint *height, gint *pixel_width, gint *pixel_height) {
    if (!width || !height) {
        return;
    }

    // Start with safe defaults
    *width = 80;
    *height = 24;
    if (pixel_width) *pixel_width = -1;
    if (pixel_height) *pixel_height = -1;

    // Upper bounds to filter out clearly bogus ioctl responses
    const gint pixel_extent_max = 8192 * 3;

    struct winsize ws;
    gboolean have_winsz = FALSE;

    // Try stdout, then stderr, then stdin — mirrors chafa's order
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0
        || ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0
        || ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        have_winsz = TRUE;
    }

    // Fall back to the controlling TTY if all of the above failed (e.g. in pipes)
    if (!have_winsz) {
        const char *term_path = ctermid(NULL);
        if (term_path) {
            int fd = open(term_path, O_RDONLY);
            if (fd >= 0) {
                if (ioctl(fd, TIOCGWINSZ, &ws) == 0) {
                    have_winsz = TRUE;
                }
                close(fd);
            }
        }
    }

    if (have_winsz) {
        if (ws.ws_col > 0) *width = ws.ws_col;
        if (ws.ws_row > 0) *height = ws.ws_row;

        if (pixel_width && pixel_height) {
            gint xpx = ws.ws_xpixel;
            gint ypx = ws.ws_ypixel;

            // Sanity-check pixel values just like chafa does
            if (xpx > pixel_extent_max || ypx > pixel_extent_max
                || xpx <= 0 || ypx <= 0) {
                xpx = -1;
                ypx = -1;
            }

            *pixel_width = xpx;
            *pixel_height = ypx;
        }
    }
}

// Derive terminal cell geometry; falls back to 10x20 when pixel metrics are unavailable
void get_terminal_cell_geometry(gint *cell_width, gint *cell_height) {
    if (cell_width) *cell_width = 10;
    if (cell_height) *cell_height = 20;

    gint width = 0, height = 0, pixel_width = -1, pixel_height = -1;
    const gint cell_extent_px_max = 8192;

    get_terminal_size_pixels(&width, &height, &pixel_width, &pixel_height);

    if (pixel_width > 0 && pixel_height > 0 && width > 0 && height > 0) {
        gint cw = pixel_width / width;
        gint ch = pixel_height / height;

        if (cw > 0 && ch > 0 && cw < cell_extent_px_max && ch < cell_extent_px_max) {
            if (cell_width) *cell_width = cw;
            if (cell_height) *cell_height = ch;
        }
    }
}

// Calculate terminal cell aspect ratio from pixel dimensions
gdouble get_terminal_cell_aspect_ratio(void) {
    gint width, height, pixel_width, pixel_height;
    const gchar *konsole_ver = g_getenv("KONSOLE_VERSION");
    const gboolean is_konsole = (konsole_ver && *konsole_ver);
    const gdouble fallback = is_konsole ? 0.55 : 0.5; // Konsole tends to need a slightly wider glyph

    get_terminal_size_pixels(&width, &height, &pixel_width, &pixel_height);
    
    // If pixel dimensions are available and look sane, calculate aspect ratio
    if (pixel_width > 0 && pixel_height > 0 && width > 0 && height > 0) {
        gdouble pixel_width_per_cell = (gdouble)pixel_width / width;
        gdouble pixel_height_per_cell = (gdouble)pixel_height / height;

        // Guard against bogus terminal reports (e.g., zero or extreme values)
        if (pixel_width_per_cell > 0.0 && pixel_height_per_cell > 0.0 &&
            pixel_width_per_cell < 64.0 && pixel_height_per_cell < 64.0) {
            gdouble ratio = pixel_width_per_cell / pixel_height_per_cell;

            // Only accept reasonable ratios; otherwise fall back
            if (ratio > 0.25 && ratio < 4.0) {
                // Konsole sometimes reports a noticeably narrow ratio; gently widen it
                if (is_konsole && ratio < 0.6) {
                    return fallback;
                }
                return ratio;
            }
        }
    }
    
    // Fallback to default aspect ratio
    // Most terminals have character cells that are taller than they are wide
    return fallback;
}

// Error handling utilities
const gchar* error_code_to_string(ErrorCode error) {
    switch (error) {
        case ERROR_NONE: return "No error";
        case ERROR_FILE_NOT_FOUND: return "File not found";
        case ERROR_INVALID_IMAGE: return "Invalid image format";
        case ERROR_MEMORY_ALLOC: return "Memory allocation failed";
        case ERROR_CHAFA_INIT: return "Chafa initialization failed";
        case ERROR_THREAD_CREATE: return "Thread creation failed";
        case ERROR_TERMINAL_SIZE: return "Terminal size error";
        case ERROR_HELP_EXIT: return "Help requested";
        case ERROR_VERSION_EXIT: return "Version requested";
        case ERROR_INVALID_ARGS: return "Invalid arguments";
        default: return "Unknown error";
    }
}
