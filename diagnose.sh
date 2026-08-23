#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROBE="$SCRIPT_DIR/scripts/hid_registration_probe.sh"

if [ ! -x "$PROBE" ]; then
    printf 'ERROR: diagnostic probe is missing or not executable: %s\n' "$PROBE" >&2
    exit 1
fi

exec "$PROBE" --diagnose-only "$@"
