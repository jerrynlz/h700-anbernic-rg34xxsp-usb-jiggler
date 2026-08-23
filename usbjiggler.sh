#!/bin/bash

XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
controlfolder=""
GAMEDIR=""
app_pid=""
gptk_pid=""

for candidate in \
    "${PORTMASTER_CONTROL_DIR:-/__portmaster_control_not_set__}" \
    "/opt/system/Tools/PortMaster" \
    "/opt/tools/PortMaster" \
    "$XDG_DATA_HOME/PortMaster" \
    "/mnt/mmc/MUOS/PortMaster" \
    "/roms/ports/PortMaster"; do
    if [[ -r "$candidate/control.txt" ]]; then
        controlfolder="$candidate"
        break
    fi
done

if [[ -z "$controlfolder" ]]; then
    printf '%s\n' "ERROR: PortMaster control.txt was not found" >&2
    exit 1
fi

source "$controlfolder/control.txt"

for candidate in \
    "${USBJIGGLER_GAMEDIR:-/__usbjiggler_gamedir_not_set__}" \
    "/${directory:-}/ports/usbjiggler" \
    "/mnt/mmc/ports/usbjiggler" \
    "/mmc/ports/usbjiggler" \
    "/ports/usbjiggler"; do
    if [[ -x "$candidate/usbjiggler" && -r "$candidate/jiggler.gptk" ]]; then
        GAMEDIR="$candidate"
        break
    fi
done

if [[ -z "$GAMEDIR" ]]; then
    printf '%s\n' "ERROR: the usbjiggler application directory was not found" >&2
    exit 1
fi

stop_process() {
    local pid="${1:-}"
    local attempts=0

    [[ -n "$pid" ]] || return 0
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        while kill -0 "$pid" 2>/dev/null && (( attempts < 180 )); do
            sleep 0.25
            attempts=$((attempts + 1))
        done
        if kill -0 "$pid" 2>/dev/null; then
            kill -KILL "$pid" 2>/dev/null || true
        fi
    fi
    wait "$pid" 2>/dev/null || true
}

finish_port() {
    local status=$?
    local finish_status=0

    trap - EXIT INT TERM HUP
    stop_process "$app_pid"
    app_pid=""
    stop_process "$gptk_pid"
    gptk_pid=""
    if declare -F pm_finish >/dev/null; then
        pm_finish || finish_status=$?
    fi
    if (( status == 0 && finish_status != 0 )); then
        status=$finish_status
    fi
    exit "$status"
}

trap finish_port EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

if declare -F get_controls >/dev/null; then
    get_controls
fi

cd "$GAMEDIR" || exit 1
: > "$GAMEDIR/log.txt"

if declare -F pm_platform_helper >/dev/null; then
    pm_platform_helper "$GAMEDIR/usbjiggler"
fi

read -r -a gptokeyb_command <<< "${GPTOKEYB:?ERROR: GPTOKEYB is not configured}"
"${gptokeyb_command[@]}" "usbjiggler" -c "$GAMEDIR/jiggler.gptk" &
gptk_pid=$!

"$GAMEDIR/usbjiggler" >> "$GAMEDIR/log.txt" 2>&1 &
app_pid=$!
wait "$app_pid"
app_status=$?
app_pid=""
exit "$app_status"
