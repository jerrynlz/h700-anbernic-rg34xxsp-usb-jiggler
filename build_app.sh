#!/bin/bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$APP_DIR"

COMMON_FLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -pthread)
RUNTIME_SOURCES=(src/app_runtime.c src/settings.c)
UI_SOURCES=(src/ui_model.c)
AUDITED_TARGET_SDL_SHA256="40d0616f6b97d447f47e1e77c116d84bc4d3d7a5e33a50e2f4f472f9b250f46d"

if [[ "${1:-}" == "--package" ]]; then
    DIST_DIR="$APP_DIR/dist"
    PACKAGE_DIR="$DIST_DIR/ports/usbjiggler"
    LAUNCHER_DIR="$DIST_DIR/ROMS/Ports"
    ARCHIVE="$APP_DIR/usbjiggler-portmaster.zip"
    REQUIRED=(
        usbjiggler usbjiggler.sh jiggler.gptk port.json gameinfo.xml screenshot.png readme.md
        scripts/gadget_setup.sh scripts/gadget_cleanup.sh
        kernel/usb_f_hid.ko kernel/usb_f_hid.ko.provenance
        docs/ui-flow.md docs/portmaster-validation.md
    )

    for path in "${REQUIRED[@]}"; do
        if [[ ! -f "$APP_DIR/$path" ]]; then
            printf 'ERROR: required package artifact is missing: %s\n' "$path" >&2
            exit 1
        fi
    done
    if [[ "$DIST_DIR" != "$APP_DIR/dist" || "$APP_DIR" == "/" ]]; then
        printf '%s\n' "ERROR: unsafe distribution path" >&2
        exit 1
    fi
    rm -rf -- "$DIST_DIR"
    mkdir -p "$PACKAGE_DIR/scripts" "$PACKAGE_DIR/kernel" "$PACKAGE_DIR/docs" "$LAUNCHER_DIR"
    install -m 0755 usbjiggler "$PACKAGE_DIR/usbjiggler"
    install -m 0755 usbjiggler.sh "$PACKAGE_DIR/usbjiggler.sh"
    install -m 0755 usbjiggler.sh "$LAUNCHER_DIR/usbjiggler.sh"
    install -m 0755 scripts/gadget_setup.sh "$PACKAGE_DIR/scripts/gadget_setup.sh"
    install -m 0755 scripts/gadget_cleanup.sh "$PACKAGE_DIR/scripts/gadget_cleanup.sh"
    install -m 0644 jiggler.gptk port.json gameinfo.xml screenshot.png readme.md "$PACKAGE_DIR/"
    install -m 0644 kernel/usb_f_hid.ko kernel/usb_f_hid.ko.provenance "$PACKAGE_DIR/kernel/"
    install -m 0644 docs/ui-flow.md docs/portmaster-validation.md "$PACKAGE_DIR/docs/"
    (
        cd "$DIST_DIR"
        find . -type f ! -name ARTIFACTS.sha256 -print0 | sort -z | xargs -0 sha256sum > "$PACKAGE_DIR/ARTIFACTS.sha256"
    )
    python3 - "$DIST_DIR" "$ARCHIVE" <<'PY'
import pathlib
import sys
import zipfile

source = pathlib.Path(sys.argv[1])
archive = pathlib.Path(sys.argv[2])
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
    for path in sorted(item for item in source.rglob("*") if item.is_file()):
        relative = path.relative_to(source).as_posix()
        info = zipfile.ZipInfo(relative, (2026, 1, 1, 0, 0, 0))
        mode = 0o755 if relative.endswith("usbjiggler") or relative.endswith(".sh") else 0o644
        info.external_attr = (0o100000 | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        output.writestr(info, path.read_bytes(), compresslevel=9)
PY
    (
        cd "$APP_DIR"
        sha256sum "$(basename "$ARCHIVE")" > "$(basename "$ARCHIVE").sha256"
    )
    printf 'PACKAGE_ARCHIVE=%s\n' "$ARCHIVE"
    printf 'PACKAGE_BUILD_COMPLETE=YES\n'
    exit 0
fi

if [[ "${1:-}" == "--test" ]]; then
    HOST_CC="${HOST_CC:-gcc}"
    TEST_BINARY="${TEST_BINARY:-/tmp/usbjiggler-runtime-tests}"
    "$HOST_CC" "${COMMON_FLAGS[@]}" "${RUNTIME_SOURCES[@]}" tests/runtime_tests.c -o "$TEST_BINARY"
    "$TEST_BINARY"
    exit 0
fi

if [[ "${1:-}" == "--ui-test" ]]; then
    HOST_CC="${HOST_CC:-gcc}"
    TEST_BINARY="${TEST_BINARY:-/tmp/usbjiggler-ui-tests}"
    UI_TEST_CFLAGS=()
    if [[ -n "${SDL2_CONFIG:-}" ]]; then
        read -r -a UI_TEST_CFLAGS <<<"$("$SDL2_CONFIG" --cflags)"
    elif [[ -n "${SDL_CFLAGS:-}" ]]; then
        read -r -a UI_TEST_CFLAGS <<<"$SDL_CFLAGS"
    else
        printf '%s\n' "ERROR: set SDL2_CONFIG or SDL_CFLAGS for UI tests" >&2
        exit 1
    fi
    "$HOST_CC" "${COMMON_FLAGS[@]}" "${UI_TEST_CFLAGS[@]}" \
        "${UI_SOURCES[@]}" src/settings.c tests/ui_tests.c -o "$TEST_BINARY"
    "$TEST_BINARY"
    exit 0
fi

if [[ -n "${CC:-}" ]]; then
    COMPILER="$CC"
elif command -v aarch64-buildroot-linux-gnu-gcc >/dev/null 2>&1; then
    COMPILER=aarch64-buildroot-linux-gnu-gcc
elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    COMPILER=aarch64-linux-gnu-gcc
else
    printf '%s\n' "ERROR: no AArch64 compiler found" >&2
    exit 1
fi

SDL_COMPILE_FLAGS=()
SDL_LINK_FLAGS=()
if [[ -n "${SDL2_CONFIG:-}" ]]; then
    read -r -a SDL_COMPILE_FLAGS <<<"$("$SDL2_CONFIG" --cflags)"
    read -r -a SDL_LINK_FLAGS <<<"$("$SDL2_CONFIG" --libs)"
elif [[ -n "${SDL_CFLAGS:-}" ]]; then
    read -r -a SDL_COMPILE_FLAGS <<<"$SDL_CFLAGS"
    if [[ -n "${TARGET_SDL:-}" ]]; then
        TARGET_SDL_ACTUAL_SHA256="$(sha256sum "$TARGET_SDL" | awk '{print $1}')"
        if [[ "$TARGET_SDL_ACTUAL_SHA256" != "$AUDITED_TARGET_SDL_SHA256" ]]; then
            printf 'ERROR: TARGET_SDL is not the audited SDL 2.28.5 library (%s)\n' \
                "$AUDITED_TARGET_SDL_SHA256" >&2
            exit 1
        fi
        SDL_LINK_FLAGS=("$TARGET_SDL" -Wl,--allow-shlib-undefined -lm)
    elif [[ -n "${SDL_LIBS:-}" ]]; then
        read -r -a SDL_LINK_FLAGS <<<"$SDL_LIBS"
    else
        SDL_LINK_FLAGS=(-lSDL2 -lm)
    fi
else
    printf '%s\n' "ERROR: set SDL2_CONFIG or SDL_CFLAGS for the target SDL2 headers" >&2
    exit 1
fi

OUTPUT="${OUTPUT:-$APP_DIR/usbjiggler}"
"$COMPILER" "${COMMON_FLAGS[@]}" "${SDL_COMPILE_FLAGS[@]}" \
    src/usbjiggler.c "${RUNTIME_SOURCES[@]}" \
    "${UI_SOURCES[@]}" \
    "${SDL_LINK_FLAGS[@]}" -o "$OUTPUT"

file "$OUTPUT"
printf 'APP_BUILD_COMPLETE=YES\n'
