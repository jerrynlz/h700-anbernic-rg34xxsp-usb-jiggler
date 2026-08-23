# HID path audit

## Scope

This audit covers ABI-correct HID registration, the H700 single-interrupt-endpoint adaptation, reversible `/dev/hidg0` binding, exact report delivery, user-confirmed single-report cursor movement, physical reconnect repeatability, and final cleanup. No boot partition was modified or flashed.

## Confirmed local artifacts

| Artifact | SHA-256 | Notes |
|---|---|---|
| `src/usbjiggler.c` | `34e828c559d39f1852612935cfcedd0207b83d610fa6967f5f02b26ed4909110` | Local source supplied on 2026-08-20. It includes ConfigFS gadget management and a kernel installer; neither was modified in this goal. |
| `usbjiggler` | `6963a32963cf9bc6e95c8c15a0b6017460cc536690c0c481c60f5f672587c611` | AArch64 ELF binary. The same hash was observed on the device. |
| original `build_module.sh` | `98678d5ee1c97343181848f0626a548d7476fbd0a65e58a64210e2b297c2a4f5` | Downloaded vanilla Linux 4.9.170 and substituted a KNULLI config. That does not reproduce the muOS vendor ABI. |
| original `diagnose.sh` | `bdde14b1731fdb5ddd36d459396bc0f0055517edc8a01a84c0760181a31c39b6` | Inspected the local PC build tree rather than the target device. |
| final `build_module.sh` | `8bed9a0df3af08ea21e96db2b8ff107ea5cdd0476cccd53d5920f5d7c7ccda44` | Exact-tree-gated builder; records source, config, symbol table, compiler, module hash, and vermagic provenance. |
| final `diagnose.sh` | `ff923a2773031b05c3aafb90eabc47754e18cacc7168e221558b3c9ad208c773` | Finite wrapper around the target-side diagnostic probe. |
| final `scripts/hid_registration_probe.sh` | `718066efdfe1fd54032a1308fabbdb549859c674c492d62c7b57308671fe3f94` | Read-only by default; explicit module mode validates provenance, exact vermagic, symbol versions, unbound registration, cleanup, unload, and kernel logs. |
| `scripts/gadget_setup.sh` | `9033f293e83be55ee4a0065802e6589e703e782a9e6d64709fd29a49389bf98f` | Provenance-gated, ownership-aware, bounded setup for only `usbjiggler-test`; tracks invocation-created resources and never rolls back pre-existing gadget or state files. |
| `scripts/gadget_cleanup.sh` | `2b4c6b28dc87d840f273a97a779313ca6a5e19a077a19870f608fd8176cb4b2c` | Exact-path unbind/removal and owned-module unload; refuses unexpected gadget or UDC state. |
| `scripts/hid_report_test.c` | `1873bf72d5fd8a6485d87b850ac2aa9f1454d333d7416d7363e0f63c7d33d4de` | Binary-safe writer for exactly one `+2 X` report, one-second pause, then one `-2 X` report with finite poll, interruption, short-write, and error checks. |

## Confirmed target environment

- Device: Anbernic RG34XXSP running MustardOS 2601.0 JACARANDA / muOS 2 NeXT.
- Kernel: `Linux muos-7b2313 4.9.170 #2 SMP PREEMPT Wed Dec 24 23:00:24 CST 2025 aarch64`.
- Compiler recorded by `/proc/version`: Linaro GCC 5.3-2016.05.
- `CONFIG_MODVERSIONS=y`, `CONFIG_USB_GADGET=y`, `CONFIG_USB_LIBCOMPOSITE=y`, and `CONFIG_USB_CONFIGFS=y` are confirmed.
- `CONFIG_USB_CONFIGFS_F_HID` is absent from the stock config.
- ConfigFS is mounted read-write at `/sys/kernel/config`; UDC `5100000.udc-controller` exists and OTG role is `usb_device`.
- At the initial diagnostic point, the UDC state was `not attached`, the ConfigFS gadget root was empty, and no `/dev/hidg*` device existed.
- `/lib/modules/4.9.170` contains stock modules and indexes, but no corresponding kernel source, generated headers, or `Module.symvers` file.

## Candidate module evidence

The device contained two byte-identical module files:

```text
88fe05366050f7e2855054b324cbc0f57c600d6994d60e174dbac8bfc1bc2b03  kernel/usb_f_hid.ko
88fe05366050f7e2855054b324cbc0f57c600d6994d60e174dbac8bfc1bc2b03  kernel/usb_f_hid_patched.ko
```

Confirmed metadata:

- AArch64 relocatable ELF with debug information.
- Vermagic: `4.9.170 SMP preempt mod_unload modversions aarch64`.
- The ELF `__versions` section has size zero, proving it was built without usable target `Module.symvers` data despite `CONFIG_MODVERSIONS=y`.
- A normal `insmod` without `-f` returned success. Vermagic acceptance therefore did **not** establish ABI compatibility.
- Creating an unbound `functions/hid.usb0` initially returned success.
- Removing that function caused a kernel oops in `configfs_rmdir`; the module remained in use and required a reboot to restore clean state.

Conclusion: the supplied module is **disproven as ABI-safe**. Its apparent load and registration success cannot be used as proof. The failure demonstrates that matching release text and vermagic are insufficient for this vendor kernel; exact source patches, configuration, generated headers, symbol versions, and compatible compiler assumptions are required.

The unsafe artifact was preserved on the device as `backup/dev-audit-20260820/usb_f_hid.ko.unsafe-empty-versions` and was not loaded again.

## Registration-compatible module evidence

KNULLI's H700 build configuration identified the vendor source lineage as:

```text
https://github.com/orangepi-xunlong/linux-orangepi.git
branch: orange-pi-4.9-sun50iw9
commit: 0cd0547ea405b84b5b60fbc92978ac1bc2b68055
```

The running `/proc/config.gz` was captured read-only and has decompressed SHA-256 `565948300ae2d0f68a8ec85a18be033cde06ad4c46a988bebf708b9ce35cb119`. Applying it to that vendor tree demonstrated that the public source is not the exact muOS tree: `olddefconfig` dropped several muOS-only options. The strict `build_module.sh` exact-tree acknowledgement was therefore not used.

The running kernel does expose its export table boundaries through `/proc/kallsyms`. A temporary read-only extractor translated the mapped kernel image to its documented `Kernel code` physical range and read the export and CRC tables through `/dev/mem`. It did not read `/dev/mmcblk0p4` or alter persistent kernel/boot state. Results:

- 8,717 running export CRC records.
- Extracted `Module.symvers` SHA-256: `cc1f3b38d3d6f5ecc188a3fa87e5b7bcf6a04c0de9743407ddecee8b860bd30a`.
- All 55 symbols imported by `usb_f_hid.ko` were covered.
- A vendor-generated candidate disagreed only for `device_create`, `device_destroy`, and `dev_err`; normal `insmod` rejected it and logged each symbol-version disagreement.

The HID function was then rebuilt from the H700 vendor source with the exact running config as its base, the exact running export CRC table, and Linaro GCC 5.3-2016.02. Registration-compatible artifact:

```text
db93227a5d9ef20713576ec89bda124d1ce972b15f8d70d9cedc9faaf519bf4c  kernel/usb_f_hid.ko
vermagic=4.9.170 SMP preempt mod_unload modversions aarch64
__versions size=0x0dc0
running export coverage=55/55
```

The finite unbound lifecycle probe then proved:

1. Normal `insmod` succeeded; no force flag was used.
2. Private gadget `usbjiggler-probe-<pid>` was created without writing a UDC name.
3. `functions/hid.usb0` was created and exposed `dev`, `protocol`, `report_desc`, `report_length`, and `subclass` attributes.
4. The HID function and private gadget were removed successfully.
5. `usb_f_hid` unloaded successfully.
6. No kernel messages were emitted during the successful lifecycle.
7. Final state was clean: no loaded `usb_f_hid`, no probe gadget, no `/dev/hidg*`, and UDC remained unbound.

Probe result markers:

```text
module_sha256=db93227a5d9ef20713576ec89bda124d1ce972b15f8d70d9cedc9faaf519bf4c
module_vermagic=4.9.170 SMP preempt mod_unload modversions aarch64
module_versions_size=000dc0
module_versioned_imports=55
HID_FUNCTION_REGISTERED=YES
HID_FUNCTION_LIFECYCLE=PASS
probe finished rc=0
```

This establishes ABI-correct HID function registration for the running muOS kernel without boot modification. It does not yet prove UDC binding, `/dev/hidg0`, report delivery, or host cursor movement.

## IN-only bind-compatible module

The first bounded UDC bind attempt used the registration-compatible artifact above. Descriptor creation and function linking succeeded, but writing `5100000.udc-controller` to the gadget's `UDC` attribute failed. The setup tool rolled back its private gadget and unloaded the module cleanly. The kernel recorded:

```text
configfs-gadget gadget: hidg_bind FAILED
configfs-gadget 5100000.udc-controller: failed to start usbjiggler-test: -19
```

This is confirmed as an endpoint-allocation failure rather than a cable diagnosis:

- Vendor `f_hid.c` advertises and allocates both interrupt IN and interrupt OUT endpoints.
- The H700 sunxi UDC exposes only one bidirectional interrupt endpoint, `ep4-int`.
- The UDC source explicitly states that `ep4-int` cannot transmit and receive simultaneously.
- Interrupt IN claims `ep4-int`; the subsequent interrupt OUT autoconfiguration has no eligible endpoint and returns `-ENODEV` (`-19`).

`patches/f_hid-in-only.patch` removes the unused interrupt OUT descriptor and allocation, advertises one endpoint, preserves the userspace IN/write path, and makes unsupported reads return `-EOPNOTSUPP`. Patch SHA-256:

```text
40b1596f519b9bead8fa0bd0fd772cbdbc5e97365ea3365f5364949497895715  patches/f_hid-in-only.patch
```

The patched module was rebuilt with the same exact running export table and ABI inputs:

```text
5a3cfc769fd592c8f8ad7aa4238df6d1c1c70e72de5ad923eb334280a35e07a9  kernel/usb_f_hid.ko
vermagic=4.9.170 SMP preempt mod_unload modversions aarch64
__versions size=0x0dc0
running export coverage=55/55
endpoint mode=interrupt IN only
```

Confirmed validation sequence:

1. The normal unbound create/remove/unload probe passed without kernel messages or residual state.
2. A separate finite private gadget configured protocol 2, subclass 1, report length 4, and the 52-byte relative-mouse descriptor.
3. UDC binding succeeded and `/dev/hidg0` appeared immediately as character device major 241, minor 0 with root-only permissions.
4. The kernel logged `high-speed config #1` and `USB_STATE=CONFIGURED`; Windows recorded both `USB\\VID_1D6B&PID_0104` as a USB input device and `HID\\VID_1D6B&PID_0104` as a HID mouse.
5. The probe unbound and removed only its private gadget, unloaded `usb_f_hid`, and left no gadget, module, or `/dev/hidg*` residue.
6. No `hidg_bind FAILED`, `-19`, oops, or kernel fault occurred during the patched lifecycle.

The UDC's sysfs `state` still reported `not attached` during the successful configuration even though kernel and Windows enumeration evidence confirmed attachment. It is therefore recorded but not treated as the sole enumeration oracle. One `ERR: Operation not supported` line corresponds to an optional host control request that the composite setup path rejected with `-EOPNOTSUPP`; enumeration completed despite that stall.

This bound-only stage proved that the patched module can expose `/dev/hidg0`; report delivery and visible cursor movement were then tested separately below.

## Host-visible report validation

The final static AArch64 build of `hid_report_test.c` has SHA-256 `32b1fb558ae37c16d4d7d2b3252b183078faaefab8510dc3b9a82bb8eca84e38`. The validated behavior uses two distinct four-byte writes separated by one second:

```text
write(3, "\0\2\0\0", 4) = 4
nanosleep({tv_sec=1, tv_nsec=0}, NULL) = 0
write(3, "\0\376\0\0", 4) = 4
HID_REPORT_PAIR_COMPLETE=YES
```

Kernel endpoint diagnostics confirmed both requests completed on `ep4-int` with four-byte actual lengths. Windows simultaneously reported the live gadget as:

```text
USB\VID_1D6B&PID_0104\RG34XXSP-HID-TEST  USB Input Device  OK
HID\VID_1D6B&PID_0104\...                 HID-compliant mouse OK
```

The required `+2`/`-2` pair completed successfully but was intentionally tiny and the user could not reliably see the two-pixel twitch. A bounded visibility diagnostic then sent ten `+64 X` reports followed by ten `-64 X` reports; the user confirmed clear right-and-left cursor movement. For the decisive one-report proof, one four-byte `00 7f 00 00` report was sent and the user confirmed a single visible movement to the right. One `00 81 00 00` report then restored the cursor.

Cleanup unbound the UDC, removed only `usbjiggler-test`, unloaded the module, removed `/dev/hidg0`, and caused the Windows HID devices to disappear. The user physically disconnected and reconnected the USB-C cable, after which the complete sequence was repeated:

1. Exact module provenance and 55 symbol versions passed.
2. Protocol 2, subclass 1, report length 4, and the 52-byte descriptor were re-created.
3. Windows again enumerated both the USB input device and HID mouse with status `OK`.
4. The exact `+2`/`-2` utility completed again.
5. The user again confirmed visible rightward movement from one `00 7f 00 00` report.
6. The cursor was restored and final cleanup left no gadget, module, `/dev/hidg*`, or present Windows target device.

This confirms end-to-end and post-reconnect HID operation without recursive processes, unbounded retries, kernel flashing, or boot-partition access.

Post-review rollback guards were also exercised from a clean state. Setup refused a pre-existing state file and preserved its sentinel contents, then separately refused a pre-existing `usbjiggler-test` directory and left that directory intact. In both cases it loaded no module and created no additional gadget state. The final setup/report/cleanup cycle then passed again with the bounded report utility and returned to an empty ConfigFS gadget root with no loaded module or `/dev/hidg*` node.

## Reported or historical observations

- Earlier logs report forced loading with `insmod -f`, kernel taint from `module_layout`, and `hidg_bind FAILED -19` during UDC binding.
- Those observations remain useful evidence but do not explain `-19` and do not prove cable causality.
- The device-side source hash observed during this audit was `02d0cbe2356a09079715ff0126194d91a0301ca1123e0c97fcc75d0d5e764142`, which differs from the newly supplied local source. The deployed binary nevertheless matches the local binary hash.

## Build and probe policy

- `build_module.sh` now refuses vanilla or look-alike trees. It requires an explicitly acknowledged exact muOS build tree with `.config`, non-empty `Module.symvers`, generated headers, required symbols, and `CONFIG_USB_CONFIGFS_F_HID=m`.
- `scripts/hid_registration_probe.sh` defaults to finite read-only diagnostics.
- Module testing requires a matching SHA-256 provenance sidecar and a populated `__versions` section, uses only normal `insmod`, creates a uniquely named unbound gadget, validates create/remove/unload plus kernel logs, and never writes to or reads from `/dev/mmcblk0p4`.
- The unsafe supplied module must not be tested again. The validated IN-only module (`5a3cfc769fd592c8f8ad7aa4238df6d1c1c70e72de5ad923eb334280a35e07a9`) is deployed at `/mnt/mmc/ports/usbjiggler/kernel/usb_f_hid.ko`; boot integration remains forbidden.
