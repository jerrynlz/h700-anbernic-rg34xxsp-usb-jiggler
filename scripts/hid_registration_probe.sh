#!/bin/sh
set -u

PROBE_NAME="usbjiggler-probe-$$"
GADGET_ROOT="/sys/kernel/config/usb_gadget"
PROBE_PATH="$GADGET_ROOT/$PROBE_NAME"
MODE="diagnose"
MODULE=""
LOADED_BY_PROBE=0
FUNCTION_CREATED=0
GADGET_CREATED=0
DMESG_BEFORE=0
LOG="${HID_PROBE_LOG:-/tmp/hid-registration-probe.log}"

usage() {
    cat <<'EOF'
Usage:
  hid_registration_probe.sh --diagnose-only
  hid_registration_probe.sh --test-module /path/to/usb_f_hid.ko

The module test is intentionally unbound: it proves only that ConfigFS can
create functions/hid.usb0. It never binds a UDC and never accesses boot media.
EOF
}

log() {
    printf '%s\n' "$*" | tee -a "$LOG"
}

run_capture() {
    label=$1
    shift
    log "--- $label"
    "$@" >>"$LOG" 2>&1
    rc=$?
    log "rc=$rc"
    return "$rc"
}

cleanup() {
    rc=$?
    cleanup_rc=0
    trap - EXIT INT TERM HUP

    if [ "$FUNCTION_CREATED" -eq 1 ] && [ -d "$PROBE_PATH/functions/hid.usb0" ]; then
        rmdir "$PROBE_PATH/functions/hid.usb0" >>"$LOG" 2>&1 || {
            log "ERROR: failed to remove probe HID function; do not unload the module"
            cleanup_rc=1
        }
    fi
    if [ "$GADGET_CREATED" -eq 1 ] && [ -d "$PROBE_PATH" ]; then
        rmdir "$PROBE_PATH" >>"$LOG" 2>&1 || {
            log "ERROR: failed to remove probe gadget"
            cleanup_rc=1
        }
    fi
    if [ "$LOADED_BY_PROBE" -eq 1 ]; then
        if [ ! -d "$PROBE_PATH/functions/hid.usb0" ]; then
            rmmod usb_f_hid >>"$LOG" 2>&1 || {
                log "ERROR: failed to unload usb_f_hid"
                cleanup_rc=1
            }
        else
            log "ERROR: usb_f_hid remains loaded because its ConfigFS function could not be removed"
            cleanup_rc=1
        fi
    fi

    sleep 1
    log "--- kernel messages emitted during probe"
    dmesg | tail -n "+$((DMESG_BEFORE + 1))" >"$LOG.dmesg" 2>&1 || true
    cat "$LOG.dmesg" >>"$LOG"
    if grep -Eiq 'BUG:|Oops:|Unable to handle kernel|Call trace:|general protection fault|kernel panic' "$LOG.dmesg"; then
        log "ERROR: kernel fault detected during module lifecycle"
        cleanup_rc=1
    fi
    rm -f "$LOG.dmesg"

    if [ "$rc" -eq 0 ] && [ "$cleanup_rc" -ne 0 ]; then
        rc=$cleanup_rc
    fi
    if [ "$MODE" = "test" ] && [ "$rc" -eq 0 ]; then
        log "HID_FUNCTION_LIFECYCLE=PASS"
    fi
    log "--- probe finished rc=$rc"
    exit "$rc"
}

case "${1:-}" in
    --diagnose-only)
        MODE="diagnose"
        ;;
    --test-module)
        MODE="test"
        MODULE=${2:-}
        [ -n "$MODULE" ] || { usage >&2; exit 2; }
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

: >"$LOG" || exit 1
DMESG_BEFORE=$(dmesg | wc -l)
trap cleanup EXIT INT TERM HUP

log "=== HID REGISTRATION PROBE ==="
log "timestamp=$(date -Iseconds 2>/dev/null || date)"
run_capture "uname" uname -a || true
run_capture "kernel version" cat /proc/version || true
run_capture "relevant kernel config" sh -c "zcat /proc/config.gz 2>/dev/null | grep -E 'CONFIG_(MODVERSIONS|MODULE_UNLOAD|USB_GADGET|USB_LIBCOMPOSITE|USB_CONFIGFS|USB_CONFIGFS_F_HID)='" || true
run_capture "loaded modules" cat /proc/modules || true
run_capture "module metadata files" sh -c 'find /lib/modules/$(uname -r) -type f -print 2>/dev/null | sort' || true
run_capture "UDC state" sh -c 'for u in /sys/class/udc/*; do [ -e "$u" ] || continue; echo "UDC=$(basename "$u")"; cat "$u/state" 2>/dev/null; done' || true
run_capture "OTG role" cat /sys/devices/platform/soc/usbc0/otg_role || true
run_capture "ConfigFS ownership" sh -c 'find /sys/kernel/config/usb_gadget -maxdepth 4 -print 2>/dev/null | sort' || true
run_capture "HID character devices" sh -c 'ls -l /dev/hidg* 2>/dev/null || echo none' || true

[ "$MODE" = "test" ] || exit 0
[ "$(id -u)" = "0" ] || { log "ERROR: module test requires root"; exit 1; }
[ -f "$MODULE" ] || { log "ERROR: module not found: $MODULE"; exit 1; }
[ ! -e "$PROBE_PATH" ] || { log "ERROR: refusing to touch pre-existing probe path: $PROBE_PATH"; exit 1; }

provenance="$MODULE.provenance"
[ -s "$provenance" ] || { log "ERROR: missing build provenance: $provenance"; exit 1; }
grep -q '^expected_release=4.9.170$' "$provenance" || { log "ERROR: provenance does not target 4.9.170"; exit 1; }

expected_hash=$(sed -n 's/^module_sha256=//p' "$provenance")
actual_hash=$(sha256sum "$MODULE" | awk '{print $1}')
[ -n "$expected_hash" ] && [ "$expected_hash" = "$actual_hash" ] || { log "ERROR: module SHA-256 does not match provenance"; exit 1; }

section_size=$(readelf -SW "$MODULE" 2>/dev/null | awk '$2 == "__versions" { print $6 }')
[ -n "$section_size" ] && [ "$section_size" != "000000" ] || { log "ERROR: module has no populated __versions section; refusing unsafe load on CONFIG_MODVERSIONS kernel"; exit 1; }
version_count=$(modprobe --dump-modversions "$MODULE" 2>/dev/null | wc -l)

vermagic=$(modinfo -F vermagic "$MODULE" 2>/dev/null || true)
log "module_sha256=$actual_hash"
log "module_vermagic=$vermagic"
log "module_versions_size=$section_size"
log "module_versioned_imports=$version_count"
case "$vermagic" in
    "4.9.170 "*) ;;
    *) log "ERROR: module vermagic does not match 4.9.170"; exit 1 ;;
esac

if grep -q '^usb_f_hid ' /proc/modules; then
    log "ERROR: usb_f_hid is already loaded; refusing to adopt an unknown module"
    exit 1
fi

if ! run_capture "non-forced module load" insmod "$MODULE"; then
    sleep 1
    exit 1
fi
LOADED_BY_PROBE=1
sleep 1

mkdir "$PROBE_PATH" >>"$LOG" 2>&1 || { log "ERROR: failed to create private unbound probe gadget"; exit 1; }
GADGET_CREATED=1
mkdir "$PROBE_PATH/functions/hid.usb0" >>"$LOG" 2>&1 || { log "ERROR: HID function registration failed"; exit 1; }
FUNCTION_CREATED=1

log "HID_FUNCTION_REGISTERED=YES"
run_capture "unbound HID function attributes" ls -la "$PROBE_PATH/functions/hid.usb0" || true
log "No UDC binding was attempted."
exit 0
