#!/bin/sh
set -u

GADGET_ROOT=/sys/kernel/config/usb_gadget
GADGET_NAME=usbjiggler-test
GADGET="$GADGET_ROOT/$GADGET_NAME"
UDC_NAME=5100000.udc-controller
STATE=${USBJIGGLER_STATE:-/tmp/usbjiggler-gadget.state}
LOG=${USBJIGGLER_CLEANUP_LOG:-/tmp/usbjiggler-gadget-cleanup.log}
DMESG_BEFORE=0

log() {
    printf '%s\n' "$*" | tee -a "$LOG"
}

fail() {
    log "ERROR: $*"
    exit 1
}

: >"$LOG" || exit 1
DMESG_BEFORE=$(dmesg | wc -l)

log "=== USB JIGGLER GADGET CLEANUP ==="
log "timestamp=$(date -Iseconds 2>/dev/null || date)"
[ "$(id -u)" = 0 ] || fail "cleanup requires root"

module_loaded_by_setup=0
prior_owner=none
if [ -s "$STATE" ]; then
    state_gadget=$(sed -n 's/^gadget_name=//p' "$STATE")
    state_udc=$(sed -n 's/^udc_name=//p' "$STATE")
    module_loaded_by_setup=$(sed -n 's/^module_loaded_by_setup=//p' "$STATE")
    prior_owner=$(sed -n 's/^prior_owner=//p' "$STATE")
    [ "$state_gadget" = "$GADGET_NAME" ] || fail "state belongs to a different gadget: $state_gadget"
    [ "$state_udc" = "$UDC_NAME" ] || fail "state targets a different UDC: $state_udc"
fi

log "--- existing ConfigFS gadgets and UDC ownership"
for candidate in "$GADGET_ROOT"/*; do
    [ -d "$candidate" ] || continue
    candidate_udc=$(cat "$candidate/UDC" 2>/dev/null || true)
    log "gadget=$(basename "$candidate") udc=${candidate_udc:-unbound}"
done

if [ -d "$GADGET" ]; then
    bound_udc=$(cat "$GADGET/UDC" 2>/dev/null || true)
    if [ -n "$bound_udc" ]; then
        [ "$bound_udc" = "$UDC_NAME" ] || fail "owned gadget is bound to unexpected UDC: $bound_udc"
        printf '\n' >"$GADGET/UDC" || fail "failed to unbind owned gadget"
        log "UDC_UNBOUND=YES"
        waited=0
        while [ "$waited" -lt 10 ] && [ -e /dev/hidg0 ]; do
            sleep 1
            waited=$((waited + 1))
        done
        log "hidg_removal_wait_seconds=$waited"
    fi

    [ ! -L "$GADGET/configs/c.1/hid.usb0" ] || rm "$GADGET/configs/c.1/hid.usb0" || fail "failed to unlink owned HID function"
    [ ! -d "$GADGET/configs/c.1/strings/0x409" ] || rmdir "$GADGET/configs/c.1/strings/0x409" || fail "failed to remove owned config strings"
    [ ! -d "$GADGET/configs/c.1" ] || rmdir "$GADGET/configs/c.1" || fail "failed to remove owned config"
    [ ! -d "$GADGET/functions/hid.usb0" ] || rmdir "$GADGET/functions/hid.usb0" || fail "failed to remove owned HID function"
    [ ! -d "$GADGET/strings/0x409" ] || rmdir "$GADGET/strings/0x409" || fail "failed to remove owned gadget strings"
    rmdir "$GADGET" || fail "failed to remove owned gadget"
    log "OWNED_GADGET_REMOVED=YES"
else
    log "owned gadget is already absent"
fi

if [ "$module_loaded_by_setup" = 1 ] && grep -q '^usb_f_hid ' /proc/modules; then
    other_hid_functions=$(find "$GADGET_ROOT" -mindepth 3 -maxdepth 3 -type d -path '*/functions/hid.*' 2>/dev/null | wc -l)
    [ "$other_hid_functions" -eq 0 ] || fail "refusing to unload usb_f_hid while another HID function exists"
    rmmod usb_f_hid || fail "failed to unload usb_f_hid"
    log "MODULE_UNLOADED=YES"
fi

if [ "$prior_owner" != none ]; then
    [ -d "$prior_owner" ] || fail "prior owner no longer exists: $prior_owner"
    current_owner=""
    for candidate in "$GADGET_ROOT"/*; do
        [ -d "$candidate" ] || continue
        [ "$(cat "$candidate/UDC" 2>/dev/null || true)" = "$UDC_NAME" ] && current_owner=$candidate
    done
    [ -z "$current_owner" ] || fail "cannot restore prior owner; UDC is owned by $current_owner"
    printf '%s\n' "$UDC_NAME" >"$prior_owner/UDC" || fail "failed to restore prior UDC owner"
    log "prior_owner_restored=$prior_owner"
fi

rm -f "$STATE"
sleep 1
log "--- kernel messages emitted during cleanup"
dmesg | tail -n "+$((DMESG_BEFORE + 1))" >>"$LOG" 2>&1 || true
if grep -Eiq 'BUG:|Oops:|Unable to handle kernel|Call trace:|general protection fault|kernel panic' "$LOG"; then
    fail "kernel fault detected during cleanup"
fi
log "final_udc_state=$(cat "/sys/class/udc/$UDC_NAME/state" 2>/dev/null || echo unavailable)"
log "GADGET_CLEANUP_COMPLETE=YES"
exit 0
