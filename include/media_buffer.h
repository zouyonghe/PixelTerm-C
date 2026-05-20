#ifndef MEDIA_BUFFER_H
#define MEDIA_BUFFER_H

#include "common.h"

gboolean media_buffer_dimensions_within_limits(gint width, gint height);
gboolean media_buffer_size_within_limits(gint height, gint rowstride, gsize *buffer_size_out);
gboolean media_buffer_validate_layout(gint width,
                                      gint height,
                                      gint rowstride,
                                      gint n_channels,
                                      gint bytes_per_channel,
                                      gsize *buffer_size_out);

#endif // MEDIA_BUFFER_H
