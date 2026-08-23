#!/bin/sh
set -eu

EXPECTED_RELEASE="${EXPECTED_RELEASE:-4.9.170}"
KERNEL_BUILD_DIR="${KERNEL_BUILD_DIR:-}"
OUTPUT_DIR="${OUTPUT_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/kernel}"
ACK_EXACT_MUOS_TREE="${ACK_EXACT_MUOS_TREE:-0}"

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

[ -n "$KERNEL_BUILD_DIR" ] || fail "KERNEL_BUILD_DIR is required. Point it at the exact muOS kernel build tree matching the running RG34XXSP kernel; vanilla Linux 4.9.170 and KNULLI sources/configs are not ABI-compatible substitutes."
[ -d "$KERNEL_BUILD_DIR" ] || fail "kernel build tree not found: $KERNEL_BUILD_DIR"
[ "$ACK_EXACT_MUOS_TREE" = "1" ] || fail "set ACK_EXACT_MUOS_TREE=1 only after verifying this tree matches the target muOS source, patches, .config, Module.symvers, generated headers, and compiler assumptions"

for path in \
    Makefile \
    .config \
    Module.symvers \
    include/generated/utsrelease.h \
    include/generated/autoconf.h \
    drivers/usb/gadget/function/Makefile \
    drivers/usb/gadget/function/Kconfig \
    drivers/usb/gadget/function/f_hid.c
do
    [ -e "$KERNEL_BUILD_DIR/$path" ] || fail "exact build artifact is missing: $KERNEL_BUILD_DIR/$path"
done

[ -s "$KERNEL_BUILD_DIR/Module.symvers" ] || fail "Module.symvers is empty; modules_prepare alone is insufficient for CONFIG_MODVERSIONS=y"

command -v "${CROSS_COMPILE:-aarch64-linux-gnu-}gcc" >/dev/null 2>&1 || fail "cross-compiler not found: ${CROSS_COMPILE:-aarch64-linux-gnu-}gcc"

make -s -C "$KERNEL_BUILD_DIR" \
    ARCH=arm64 \
    CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" \
    LOCALVERSION= \
    prepare

actual_release=$(sed -n 's/^#define UTS_RELEASE "\([^"]*\)"/\1/p' "$KERNEL_BUILD_DIR/include/generated/utsrelease.h")
[ "$actual_release" = "$EXPECTED_RELEASE" ] || fail "UTS_RELEASE is '$actual_release', expected '$EXPECTED_RELEASE'"

grep -q '^CONFIG_MODVERSIONS=y$' "$KERNEL_BUILD_DIR/.config" || fail "exact target config must contain CONFIG_MODVERSIONS=y"
grep -q '^CONFIG_USB_GADGET=y$' "$KERNEL_BUILD_DIR/.config" || fail "exact target config must contain CONFIG_USB_GADGET=y"
grep -q '^CONFIG_USB_CONFIGFS=y$' "$KERNEL_BUILD_DIR/.config" || fail "exact target config must contain CONFIG_USB_CONFIGFS=y"

for symbol in module_layout usb_function_register usb_function_unregister config_group_init_type_name usb_ep_autoconfig
do
    grep -Eq "[[:space:]]${symbol}([[:space:]]|$)" "$KERNEL_BUILD_DIR/Module.symvers" || fail "Module.symvers lacks required symbol: $symbol"
done

if ! grep -q '^CONFIG_USB_CONFIGFS_F_HID=m$' "$KERNEL_BUILD_DIR/.config"; then
    fail "the verified tree is not prepared with CONFIG_USB_CONFIGFS_F_HID=m; use a disposable copy of the exact muOS tree, change only HID to tristate/module, then run olddefconfig and a full symbol-version-preserving build"
fi

printf 'Building usb_f_hid.ko from verified tree: %s\n' "$KERNEL_BUILD_DIR"
make -C "$KERNEL_BUILD_DIR" \
    ARCH=arm64 \
    CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" \
    LOCALVERSION= \
    M=drivers/usb/gadget/function \
    modules

module="$KERNEL_BUILD_DIR/drivers/usb/gadget/function/usb_f_hid.ko"
[ -s "$module" ] || fail "build completed without producing $module"

versions_size=$(readelf -SW "$module" | awk '$2 == "__versions" { print $6 }')
[ -n "$versions_size" ] || fail "built module has no __versions section"
[ "$versions_size" != "000000" ] || fail "built module has an empty __versions section and is unsafe for the target CONFIG_MODVERSIONS kernel"

module_vermagic=$(modinfo -F vermagic "$module" 2>/dev/null || true)
case "$module_vermagic" in
    "$EXPECTED_RELEASE "*) ;;
    *) fail "built module vermagic is '$module_vermagic'; expected release '$EXPECTED_RELEASE' with no local-version suffix" ;;
esac

mkdir -p "$OUTPUT_DIR"
cp "$module" "$OUTPUT_DIR/usb_f_hid.ko"
sha256sum "$OUTPUT_DIR/usb_f_hid.ko" > "$OUTPUT_DIR/usb_f_hid.ko.sha256"

{
    printf 'source_tree=%s\n' "$KERNEL_BUILD_DIR"
    printf 'source_commit='
    git -C "$KERNEL_BUILD_DIR" rev-parse HEAD 2>/dev/null || printf 'unavailable\n'
    printf 'expected_release=%s\n' "$EXPECTED_RELEASE"
    printf 'kernel_config_sha256='
    sha256sum "$KERNEL_BUILD_DIR/.config" | awk '{print $1}'
    printf 'module_symvers_sha256='
    sha256sum "$KERNEL_BUILD_DIR/Module.symvers" | awk '{print $1}'
    printf 'compiler='
    "${CROSS_COMPILE:-aarch64-linux-gnu-}gcc" --version | sed -n '1p'
    printf 'module_sha256='
    sha256sum "$OUTPUT_DIR/usb_f_hid.ko" | awk '{print $1}'
    printf 'module_vermagic='
    printf '%s\n' "$module_vermagic"
    printf 'generated_compile_h='
    tr '\n' ' ' < "$KERNEL_BUILD_DIR/include/generated/compile.h" 2>/dev/null || true
    printf '\n'
} > "$OUTPUT_DIR/usb_f_hid.ko.provenance"

printf 'Built verified candidate: %s\n' "$OUTPUT_DIR/usb_f_hid.ko"
printf 'Review %s before deployment. Never use insmod -f.\n' "$OUTPUT_DIR/usb_f_hid.ko.provenance"
