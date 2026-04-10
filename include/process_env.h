#ifndef PROCESS_ENV_H
#define PROCESS_ENV_H

#include <glib.h>

const gchar *pixelterm_getenv(const gchar *name);

void pixelterm_env_set_for_test(const gchar *name, const gchar *value);
void pixelterm_env_unset_for_test(const gchar *name);
void pixelterm_env_reset_for_test(void);

#endif
