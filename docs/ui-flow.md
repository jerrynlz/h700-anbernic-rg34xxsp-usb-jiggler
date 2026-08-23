# USB Jiggler UI Flow

The SDL2 interface renders at a fixed 640×480 logical resolution and treats `AppRuntimeSnapshot` as the sole source of HID status.

## Screens

- **Preparing / Retrying / Exiting:** animated progress deck while the runtime worker owns blocking lifecycle work.
- **Ready:** movement preview, saved profile, standby countdown, and `A Start`.
- **Active:** animated pattern path, report pulse, report count, and live countdown.
- **Settings:** focused Interval, Movement, and Pattern cards with bounded gamepad adjustment.
- **Help:** gamepad controls and reversible cleanup behavior.
- **Disconnected / Error:** runtime-provided failure text and `A Safe Retry` without inferring cable state from UDC status.
- **Exit confirmation / Cleanup:** `A Exit` or `B Cancel`; confirmation starts non-blocking cleanup and keeps an animated cleanup screen responsive until the runtime reports completion. Successful exit requires runtime cleanup success.

## Controls

| Input | Action |
| --- | --- |
| D-pad Up/Down | Open settings and change focused row |
| D-pad Left/Right | Adjust focused value |
| A | Start, stop, retry, select, or confirm |
| B | Back or request exit |
| Start | Open or close help |

Keyboard and direct SDL joystick button, hat, and axis events converge on the same `UiAction` model. Holding Left or Right accelerates adjustment while clamping interval to 1–300 seconds and movement to 1–2,000 pixels. Large movement values remain HID-safe because the runtime interpolates them into legal relative reports roughly every 16 ms.

## Deterministic Captures

`usbjiggler --render-screens <directory>` uses SDL's dummy video driver and software renderer without starting the HID runtime. It writes 640×480 BMP captures for preparing, ready, active, settings, help, disconnected, error, cleanup, and exit screens. `tests/render_smoke.sh` renders twice and verifies dimensions and byte-stable output.
