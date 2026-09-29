# Building and testing

## Host tests

From the repository root, with a C++17 compiler and Python 3.10+:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements-dev.txt
.venv/bin/python tools/run_tests.py
```

The runner builds every `tests/test_*.cpp` into `build/tests/`, runs it with AddressSanitizer and UndefinedBehaviorSanitizer, then runs the Python `unittest` suite. Set `CXX` to choose another compiler. Use `--no-sanitizers` only when the compiler/platform does not support those sanitizers. Tests do not connect to or flash hardware and do not require a factory firmware dump.

| Area | Coverage |
|---|---|
| Matrix/protocol | CRC vectors, corrupt/dropped/inserted bytes, incomplete-frame timeout, mapping, media, 6KRO, report queue |
| Debounce | Synthetic chatter, per-key training and bounds, late new faults, clean-series reset, modifiers, timer wraparound |
| Macros/lighting | Recording, playback, default assignments, bounds, persistence snapshots/CRC, PWM registers |
| Profiles/HID | Three modeled connections, reconnect order, double-tap timing, releases, handle reuse, isolated reads and notifications |
| Sleep | Held startup key, storage/ACK waits, old firmware, timeout, supply retention |
| LPC programming | Image/vector/CRP validation, SWD parity/ACK, IAP order, bad targets, write/readback failures |
| BLE updater | Session ownership, incomplete transfer, CRC, receipts, disconnect/reconnect and activation errors |

Transport fakes exercise the real HID and LPC update classes, but cannot prove RF behavior or switch mechanics.

## Firmware artifacts

```sh
.venv/bin/pio run -e supermini_ble
.venv/bin/python tools/build_lpc.py
.venv/bin/python tools/package_nrf.py
.venv/bin/python tools/verify_artifacts.py
```

Verification checks LPC size/vectors/checksum/CRP, UF2 address/family/content, and the DFU ZIP image/CRC/SoftDevice requirement. It performs no hardware writes. The optional `--original path/to/your-dump.bin` additionally verifies the known factory SHA-256.

GitHub Actions runs host tests and both firmware builds from a clean checkout. Vendor dumps, local `.pio`, `.venv`, and `build` directories are not needed in the repository. The optional bootloader build fetches its own pinned upstream source and is documented separately because it also downloads vendor components.

## Hardware validation to repeat

Start with a verified original backup and the [wiring guide](hardware.md). Then:

1. Confirm `status`: live UART, command ACK, intended profile, and actual BLE timing.
2. Verify single presses/releases and modifier chords. Test the full physical matrix and media keys, not only a few letters.
3. For chatter, compare a known count of slow physical taps against host down/up events; distinguish OS autorepeat during a hold. Also check deliberate fast repeated taps. Do not log unrelated typing.
4. Record and play a macro in each bank, switch banks during typing, and check pending saves clear.
5. Cycle both backlight groups through mode 5 and verify every indicator stays dark.
6. Pair three real hosts and test double-tap routing, held-key release, reconnect order, and LED state per host.
7. Perform button sleep/wake cycles and confirm `woke_from_system_off=1` plus recovered UART.
8. Test both BLE update paths and USB/SWD recovery before enclosing the hardware.
9. Measure complete-board current and verify the power/charging design before claiming battery runtime.

Development checks confirmed typing, MR indication/playback, lighting command ACKs, full LPC programming/readback, nRF BLE self-update, and button wake. They did not fully validate all the items above. The latest adaptive algorithm passed model tests and was installed, but its final physical switch test received no presses.
