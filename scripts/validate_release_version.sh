#!/usr/bin/env bash
set -euo pipefail

# Only literal tag characters may reach make or compiler shell recipes.
if [[ ! "${1:-}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+([-+][A-Za-z0-9._-]+)?$ ]]; then
    printf '%s\n' 'Invalid release version: expected vX.Y.Z with an optional tag suffix.' >&2
    exit 1
fi
