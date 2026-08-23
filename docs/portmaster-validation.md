# PortMaster Validation

## Validated target

| Item | Confirmed value |
| --- | --- |
| Device | Anbernic RG34XXSP |
| OS | muOS 2601.0 JACARANDA / muOS 2 NeXT |
| Architecture | AArch64 Allwinner H700 / `sun50iw9p1` |
| Kernel | Linux 4.9.170 `#2 SMP PREEMPT` |
| UDC | `5100000.udc-controller` |
| SDL | 2.28.5, queried on the device |
| HID interface | IN-only ConfigFS `hid.usb0`, four-byte relative mouse reports |

The exact module was built with all 55 imported symbol CRCs aligned to the running kernel and accepted only after non-forced registration, ConfigFS create/remove, and unload validation. No boot storage was modified.

## Automated evidence

- Runtime tests compile with warnings as errors and cover strict settings parsing, atomic persistence, every report pattern, 1 px through 2,000 px interpolation, fixed 16 ms frames, one/two-second gesture timing, helper timeout and cancellation, disconnect/retry, async cleanup, cleanup-once, and cleanup failure ownership.
- UI tests compile with warnings as errors and cover keyboard, joystick button, hat, and axis mapping; cross-source deduplication; held-source promotion/release; focus; clamping; accelerated adjustment; and exit effects.
- ASan and UBSan pass for the runtime suite.
- SDL dummy-driver smoke tests render preparing, ready, active, settings, help, disconnected, error, cleanup, and exit captures twice at 640×480 with byte-identical results.
- GCC Linaro 5.3 cross-build verifies the audited SDL 2.28.5 library hash `40d0616f6b97d447f47e1e77c116d84bc4d3d7a5e33a50e2f4f472f9b250f46d`, produces an AArch64 ELF using `/lib/ld-linux-aarch64.so.1`, and requires no GLIBC symbol newer than 2.17.
- Source and binary audits find no boot-partition, installer, restore, reboot, forced-module-load, `system`, or `popen` path.

## On-device acceptance

Confirmed through the normal PortMaster launcher after a clean reboot:

- The fullscreen window completely covers the muOS home screen.
- D-pad navigation, held acceleration, A, B, and Start work with GPTOKEYB and direct SDL input active together.
- Settings reach the full 1–300 second and 1–2,000 pixel ranges.
- Horizontal, Vertical, Square, and Random previews remain responsive.
- A large configured movement produces smooth host-visible Windows cursor motion and returns net-zero instead of teleporting.
- Stop prevents another cycle; exit shows `CLEANING USB STATE` before returning to Ports.
- Successful setup and cleanup log `GADGET_SETUP_READY=YES` and `GADGET_CLEANUP_COMPLETE=YES`.
- After exit there is no app or GPTOKEYB process, `usbjiggler-test` gadget, `/dev/hidg0`, or loaded `usb_f_hid` module.

Final clean-archive acceptance also confirmed:

- The staged `ARTIFACTS.sha256` manifest verified every extracted file before installation, and the installed app directory contains only the whitelisted release files plus the user-created profile.
- An existing 6 second / 1,424 pixel / Horizontal profile survived the clean package upgrade.
- A new 11 second / 1,777 pixel / Random profile survived normal exit and a complete PortMaster relaunch.
- Unplugging USB-C while ACTIVE reached the DISCONNECTED screen after the next cycle; reconnecting and pressing A returned to READY and resumed smooth host movement.
- Sending TERM to the PortMaster launcher while ACTIVE caused the app to complete gadget cleanup, then stopped GPTOKEYB before `pm_finish`; the handheld returned to Ports and Windows removed the HID mouse.
- After forced termination there was no launcher, app, or GPTOKEYB process, gadget directory, `/dev/hidg0`, or loaded `usb_f_hid` module.
- A second normal PortMaster launch after forced termination reached READY with the persisted profile, moved the host cursor, and exited cleanly with zero residue.

## Core artifact hashes

| Artifact | SHA-256 |
| --- | --- |
| `usbjiggler` | `09eae28993dc9c36329726deb5a1e70219657bf2dccd7d2206492b891c588cd9` |
| `kernel/usb_f_hid.ko` | `5a3cfc769fd592c8f8ad7aa4238df6d1c1c70e72de5ad923eb334280a35e07a9` |
| `kernel/usb_f_hid.ko.provenance` | `f31c7f9fd82b2a25654d6ebd6b41e88442b7cd2dc1368bbd05f8e087a17de0e9` |
| `scripts/gadget_setup.sh` | `9033f293e83be55ee4a0065802e6589e703e782a9e6d64709fd29a49389bf98f` |
| `scripts/gadget_cleanup.sh` | `2b4c6b28dc87d840f273a97a779313ca6a5e19a077a19870f608fd8176cb4b2c` |
| `screenshot.png` | `7fc8a4eace20af3ed9e8364c2760866ed497895f35a0f115d9b3e274444bfbd9` |

`ARTIFACTS.sha256` in the staged package is generated from the final whitelisted release contents.
