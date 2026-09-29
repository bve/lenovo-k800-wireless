# Original G keys, macro controls, and LEDs

The original LPC dump, Lenovo's manual, and static analysis of Lenovo's v1.15 PC application establish the mapping below. The installer was unpacked for inspection; its Windows application was not executed. This document describes the original protocol, while [current controls](../docs/factory_controls.md) documents the replacement firmware.

## Original division of work

M1–M3 select three banks of G1–G6 assignments, 18 slots total. The manual's recording sequence is **M → MR → G → keys → MR**. MR is steady while waiting for G, blinking while recording, and off afterward.

Unassigned G1–G5 produce digits 1–5. G6 targets a compatible Lenovo headset's surround function in the PC software; the keyboard's fallback HID usage is `0x6a` (F15). The LPC itself does not implement headset processing.

The manual requires the desktop application for macro recording. Both ends of the protocol confirm that LPC reports special-button states while the PC records/plays assignments and sends indicator commands. The nRF replacement implements that PC-side responsibility.

## Recovered matrix mapping

Rows and columns are zero-based. R15 is `PIO0_20`; R16 is `PIO1_31`. Codes B0–B9/C0–C2 are internal factory codes, not HID usages.

| Button | Factory code | Matrix index | Row / column | Table address | Fallback |
|---|---|---:|---|---|---|
| G1 | B0 | 123 | 15 / 3 | `0x5598` | Digit 1, `0x1e` |
| G2 | B1 | 127 | 15 / 7 | `0x55a0` | Digit 2, `0x1f` |
| G3 | B2 | 124 | 15 / 4 | `0x559a` | Digit 3, `0x20` |
| G4 | B3 | 122 | 15 / 2 | `0x5596` | Digit 4, `0x21` |
| G5 | B4 | 125 | 15 / 5 | `0x559c` | Digit 5, `0x22` |
| G6 | B5 | 126 | 15 / 6 | `0x559e` | F15, `0x6a` |
| MR | B6 | 134 | 16 / 6 | `0x55ae` | Host control |
| M1 | B7 | 128 | 16 / 0 | `0x55a2` | Host control |
| M2 | B8 | 130 | 16 / 2 | `0x55a6` | Host control |
| M3 | B9 | 129 | 16 / 1 | `0x55a4` | Host control |
| Windows lock | C0 | 135 | 16 / 7 | `0x55b0` | Suppress Windows keys |
| Backlight C1 | C1 | 132 | 16 / 4 | `0x55aa` | First group brightness |
| Backlight C2 | C2 | 133 | 16 / 5 | `0x55ac` | Second group brightness |

The main table is at `0x54a2`; alternate G mappings are at `0x55c2` in matrix-index order. Flag `0x10000239` selects fallback mappings when zero; handlers `0x4086`, `0x3e7e`, and `0x39f4` read it.

## Original USB protocol

Handler `0x3bc0` updates special-key bits; `0x3b20` with argument 8 sends four bytes to endpoint `0x82`. The descriptor at `0x5310..0x537c` declares vendor usage page `0xff00`, Input Report ID 3, response ID 4, and command Feature Report ID 5.

```text
03 controls G M
controls: bit0=C0, bit1=C1, bit2=C2
G:        bit0=G1 ... bit5=G6
M:        bit0=MR, bit1=M1, bit2=M2, bit3=M3
```

The buffer begins at `0x100002bd`. Masks are at `0x547e`, `0x5481`, and `0x5485`. These are pressed/released states, not selected bank numbers or macro contents.

Commands are handled by `0x2f9c` as `[05, operation, group, selector/mask, data0, data1]`: operation 1 reads, 2 writes. Group 1/write with selector `0x55` enables host mode; `0xaa` disables it and restores original lighting states. Group 3 at `0x312e` controls MR/M1/M2/M3. MR values 0/4/7 mean off/on/blinking; the loop at `0x33c8` performs blinking.

Original C0 controls Windows lock, with `0x3e7e` suppressing both Windows keys while enabled. C1/C2 cycle two brightness groups through 4 → 3 → 2 → 1 → 0 → 4. Group 5 invokes firmware update through `0x2eb8` and sector-5 erase; it is not macro storage.

## LED hardware

Functions `0x485a`, `0x48bc`, `0x491e`, and `0x497c` write I2C address bytes `0xc0`/`0xc2`, corresponding to 7-bit addresses `0x60`/`0x61`. SCL is `PIO0_4`, SDA is `PIO0_5`.

- `0x60`: M3 channel 0, M2 channel 1, MR channel 2, C1 backlight channel 3.
- `0x61`: M1 channel 0, Windows-lock LED channel 1, C2 backlight channel 3.
- Num/Caps/Scroll: `PIO1_15` / `PIO1_14` / `PIO0_23`, active LOW, handler `0x2f5a`.

MODE1=`0x81`, MODE2=`0x05`, LEDOUT=`0x55`/`0x45`, and PWM registers 2–5 match PCA9633's register layout. The chip marking was not checked. Brightness 1/2/3/4 uses `e8/e0/c7/9f` on `0x60` and `e6/e0/c7/9f` on `0x61`. External inversion makes LEDOUT field 1 off and field 2 PWM. MR toggles every 250 ms. The exact physical keycap groups controlled by C1/C2 need visual verification.

## PC application evidence

The x86 `HID.exe` image base is `0x00400000`:

| Address | Observed responsibility |
|---|---|
| `0x4076f0` | Open USB `17ef:6082`, vendor usage page `0xff00` |
| `0x4076d0`, `0x407880` | Read callback; compare Report ID 3 bits |
| `0x40794c..0x407976` | Post message `0x656` with byte/bit/release state |
| `0x401f20` | Handle G assignment or recording-slot selection |
| `0x402041..0x40209c` | MR toggles recording; M bits choose bank |
| `0x4021a0` | Complete recording and replace selected assignment |
| `0x402e40` | Execute a G assignment in the selected bank |
| `0x403610` | Send MR 0/4/7 and M indicator commands |

Paths such as `Lenovo Keyboard K800/Gaming.ini`, `Lenovo Keyboard K800/MacroSetting`, and `MacroProfile%d` support the K800 naming. The M-group handler and manual establish the button names; numeric LPC codes alone would not.

## Current implementation differences

All these controls use the existing matrix. The replacement adds autonomous macros on nRF, BLE profile selection by M double-tap, sleep on the former Windows-lock key, and backlight mode 5 for all LEDs off. LPC receives LED commands over `PIO0_10` from nRF `P0.11`; the same line returns to SWCLK during programming. See [current behavior](../docs/factory_controls.md).

## Sources and reproduction

```sh
python3 tools/inspect_firmware.py path/to/your-original.bin --output-dir build/factory_analysis
```

The hash check and exported tables are reproducible; button names also rely on the manual/application analysis above. Vendor binaries and installers are not distributed.

- [Lenovo keyboard support](https://support.lenovo.com/us/en/accessories/acc100234-lenovo-y-gaming-mechanical-switch-keyboard).
- [Lenovo quick-start guide v1.0](https://download.lenovo.com/consumer/options/lenovo_y_gaming_mechanical_switch_keyboard_qsg_v1.0_201509.pdf), printed pages 2 and 4; SHA-256 `d9e48a8d430ab561ff9daa7f5ebb6aff5cc0e6d2eb535c975844116d1e5eeed6`.
- [Lenovo application v1.15](https://download.lenovo.com/consumer/options/lenovo_y_gaming_mechanical_keyboard_v1.15_201510.exe); installer SHA-256 `1a3794338e0fe586d08fcd7d20c30311531a43e1a021916474948343830172fe`, extracted HID.exe SHA-256 `82116916e9a79dd4e67bab2de48a82bf2498dd82f06b8b2a34fb9353e99366c8`.
