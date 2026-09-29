# Macros, original controls, and lighting

The original keyboard sends special-button states to Lenovo's desktop software, which manages macros and sends LED commands back. In this replacement, **the nRF performs that host-side role**. LPC scans the matrix and drives the LEDs. Neither controller runs the original Lenovo binary.

## Controls

| Button | Action |
|---|---|
| M1 / M2 / M3 | Select one of three banks of six G keys; double-tap also selects the BLE profile |
| MR | Start recording; press again to finish |
| G1–G6 | Select a recording slot or play its assignment once |
| Two backlight buttons | Cycle the corresponding group: 4 → 3 → 2 → 1 → 0 → 5 → 4 |
| Crossed-out Windows | Sleep LPC and nRF; press again to wake |

Defaults: bank M1, brightness 4/4, G1–G5 as digits 1–5, G6 as F15. Windows keys are enabled; the former Windows-lock button now controls sleep. Its old lock LED is not used. Num/Caps/Scroll indicators follow the selected host's HID output report.

## Recording

1. Select M1, M2, or M3.
2. Press MR; its indicator stays on.
3. Press the destination G key; MR blinks.
4. Type the key sequence or combination.
5. Press MR again; its indicator turns off and the assignment is replaced.

Recorded typing also reaches the foreground application. To clear an assignment, use **MR → G → MR** without typing; the default key returns.

A macro holds up to 128 press/release events over at most 60 seconds, or up to 64 simple taps without extra modifiers. Ordinary keys, modifiers, and the seven media functions are recorded. Delay before the first event is discarded; subsequent event spacing is preserved. BLE congestion slows playback rather than collapsing distinct taps. Modifiers are pressed before letters in a simultaneous chord.

MR finishes recording by releasing any still-held keys. On overflow, 6KRO error during recording, or connection loss, an unfinished recording is cancelled and the previous assignment retained. Capacity/6KRO errors blink MR for two seconds. Disconnect stops playback; a held G key does not retrigger on reconnect. Another assigned G replaces playback; M and MR also stop it. Physical held keys remain independent of releases inside the macro.

Lenovo desktop functions requiring Windows APIs, such as launching an application or controlling headset surround sound, are not reproduced by ordinary BLE HID. G6 defaults to F15 and can receive a keyboard macro. Lenovo desktop profile files are not imported.

## Persistence

Macros, bank selection, brightness, and BLE profile state are stored under `/keyboard` in the nRF's InternalFS, separately from Bluefruit bond keys. Files carry a CRC and are written through a temporary file, readback, and atomic replacement.

Saving begins after keys are released and at least 100 ms of idle time, with at most one file per second. A lower-priority worker performs Flash operations so they do not synchronously stall matrix/HID handling. If an assignment changes during a save, the newer state remains pending for another write.

Allow several seconds before physically removing power; `status` shows pending changes. A normal BLE disconnect retains completed recordings in RAM while nRF remains powered. Corrupt files are not loaded or automatically formatted/overwritten; the affected slot uses its default until a new assignment is saved. Manual sleep waits for pending saves.

## Backlight and indicators

Level 0 turns off only the selected group. The next press enters **mode 5**, which turns off both groups and all M/MR/Num/Caps/Scroll indicators. Their logical state is preserved. Pressing either backlight button leaves mode 5 and enables that group at level 4; if the other group held mode 5, it returns to 0.

M/MR presses and host LED changes do not turn lights back on during mode 5. Mode 5 is saved and survives wake. [Sleep](sleep.md) temporarily turns all LEDs off without changing the saved brightness.

| Function | Driver channel or LPC GPIO |
|---|---|
| M3 / M2 / MR | I2C `0x60`, channels 0 / 1 / 2 |
| C1, matrix R16C4 | I2C `0x60`, channel 3 |
| M1 / old Windows-lock LED | I2C `0x61`, channels 0 / 1 |
| C2, matrix R16C5 | I2C `0x61`, channel 3 |
| Num / Caps / Scroll | `PIO1_15` / `PIO1_14` / `PIO0_23`, active LOW |

I2C uses `PIO0_4` (SCL) and `PIO0_5` (SDA). The recovered register layout matches PCA9633, but the physical chip markings have not been verified. The exact C1/C2 correspondence to keycap groups needs visual confirmation. I2C ACK confirms command acceptance, not that a particular LED is visibly lit. An unavailable driver is retried once per second without blocking typing.

## Inter-controller protocol

The existing SWD pads carry UART after the three-second startup window. All multi-byte fields are little-endian. CRC-16/CCITT-FALSE covers every byte before the CRC.

```text
LPC -> nRF, 26 bytes:
4b 42 02 sequence rows[18] control_ack flags crc16
flags: bit0 valid command received
       bit1 I2C 0x60 ACK; bit2 I2C 0x61 ACK
       bit3 sleep supported; bit4 lights off, keys released, entering sleep

nRF -> LPC, 12 bytes:
4c 43 01 sequence bank mr brightness_A brightness_B flags sleep crc16
bank: 0..2; mr: 0 off, 4 on, 7 blinking
brightness: 0..4; 5 all LEDs off
flags: bit0 legacy Windows lock (sent as 0); bits1..3 Num/Caps/Scroll
sleep: 0 normal; 1 Power-down request with button wake
```

Commands follow a complete matrix frame and repeat until the sequence is acknowledged. Stale frames already queued in UART do not trigger another command. MR blinking runs locally on LPC. nRF accepts matrix v1/v2; manual sleep requires the LPC support flag, and all-LEDs-off needs matching firmware on both controllers.

## Status

`tools/ble_update.py status --address YOUR_KEYBOARD_ADDRESS` prints a `Controls` line. `pending_macros=0` and `pending_settings=0` mean no pending saves. `matrix_v2`, `command_ACK=1`, `I2C_60=1`, and `I2C_61=1` confirm protocol and LED-driver command acceptance. Values may be zero before the first LPC frame arrives.

Encrypted read-only characteristic: `6c4b2002-5f0a-479b-9f31-5d67426590b7`, 20 bytes:

```text
version=1, bank, recording, windowsLocked, brightnessA, brightnessB,
hostLeds, flags, pressedG, pressedM, pressedControls, matrixVersion,
commandSequence, acknowledgedSequence, lpcFlags, recordLed, dirtyMacros:u32
```

`windowsLocked` remains for compatibility and is always zero. `recording`: 0 idle, 1 choose G, 2 recording. `flags`: bit0 storage healthy, bit1 playing, bit2 recording error, bit3 settings pending. Special-key masks follow the recovered factory report. Ordinary key and macro contents are not exposed by this status characteristic.

See [factory reverse-engineering notes](../analysis/factory_controls.md) for the original mapping and the distinction between stock behavior and current firmware.
