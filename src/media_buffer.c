#include "media_buffer.h"

gboolean media_buffer_dimensions_within_limits(gint width, gint height) {
    if (width <= 0 || height <= 0) {
        g_debug("Rejecting media geometry with non-positive dimensions: width=%d height=%d", width, height);
        return FALSE;
    }

    gsize pixels = 0;
    if (!g_size_checked_mul(&pixels, (gsize)width, (gsize)height)) {
        g_debug("Rejecting media geometry with overflowing pixel count: width=%d height=%d", width, height);
        return FALSE;
    }
    if (pixels > PIXELTERM_MAX_DECODED_PIXELS) {
        g_debug("Rejecting media geometry over decoded pixel cap: pixels=%llu cap=%llu",
                (unsigned long long)pixels,
                (unsigned long long)PIXELTERM_MAX_DECODED_PIXELS);
        return FALSE;
    }
    return TRUE;
}

gboolean media_buffer_size_within_limits(gint height, gint rowstride, gsize *buffer_size_out) {
    if (buffer_size_out) {
        *buffer_size_out = 0;
    }
    if (height <= 0 || rowstride <= 0) {
        g_debug("Rejecting media buffer with non-positive layout: height=%d rowstride=%d", height, rowstride);
        return FALSE;
    }

    gsize buffer_size = 0;
    if (!g_size_checked_mul(&buffer_size, (gsize)height, (gsize)rowstride)) {
        g_debug("Rejecting media buffer with overflowing size: height=%d rowstride=%d", height, rowstride);
        return FALSE;
    }
    if (buffer_size > PIXELTERM_MAX_DECODED_BUFFER_BYTES) {
        g_debug("Rejecting media buffer over decoded byte cap: size=%llu cap=%llu",
                (unsigned long long)buffer_size,
                (unsigned long long)PIXELTERM_MAX_DECODED_BUFFER_BYTES);
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
                                      gint n_channels,
                                      gint bytes_per_channel,
                                      gsize *buffer_size_out) {
    if (buffer_size_out) {
        *buffer_size_out = 0;
    }
    if (n_channels <= 0 || bytes_per_channel <= 0 || !media_buffer_dimensions_within_limits(width, height)) {
        g_debug("Rejecting media layout metadata: width=%d height=%d channels=%d bytes_per_channel=%d",
                width,
                height,
                n_channels,
                bytes_per_channel);
        return FALSE;
    }

    gsize bytes_per_pixel = 0;
    if (!g_size_checked_mul(&bytes_per_pixel, (gsize)n_channels, (gsize)bytes_per_channel)) {
        g_debug("Rejecting media layout with overflowing bytes per pixel: channels=%d bytes_per_channel=%d",
                n_channels,
                bytes_per_channel);
        return FALSE;
    }

    gsize min_rowstride = 0;
    if (!g_size_checked_mul(&min_rowstride, (gsize)width, bytes_per_pixel)) {
        g_debug("Rejecting media layout with overflowing minimum rowstride: width=%d bytes_per_pixel=%llu",
                width,
                (unsigned long long)bytes_per_pixel);
        return FALSE;
    }
    if (rowstride <= 0 || (gsize)rowstride < min_rowstride) {
        g_debug("Rejecting media layout with short rowstride: rowstride=%d minimum=%llu",
                rowstride,
                (unsigned long long)min_rowstride);
        return FALSE;
    }

    return media_buffer_size_within_limits(height, rowstride, buffer_size_out);
}
