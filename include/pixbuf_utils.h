#ifndef PIXBUF_UTILS_H
#define PIXBUF_UTILS_H

#include <gdk-pixbuf/gdk-pixbuf.h>

GdkPixbuf* pixbuf_utils_load_from_stream(const char *filepath, GError **error);

#endif
