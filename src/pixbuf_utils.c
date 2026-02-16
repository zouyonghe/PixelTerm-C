#include "pixbuf_utils.h"

#include <gio/gio.h>

GdkPixbuf* pixbuf_utils_load_from_stream(const char *filepath, GError **error) {
    if (!filepath) {
        return NULL;
    }

    GFile *file = g_file_new_for_path(filepath);
    if (!file) {
        return NULL;
    }

    GFileInputStream *stream = g_file_read(file, NULL, error);
    g_object_unref(file);
    if (!stream) {
        return NULL;
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream(G_INPUT_STREAM(stream), NULL, error);
    g_object_unref(stream);
    return pixbuf;
}
