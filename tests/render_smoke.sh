#!/bin/bash
set -euo pipefail

APP="${USBJIGGLER_HOST_APP:?set USBJIGGLER_HOST_APP to the host usbjiggler binary}"
FIRST="$(mktemp -d)"
SECOND="$(mktemp -d)"
trap 'rm -rf "$FIRST" "$SECOND"' EXIT

export SDL_VIDEODRIVER=dummy
if [[ -n "${USBJIGGLER_HOST_LD_LIBRARY_PATH:-}" ]]; then
    export LD_LIBRARY_PATH="$USBJIGGLER_HOST_LD_LIBRARY_PATH"
fi

"$APP" --render-screens "$FIRST"
"$APP" --render-screens "$SECOND"

python3 - "$FIRST" "$SECOND" <<'PY'
import hashlib
import pathlib
import struct
import sys

expected = ["preparing", "ready", "active", "settings", "help", "disconnected", "error", "cleanup", "exit"]
first = pathlib.Path(sys.argv[1])
second = pathlib.Path(sys.argv[2])

for name in expected:
    one = first / f"{name}.bmp"
    two = second / f"{name}.bmp"
    for image in (one, two):
        data = image.read_bytes()
        if data[:2] != b"BM":
            raise SystemExit(f"{image} is not a BMP")
        width, height = struct.unpack_from("<ii", data, 18)
        if (width, height) != (640, 480):
            raise SystemExit(f"{image} is {width}x{height}, expected 640x480")
    if hashlib.sha256(one.read_bytes()).digest() != hashlib.sha256(two.read_bytes()).digest():
        raise SystemExit(f"{name} capture is not deterministic")

print("RENDER_SMOKE_PASS=YES")
PY
