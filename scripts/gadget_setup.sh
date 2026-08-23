#!/bin/sh
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
GADGET_ROOT=/sys/kernel/config/usb_gadget
GADGET_NAME=usbjiggler-test
GADGET="$GADGET_ROOT/$GADGET_NAME"
UDC_NAME=5100000.udc-controller
MODULE=${HID_MODULE:-$BASE_DIR/kernel/usb_f_hid.ko}
PROVENANCE="$MODULE.provenance"
EXPECTED_MODULE_SHA256=5a3cfc769fd592c8f8ad7aa4238df6d1c1c70e72de5ad923eb334280a35e07a9
STATE=${USBJIGGLER_STATE:-/tmp/usbjiggler-gadget.state}
LOG=${USBJIGGLER_SETUP_LOG:-/tmp/usbjiggler-gadget-setup.log}
DMESG_BEFORE=0
MODULE_LOADED_BY_SETUP=0
STATE_CREATED_BY_SETUP=0
GADGET_CREATED_BY_SETUP=0
SETUP_COMPLETE=0

log() {
    printf '%s\n' "$*" | tee -a "$LOG"
}

capture_dmesg() {
    log "--- kernel messages emitted during setup"
    dmesg | tail -n "+$((DMESG_BEFORE + 1))" >"$LOG.dmesg" 2>&1 || true
    cat "$LOG.dmesg" >>"$LOG"
    if grep -Eiq 'hidg_bind FAILED|failed to start.*-19|BUG:|Oops:|Unable to handle kernel|Call trace:|general protection fault|kernel panic' "$LOG.dmesg"; then
        rm -f "$LOG.dmesg"
        return 1
    fi
    rm -f "$LOG.dmesg"
    return 0
}

remove_owned_gadget() {
    if [ -d "$GADGET" ]; then
        if [ -f "$GADGET/UDC" ] && [ -n "$(cat "$GADGET/UDC" 2>/dev/null)" ]; then
            printf '\n' >"$GADGET/UDC" 2>>"$LOG" || true
            sleep 1
        fi
        [ ! -L "$GADGET/configs/c.1/hid.usb0" ] || rm "$GADGET/configs/c.1/hid.usb0" >>"$LOG" 2>&1 || true
        [ ! -d "$GADGET/configs/c.1/strings/0x409" ] || rmdir "$GADGET/configs/c.1/strings/0x409" >>"$LOG" 2>&1 || true
        [ ! -d "$GADGET/configs/c.1" ] || rmdir "$GADGET/configs/c.1" >>"$LOG" 2>&1 || true
        [ ! -d "$GADGET/functions/hid.usb0" ] || rmdir "$GADGET/functions/hid.usb0" >>"$LOG" 2>&1 || true
        [ ! -d "$GADGET/strings/0x409" ] || rmdir "$GADGET/strings/0x409" >>"$LOG" 2>&1 || true
        rmdir "$GADGET" >>"$LOG" 2>&1 || true
    fi
}

finish() {
    rc=$?
    trap - EXIT INT TERM HUP
    if [ "$SETUP_COMPLETE" -ne 1 ]; then
        log "ERROR: setup failed; rolling back only resources created by this invocation"
        if [ "$GADGET_CREATED_BY_SETUP" -eq 1 ]; then
            remove_owned_gadget
        fi
        if [ "$MODULE_LOADED_BY_SETUP" -eq 1 ]; then
            rmmod usb_f_hid >>"$LOG" 2>&1 || log "ERROR: failed to unload usb_f_hid during rollback"
        fi
        if [ "$STATE_CREATED_BY_SETUP" -eq 1 ]; then
            rm -f "$STATE"
        fi
    fi
    sleep 1
    if ! capture_dmesg; then
        log "ERROR: kernel fault or HID bind failure detected during setup"
        rc=1
    fi
    log "--- setup finished rc=$rc"
    exit "$rc"
}

: >"$LOG" || exit 1
DMESG_BEFORE=$(dmesg | wc -l)
trap finish EXIT INT TERM HUP

log "=== USB JIGGLER GADGET SETUP ==="
log "timestamp=$(date -Iseconds 2>/dev/null || date)"
log "gadget=$GADGET"
log "target_udc=$UDC_NAME"

[ "$(id -u)" = 0 ] || { log "ERROR: setup requires root"; exit 1; }
[ -d "$GADGET_ROOT" ] || { log "ERROR: ConfigFS gadget root is unavailable"; exit 1; }
[ -d "/sys/class/udc/$UDC_NAME" ] || { log "ERROR: target UDC is unavailable: $UDC_NAME"; exit 1; }
[ ! -e "$GADGET" ] || { log "ERROR: owned gadget already exists; run gadget_cleanup.sh first"; exit 1; }
[ ! -e "$STATE" ] || { log "ERROR: stale state exists: $STATE; run gadget_cleanup.sh first"; exit 1; }

log "--- existing ConfigFS gadgets and UDC ownership"
owner=""
for candidate in "$GADGET_ROOT"/*; do
    [ -d "$candidate" ] || continue
    candidate_udc=$(cat "$candidate/UDC" 2>/dev/null || true)
    log "gadget=$(basename "$candidate") udc=${candidate_udc:-unbound}"
    if [ "$candidate_udc" = "$UDC_NAME" ]; then
        owner=$candidate
    fi
done
log "target_udc_state=$(cat "/sys/class/udc/$UDC_NAME/state" 2>/dev/null || echo unavailable)"
[ -z "$owner" ] || { log "ERROR: refusing to displace unrelated UDC owner: $owner"; exit 1; }

[ -f "$MODULE" ] || { log "ERROR: validated module is missing: $MODULE"; exit 1; }
[ -s "$PROVENANCE" ] || { log "ERROR: module provenance is missing: $PROVENANCE"; exit 1; }
actual_hash=$(sha256sum "$MODULE" | awk '{print $1}')
provenance_hash=$(sed -n 's/^module_sha256=//p' "$PROVENANCE")
[ "$actual_hash" = "$EXPECTED_MODULE_SHA256" ] || { log "ERROR: module hash is not the validated artifact: $actual_hash"; exit 1; }
[ "$provenance_hash" = "$EXPECTED_MODULE_SHA256" ] || { log "ERROR: provenance hash does not match the validated artifact"; exit 1; }
grep -q '^hid_endpoint_mode=interrupt-IN-only$' "$PROVENANCE" || { log "ERROR: provenance does not identify the validated IN-only endpoint mode"; exit 1; }
vermagic=$(modinfo -F vermagic "$MODULE" 2>/dev/null || true)
case "$vermagic" in
    "4.9.170 "*) ;;
    *) log "ERROR: exact module vermagic mismatch: $vermagic"; exit 1 ;;
esac
versions_size=$(readelf -SW "$MODULE" 2>/dev/null | awk '$2 == "__versions" { print $6 }')
version_count=$(modprobe --dump-modversions "$MODULE" 2>/dev/null | wc -l)
[ -n "$versions_size" ] && [ "$versions_size" != 000000 ] || { log "ERROR: module has no populated __versions section"; exit 1; }
[ "$version_count" -eq 55 ] || { log "ERROR: expected 55 versioned imports, found $version_count"; exit 1; }
log "module_sha256=$actual_hash"
log "module_vermagic=$vermagic"
log "module_versions_size=$versions_size"
log "module_versioned_imports=$version_count"

if grep -q '^usb_f_hid ' /proc/modules; then
    log "ERROR: usb_f_hid is already loaded; refusing to adopt module state not created by this setup"
    exit 1
fi
if ! insmod "$MODULE" >>"$LOG" 2>&1; then
    log "ERROR: normal module load failed"
    exit 1
fi
MODULE_LOADED_BY_SETUP=1
sleep 1

umask 077
STATE_CREATED_BY_SETUP=1
{
    printf 'gadget_name=%s\n' "$GADGET_NAME"
    printf 'udc_name=%s\n' "$UDC_NAME"
    printf 'module_loaded_by_setup=1\n'
    printf 'prior_owner=none\n'
} >"$STATE" || { log "ERROR: failed to record setup state"; exit 1; }

if mkdir "$GADGET"; then
    GADGET_CREATED_BY_SETUP=1
else
    log "ERROR: failed to create owned gadget"
    exit 1
fi
printf '0x1d6b\n' >"$GADGET/idVendor"
printf '0x0104\n' >"$GADGET/idProduct"
printf '0x0100\n' >"$GADGET/bcdDevice"
printf '0x0200\n' >"$GADGET/bcdUSB"

mkdir "$GADGET/strings/0x409" || exit 1
printf 'RG34XXSP-HID-TEST\n' >"$GADGET/strings/0x409/serialnumber"
printf 'Anbernic RG34XXSP\n' >"$GADGET/strings/0x409/manufacturer"
printf 'USB Jiggler Test Mouse\n' >"$GADGET/strings/0x409/product"

mkdir "$GADGET/configs/c.1" || exit 1
mkdir "$GADGET/configs/c.1/strings/0x409" || exit 1
printf 'HID Mouse Test\n' >"$GADGET/configs/c.1/strings/0x409/configuration"
printf '120\n' >"$GADGET/configs/c.1/MaxPower"

mkdir "$GADGET/functions/hid.usb0" || exit 1
printf '2\n' >"$GADGET/functions/hid.usb0/protocol"
printf '1\n' >"$GADGET/functions/hid.usb0/subclass"
printf '4\n' >"$GADGET/functions/hid.usb0/report_length"
printf '\005\001\011\002\241\001\011\001\241\000\005\011\031\001\051\003\025\000\045\001\225\003\165\001\201\002\225\001\165\005\201\003\005\001\011\060\011\061\011\070\025\201\045\177\165\010\225\003\201\006\300\300' >"$GADGET/functions/hid.usb0/report_desc"

protocol=$(cat "$GADGET/functions/hid.usb0/protocol")
subclass=$(cat "$GADGET/functions/hid.usb0/subclass")
report_length=$(cat "$GADGET/functions/hid.usb0/report_length")
descriptor_size=$(wc -c <"$GADGET/functions/hid.usb0/report_desc")
[ "$protocol" = 2 ] || { log "ERROR: protocol verification failed: $protocol"; exit 1; }
[ "$subclass" = 1 ] || { log "ERROR: subclass verification failed: $subclass"; exit 1; }
[ "$report_length" = 4 ] || { log "ERROR: report length verification failed: $report_length"; exit 1; }
[ "$descriptor_size" -eq 52 ] || { log "ERROR: report descriptor size is $descriptor_size, expected 52"; exit 1; }
log "protocol=$protocol subclass=$subclass report_length=$report_length report_descriptor_bytes=$descriptor_size"

ln -s "$GADGET/functions/hid.usb0" "$GADGET/configs/c.1/hid.usb0" || exit 1
log "function_link=$GADGET/configs/c.1/hid.usb0"

if ! { printf '%s\n' "$UDC_NAME" >"$GADGET/UDC"; } >>"$LOG" 2>&1; then
    log "ERROR: UDC bind failed"
    exit 1
fi
log "UDC_BIND_REQUESTED=YES"

waited=0
while [ "$waited" -lt 10 ] && [ ! -e /dev/hidg0 ]; do
    sleep 1
    waited=$((waited + 1))
done
[ -e /dev/hidg0 ] || { log "ERROR: /dev/hidg0 did not appear within 10 seconds"; exit 1; }
log "hidg_wait_seconds=$waited"
ls -l /dev/hidg0 >>"$LOG" 2>&1
log "udc_state=$(cat "/sys/class/udc/$UDC_NAME/state" 2>/dev/null || echo unavailable)"

sleep 1
if ! capture_dmesg; then
    log "ERROR: kernel fault or HID bind failure detected during setup"
    exit 1
fi
SETUP_COMPLETE=1
trap - EXIT INT TERM HUP
log "GADGET_SETUP_READY=YES"
log "--- setup finished rc=0"
exit 0
