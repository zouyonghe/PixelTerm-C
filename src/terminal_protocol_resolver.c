#include "terminal_protocol_resolver.h"
#include "terminal_protocols.h"

static TerminalProtocolDecision terminal_protocol_make_decision(TerminalResolvedProtocol protocol,
                                                                TerminalProtocolDecisionSource source,
                                                                const char *reason) {
    TerminalProtocolDecision decision = {
        .protocol = protocol,
        .source = source,
        .reason = reason,
    };
    return decision;
}

TerminalProtocolDecision terminal_protocol_resolve(const TerminalProtocolResolverInput *input) {
    if (input && input->has_override) {
        return terminal_protocol_make_decision(input->override_protocol,
                                               TERMINAL_PROTOCOL_DECISION_SOURCE_OVERRIDE,
                                               "override");
    }

    if (input && input->has_signal) {
        return terminal_protocol_make_decision(input->signal_protocol,
                                               TERMINAL_PROTOCOL_DECISION_SOURCE_SIGNAL,
                                               "signal");
    }

    if (terminal_session_is_direct_ssh()) {
        return terminal_protocol_make_decision(TERMINAL_RESOLVED_PROTOCOL_TEXT,
                                               TERMINAL_PROTOCOL_DECISION_SOURCE_TEXT_FALLBACK,
                                               "ssh-no-affirmative-signal");
    }

    if (input && input->has_probe) {
        return terminal_protocol_make_decision(input->probe_protocol,
                                               TERMINAL_PROTOCOL_DECISION_SOURCE_PROBE,
                                               "probe");
    }

    return terminal_protocol_make_decision(TERMINAL_RESOLVED_PROTOCOL_TEXT,
                                           TERMINAL_PROTOCOL_DECISION_SOURCE_TEXT_FALLBACK,
                                           "text-fallback");
}
