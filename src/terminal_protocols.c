#include "terminal_protocols.h"

#include <stdlib.h>
#include <string.h>

static const TerminalProtocolHint k_terminal_protocol_hints[] = {
    {
        "wezterm",
        {NULL},
        {"WezTerm", NULL},
        {"WEZTERM_EXECUTABLE", "WEZTERM_EXECUTABLE_DIR", "WEZTERM_PANE", "WEZTERM_UNIX_SOCKET", NULL},
        TRUE,
        FALSE,
        TRUE
    },
    {
        "kitty",
        {"xterm-kitty", "kitty", NULL},
        {"kitty", NULL},
        {"KITTY_WINDOW_ID", "KITTY_PID", "KITTY_INSTALLATION_DIR", NULL},
        TRUE,
        FALSE,
        FALSE
    },
    {
        "iterm2",
        {NULL},
        {"iTerm.app", "iTerm2", NULL},
        {"ITERM_SESSION_ID", "LC_TERMINAL", NULL},
        FALSE,
        TRUE,
        TRUE
    },
    {
        "ghostty",
        {"xterm-ghostty", "ghostty", NULL},
        {"ghostty", "Ghostty", NULL},
        {"GHOSTTY_RESOURCES_DIR", "GHOSTTY_BIN_DIR", NULL},
        TRUE,
        FALSE,
        FALSE
    },
    {
        "rio",
        {"rio", NULL},
        {"rio", "Rio", NULL},
        {NULL},
        FALSE,
        FALSE,
        TRUE
    },
    {
        "warp",
        {NULL},
        {"WarpTerminal", NULL},
        {NULL},
        TRUE,
        FALSE,
        FALSE
    },
    {
        "contour",
        {"contour", NULL},
        {NULL},
        {"TERMINAL_NAME", NULL},
        FALSE,
        FALSE,
        TRUE
    },
    {
        "eat",
        {"eat-truecolor", "eat-256color", "eat-16color", "eat-color", "eat-mono", NULL},
        {NULL},
        {"EAT_SHELL_INTEGRATION_DIR", NULL},
        FALSE,
        FALSE,
        TRUE
    },
    {
        "foot",
        {"foot", "foot-256color", "foot-direct", "foot-24bit", NULL},
        {NULL},
        {NULL},
        FALSE,
        FALSE,
        TRUE
    },
    {
        "mintty",
        {"mintty", NULL},
        {"mintty", NULL},
        {NULL},
        FALSE,
        TRUE,
        TRUE
    },
    {
        "mlterm",
        {"mlterm", NULL},
        {NULL},
        {"MLTERM", NULL},
        FALSE,
        TRUE,
        TRUE
    },
    {
        "yaft",
        {"yaft", "yaft-256color", NULL},
        {NULL},
        {NULL},
        FALSE,
        FALSE,
        TRUE
    },
    {
        "konsole",
        {NULL},
        {NULL},
        {"KONSOLE_VERSION", NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "alacritty",
        {"alacritty", "alacritty-direct", NULL},
        {"Alacritty", NULL},
        {NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "apple",
        {NULL},
        {"Apple_Terminal", NULL},
        {NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "ctx",
        {"ctx", NULL},
        {NULL},
        {"CTX_BACKEND", NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "fbterm",
        {"fbterm", NULL},
        {NULL},
        {NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "hurd",
        {"hurd", NULL},
        {NULL},
        {NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "linux",
        {"linux", NULL},
        {NULL},
        {NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "rxvt",
        {"rxvt-unicode", "rxvt-unicode-256color", NULL},
        {NULL},
        {NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "st",
        {"st-256color", NULL},
        {NULL},
        {NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "vt220",
        {"vt220", NULL},
        {NULL},
        {NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "vte",
        {NULL},
        {NULL},
        {"VTE_VERSION", NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "windows-console",
        {NULL},
        {NULL},
        {"ComSpec", "COMSPEC", NULL},
        FALSE,
        FALSE,
        FALSE
    },
    {
        "xterm",
        {"xterm", "xterm-256color", "xterm-direct", "xterm-direct2", "xterm-direct16", "xterm-direct256", NULL},
        {NULL},
        {"XTERM_VERSION", NULL},
        FALSE,
        FALSE,
        FALSE
    }
};

const TerminalProtocolHint* terminal_protocol_hints_get(gsize *count) {
    if (count) {
        *count = G_N_ELEMENTS(k_terminal_protocol_hints);
    }
    return k_terminal_protocol_hints;
}

static gboolean terminal_value_matches(const char *value, const char *const *candidates,
                                       gboolean case_insensitive) {
    if (!value || !candidates) {
        return FALSE;
    }

    for (gsize i = 0; candidates[i]; i++) {
        if (case_insensitive) {
            if (g_ascii_strcasecmp(value, candidates[i]) == 0) {
                return TRUE;
            }
        } else if (strcmp(value, candidates[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean terminal_env_matches_hint(const TerminalProtocolHint *hint) {
    if (!hint) {
        return FALSE;
    }

    const char *term = getenv("TERM");
    const char *term_program = getenv("TERM_PROGRAM");

    if (terminal_value_matches(term, hint->terms, FALSE)) {
        return TRUE;
    }
    if (terminal_value_matches(term_program, hint->term_programs, TRUE)) {
        return TRUE;
    }

    for (gsize i = 0; hint->env_vars[i]; i++) {
        const char *env_val = getenv(hint->env_vars[i]);
        if (env_val && *env_val) {
            return TRUE;
        }
    }

    return FALSE;
}

const TerminalProtocolHint* terminal_protocol_env_match(void) {
    gsize count = 0;
    const TerminalProtocolHint *hints = terminal_protocol_hints_get(&count);
    for (gsize i = 0; i < count; i++) {
        if (terminal_env_matches_hint(&hints[i])) {
            return &hints[i];
        }
    }
    return NULL;
}

gboolean terminal_env_supports_kitty(void) {
    gsize count = 0;
    const TerminalProtocolHint *hints = terminal_protocol_hints_get(&count);
    for (gsize i = 0; i < count; i++) {
        if (terminal_env_matches_hint(&hints[i]) && hints[i].supports_kitty) {
            return TRUE;
        }
    }
    return FALSE;
}

gboolean terminal_env_supports_iterm2(void) {
    gsize count = 0;
    const TerminalProtocolHint *hints = terminal_protocol_hints_get(&count);
    for (gsize i = 0; i < count; i++) {
        if (terminal_env_matches_hint(&hints[i]) && hints[i].supports_iterm2) {
            return TRUE;
        }
    }
    return FALSE;
}

gboolean terminal_env_supports_sixel(void) {
    gsize count = 0;
    const TerminalProtocolHint *hints = terminal_protocol_hints_get(&count);
    for (gsize i = 0; i < count; i++) {
        if (terminal_env_matches_hint(&hints[i]) && hints[i].supports_sixel) {
            return TRUE;
        }
    }
    return FALSE;
}
