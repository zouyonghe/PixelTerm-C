#include "media_buffer.h"

#include <stdarg.h>

#define MEDIA_BUFFER_REJECTION_LOG_LIMIT 32

static gint media_buffer_rejection_log_count = 0;

static void media_buffer_debug_reject(const gchar *format, ...) {
    gint previous_count = g_atomic_int_add(&media_buffer_rejection_log_count, 1);
    if (previous_count > MEDIA_BUFFER_REJECTION_LOG_LIMIT) {
        return;
    }
    if (previous_count == MEDIA_BUFFER_REJECTION_LOG_LIMIT) {
        g_debug("Further media buffer rejection logs suppressed");
        return;
    }

    va_list args;
    va_start(args, format);
    g_logv(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

gboolean media_buffer_dimensions_within_limits(gint width, gint height) {
    if (width <= 0 || height <= 0) {
        media_buffer_debug_reject("Rejecting media geometry with non-positive dimensions: width=%d height=%d", width, height);
        return FALSE;
    }

    gsize pixels = 0;
    if (!g_size_checked_mul(&pixels, (gsize)width, (gsize)height)) {
        media_buffer_debug_reject("Rejecting media geometry with overflowing pixel count: width=%d height=%d", width, height);
        return FALSE;
    }
    if (pixels > PIXELTERM_MAX_DECODED_PIXELS) {
        // cppcheck-suppress unknownMacro
        media_buffer_debug_reject("Rejecting media geometry over decoded pixel cap: pixels=%" G_GSIZE_FORMAT " cap=%" G_GSIZE_FORMAT,
                                  pixels,
                                  (gsize)PIXELTERM_MAX_DECODED_PIXELS);
        return FALSE;
    }
    return TRUE;
}

gboolean media_buffer_size_within_limits(gint height, gint rowstride, gsize *buffer_size_out) {
    if (buffer_size_out) {
        *buffer_size_out = 0;
    }
    if (height <= 0 || rowstride <= 0) {
        media_buffer_debug_reject("Rejecting media buffer with non-positive layout: height=%d rowstride=%d", height, rowstride);
        return FALSE;
    }

    gsize buffer_size = 0;
    if (!g_size_checked_mul(&buffer_size, (gsize)height, (gsize)rowstride)) {
        media_buffer_debug_reject("Rejecting media buffer with overflowing size: height=%d rowstride=%d", height, rowstride);
        return FALSE;
    }
    if (buffer_size > PIXELTERM_MAX_DECODED_BUFFER_BYTES) {
        // cppcheck-suppress unknownMacro
        media_buffer_debug_reject("Rejecting media buffer over decoded byte cap: size=%" G_GSIZE_FORMAT " cap=%" G_GSIZE_FORMAT,
                                  buffer_size,
                                  (gsize)PIXELTERM_MAX_DECODED_BUFFER_BYTES);
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
        media_buffer_debug_reject("Rejecting media layout metadata: width=%d height=%d channels=%d bytes_per_channel=%d",
                                  width,
                                  height,
                                  n_channels,
                                  bytes_per_channel);
        return FALSE;
    }

    gsize bytes_per_pixel = 0;
    if (!g_size_checked_mul(&bytes_per_pixel, (gsize)n_channels, (gsize)bytes_per_channel)) {
        media_buffer_debug_reject("Rejecting media layout with overflowing bytes per pixel: channels=%d bytes_per_channel=%d",
                                  n_channels,
                                  bytes_per_channel);
        return FALSE;
    }

    gsize min_rowstride = 0;
    if (!g_size_checked_mul(&min_rowstride, (gsize)width, bytes_per_pixel)) {
        // cppcheck-suppress unknownMacro
        media_buffer_debug_reject("Rejecting media layout with overflowing minimum rowstride: width=%d bytes_per_pixel=%" G_GSIZE_FORMAT,
                                  width,
                                  bytes_per_pixel);
        return FALSE;
    }
    if (rowstride <= 0 || (gsize)rowstride < min_rowstride) {
        // cppcheck-suppress unknownMacro
        media_buffer_debug_reject("Rejecting media layout with short rowstride: rowstride=%d minimum=%" G_GSIZE_FORMAT,
                                  rowstride,
                                  min_rowstride);
        return FALSE;
    }

    return media_buffer_size_within_limits(height, rowstride, buffer_size_out);
}
