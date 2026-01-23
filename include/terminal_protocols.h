#ifndef TERMINAL_PROTOCOLS_H
#define TERMINAL_PROTOCOLS_H

#include <glib.h>

typedef struct {
    const char *name;
    const char *terms[8];
    const char *term_programs[8];
    const char *env_vars[8];
    gboolean supports_kitty;
    gboolean supports_iterm2;
    gboolean supports_sixel;
} TerminalProtocolHint;

const TerminalProtocolHint* terminal_protocol_hints_get(gsize *count);
const TerminalProtocolHint* terminal_protocol_env_match(void);
gboolean terminal_env_supports_kitty(void);
gboolean terminal_env_supports_iterm2(void);
gboolean terminal_env_supports_sixel(void);

#endif
