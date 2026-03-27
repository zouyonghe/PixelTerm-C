#ifndef TERMINAL_PROTOCOL_RESOLVER_H
#define TERMINAL_PROTOCOL_RESOLVER_H

#include <glib.h>

typedef enum {
    TERMINAL_RESOLVED_PROTOCOL_TEXT = 0,
    TERMINAL_RESOLVED_PROTOCOL_SIXEL,
    TERMINAL_RESOLVED_PROTOCOL_KITTY,
    TERMINAL_RESOLVED_PROTOCOL_ITERM2
} TerminalResolvedProtocol;

typedef enum {
    TERMINAL_PROTOCOL_DECISION_SOURCE_OVERRIDE = 0,
    TERMINAL_PROTOCOL_DECISION_SOURCE_SIGNAL,
    TERMINAL_PROTOCOL_DECISION_SOURCE_PROBE,
    TERMINAL_PROTOCOL_DECISION_SOURCE_TEXT_FALLBACK
} TerminalProtocolDecisionSource;

typedef struct {
    gboolean has_override;
    TerminalResolvedProtocol override_protocol;
    gboolean has_weak_hint;
    TerminalResolvedProtocol weak_hint_protocol;
    gboolean has_signal;
    TerminalResolvedProtocol signal_protocol;
    gboolean has_probe;
    TerminalResolvedProtocol probe_protocol;
} TerminalProtocolResolverInput;

typedef struct {
    TerminalResolvedProtocol protocol;
    TerminalProtocolDecisionSource source;
    const char *reason;
} TerminalProtocolDecision;

TerminalProtocolDecision terminal_protocol_resolve(const TerminalProtocolResolverInput *input);

#endif
