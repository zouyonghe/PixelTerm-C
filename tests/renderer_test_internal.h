#ifndef TESTS_RENDERER_TEST_INTERNAL_H
#define TESTS_RENDERER_TEST_INTERNAL_H

#include "renderer.h"

/* Test-only declarations for renderer internals.
 * These are not part of the supported production API surface.
 */
guint8 *renderer_color_enhance_copy_for_test(const guint8 *pixel_data,
                                             gint width,
                                             gint height,
                                             gint rowstride,
                                             gint n_channels,
                                             ColorEnhanceMode mode);
gboolean renderer_validate_pixel_data_for_test(gint width,
                                               gint height,
                                               gint rowstride,
                                               gint n_channels,
                                               gsize *buffer_size);

#endif
