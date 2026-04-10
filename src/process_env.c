#include "process_env.h"

static GMutex pixelterm_env_mutex;
static GHashTable *pixelterm_env_overrides = NULL;
static const gchar * const PIXELTERM_ENV_UNSET = (const gchar *)&pixelterm_env_overrides;

static GHashTable *pixelterm_env_overrides_get_locked(void) {
    if (!pixelterm_env_overrides) {
        pixelterm_env_overrides = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    return pixelterm_env_overrides;
}

const gchar *pixelterm_getenv(const gchar *name) {
    if (!name || !*name) {
        return NULL;
    }

    g_mutex_lock(&pixelterm_env_mutex);
    if (pixelterm_env_overrides) {
        gpointer value = NULL;
        if (g_hash_table_lookup_extended(pixelterm_env_overrides, name, NULL, &value)) {
            const gchar *result = value == PIXELTERM_ENV_UNSET ? NULL : (const gchar *)value;
            g_mutex_unlock(&pixelterm_env_mutex);
            return result;
        }
    }
    g_mutex_unlock(&pixelterm_env_mutex);

    return g_getenv(name);
}

void pixelterm_env_set_for_test(const gchar *name, const gchar *value) {
    if (!name || !*name) {
        return;
    }

    g_mutex_lock(&pixelterm_env_mutex);
    g_hash_table_replace(pixelterm_env_overrides_get_locked(),
                         g_strdup(name),
                         (gpointer)g_intern_string(value ? value : ""));
    g_mutex_unlock(&pixelterm_env_mutex);
}

void pixelterm_env_unset_for_test(const gchar *name) {
    if (!name || !*name) {
        return;
    }

    g_mutex_lock(&pixelterm_env_mutex);
    g_hash_table_replace(pixelterm_env_overrides_get_locked(),
                         g_strdup(name),
                         (gpointer)PIXELTERM_ENV_UNSET);
    g_mutex_unlock(&pixelterm_env_mutex);
}

void pixelterm_env_reset_for_test(void) {
    g_mutex_lock(&pixelterm_env_mutex);
    if (pixelterm_env_overrides) {
        g_hash_table_remove_all(pixelterm_env_overrides);
    }
    g_mutex_unlock(&pixelterm_env_mutex);
}
