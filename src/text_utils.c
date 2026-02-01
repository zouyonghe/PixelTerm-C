#include "text_utils.h"

#include <string.h>

gchar* sanitize_for_terminal(const gchar *text) {
    if (!text) {
        return g_strdup("");
    }

    gchar *safe = g_strdup(text);
    for (gchar *p = safe; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f || c == '\033') {
            *p = '?';
        }
    }
    return safe;
}

gint utf8_display_width(const gchar *text) {
    if (!text) {
        return 0;
    }

    gint width = 0;
    const gchar *p = text;
    while (*p) {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
            width++;
            p++;
            continue;
        }

        if (!g_unichar_iszerowidth(ch)) {
            width += g_unichar_iswide(ch) ? 2 : 1;
        }
        p = g_utf8_next_char(p);
    }

    return width;
}

gchar* utf8_prefix_by_width(const gchar *text, gint max_width) {
    if (!text || max_width <= 0) {
        return g_strdup("");
    }

    gint width = 0;
    const gchar *p = text;
    const gchar *end = text;
    while (*p) {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        gint char_width = 1;
        const gchar *next = p + 1;
        if (ch != (gunichar)-1 && ch != (gunichar)-2) {
            if (g_unichar_iszerowidth(ch)) {
                char_width = 0;
            } else {
                char_width = g_unichar_iswide(ch) ? 2 : 1;
            }
            next = g_utf8_next_char(p);
        }
        if (width + char_width > max_width) {
            break;
        }
        width += char_width;
        end = next;
        p = next;
    }

    return g_strndup(text, end - text);
}

gchar* utf8_suffix_by_width(const gchar *text, gint max_width) {
    if (!text || max_width <= 0) {
        return g_strdup("");
    }

    const gchar *end = text + strlen(text);
    const gchar *p = end;
    const gchar *start = end;
    gint width = 0;
    while (p > text) {
        const gchar *prev = g_utf8_prev_char(p);
        gunichar ch = g_utf8_get_char_validated(prev, -1);
        gint char_width = 1;
        if (ch != (gunichar)-1 && ch != (gunichar)-2) {
            if (g_unichar_iszerowidth(ch)) {
                char_width = 0;
            } else {
                char_width = g_unichar_iswide(ch) ? 2 : 1;
            }
        }
        if (width + char_width > max_width) {
            break;
        }
        width += char_width;
        start = prev;
        p = prev;
    }

    return g_strndup(start, end - start);
}

gchar* truncate_utf8_for_display(const gchar *text, gint max_width) {
    if (!text || max_width <= 0) {
        return g_strdup("");
    }

    gint width = utf8_display_width(text);
    if (width <= max_width) {
        return g_strdup(text);
    }

    if (max_width <= 3) {
        gchar *dots = g_malloc((gsize)max_width + 1);
        memset(dots, '.', (gsize)max_width);
        dots[max_width] = '\0';
        return dots;
    }

    gint target_width = max_width - 3;
    gint current_width = 0;
    const gchar *p = text;
    const gchar *end = text;
    while (*p) {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        gint char_width = 1;
        const gchar *next = p + 1;
        if (ch != (gunichar)-1 && ch != (gunichar)-2) {
            if (g_unichar_iszerowidth(ch)) {
                char_width = 0;
            } else {
                char_width = g_unichar_iswide(ch) ? 2 : 1;
            }
            next = g_utf8_next_char(p);
        }
        if (current_width + char_width > target_width) {
            break;
        }
        current_width += char_width;
        end = next;
        p = next;
    }

    gchar *prefix = g_strndup(text, end - text);
    gchar *result = g_strdup_printf("%s...", prefix);
    g_free(prefix);
    return result;
}

gchar* truncate_utf8_middle_keep_suffix(const gchar *text, gint max_width) {
    if (!text || max_width <= 0) {
        return g_strdup("");
    }

    gint width = utf8_display_width(text);
    if (width <= max_width) {
        return g_strdup(text);
    }

    if (max_width <= 3) {
        return truncate_utf8_for_display(text, max_width);
    }

    const gchar *ext = strrchr(text, '.');
    gint ext_width = 0;
    if (ext && ext != text && *(ext + 1) != '\0') {
        ext_width = utf8_display_width(ext);
    }

    gint suffix_width = max_width / 3;
    if (suffix_width < ext_width) {
        suffix_width = ext_width;
    }
    gint max_suffix = max_width - 4;
    if (max_suffix < 1) {
        max_suffix = 1;
    }
    if (suffix_width > max_suffix) {
        suffix_width = max_suffix;
    }
    gint prefix_width = max_width - 3 - suffix_width;
    if (prefix_width < 1) {
        prefix_width = 1;
        suffix_width = max_width - 4;
        if (suffix_width < ext_width && ext_width <= max_width - 3) {
            prefix_width = max_width - 3 - ext_width;
            if (prefix_width < 0) {
                prefix_width = 0;
            }
            suffix_width = max_width - 3 - prefix_width;
        }
    }

    if (prefix_width <= 0) {
        return truncate_utf8_for_display(text, max_width);
    }

    gchar *prefix = utf8_prefix_by_width(text, prefix_width);
    gchar *suffix = utf8_suffix_by_width(text, suffix_width);
    gchar *result = g_strdup_printf("%s...%s", prefix, suffix);
    g_free(prefix);
    g_free(suffix);
    return result;
}
