#!/usr/bin/env bash
set -euo pipefail

# Only literal tag characters may reach make or compiler shell recipes.
if [[ ! "${1:-}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf '%s\n' 'Invalid release version: expected vX.Y.Z.' >&2
    exit 1
fi
