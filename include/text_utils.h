#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <glib.h>

gchar* sanitize_for_terminal(const gchar *text);
gint utf8_display_width(const gchar *text);
gchar* utf8_prefix_by_width(const gchar *text, gint max_width);
gchar* utf8_suffix_by_width(const gchar *text, gint max_width);
gchar* truncate_utf8_for_display(const gchar *text, gint max_width);
gchar* truncate_utf8_middle_keep_suffix(const gchar *text, gint max_width);

#endif
