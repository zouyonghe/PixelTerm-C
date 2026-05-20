#include "media_buffer.h"

gboolean media_buffer_dimensions_within_limits(gint width, gint height) {
    if (width <= 0 || height <= 0) {
        return FALSE;
    }

    gsize pixels = 0;
    if (!g_size_checked_mul(&pixels, (gsize)width, (gsize)height)) {
        return FALSE;
    }
    return pixels <= PIXELTERM_MAX_DECODED_PIXELS;
}

gboolean media_buffer_size_within_limits(gint height, gint rowstride, gsize *buffer_size_out) {
    if (buffer_size_out) {
        *buffer_size_out = 0;
    }
    if (height <= 0 || rowstride <= 0) {
        return FALSE;
    }

    gsize buffer_size = 0;
    if (!g_size_checked_mul(&buffer_size, (gsize)height, (gsize)rowstride)) {
        return FALSE;
    }
    if (buffer_size > PIXELTERM_MAX_DECODED_BUFFER_BYTES) {
        return FALSE;
    }
    if (buffer_size_out) {
        *buffer_size_out = buffer_size;
    }
    return TRUE;
}

gboolean media_buffer_validate_layout(gint width,
                                      gint height,
                                      gint rowstride,
                                      gint bytes_per_pixel,
                                      gsize *buffer_size_out) {
    if (buffer_size_out) {
        *buffer_size_out = 0;
    }
    if (bytes_per_pixel <= 0 || !media_buffer_dimensions_within_limits(width, height)) {
        return FALSE;
    }

    gsize min_rowstride = 0;
    if (!g_size_checked_mul(&min_rowstride, (gsize)width, (gsize)bytes_per_pixel)) {
        return FALSE;
    }
    if (rowstride <= 0 || (gsize)rowstride < min_rowstride) {
        return FALSE;
    }

    return media_buffer_size_within_limits(height, rowstride, buffer_size_out);
}
