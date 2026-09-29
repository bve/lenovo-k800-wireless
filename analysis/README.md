# Reverse-engineering notes

The mapping was recovered from an owner's original 24 KiB LPC11U13 Flash dump with SHA-256:

```text
ae89e2b3028a767ad9787610cf032206ca518004adc4202b9b3b9675a610648e
```

The factory binary is not distributed. All offsets below belong to that exact revision. A similar product name is not enough to assume binary compatibility. Initial findings were static analysis; later hardware validation is summarized in the main [README](../README.md#validation-and-limitations).

| Object | Address / value | Evidence |
|---|---|---|
| Flash | `0x0000..0x5fff`, 24 KiB | Image size and LPC11U13 memory map |
| Boot vectors / Reset | `0x0000` / `0x0291` | First vector table |
| Application vectors / Reset | `0x2000` / `0x44e5` | Second vector table |
| Application loop | `0x33c8` | Startup through `0x20c0` |
| Boot USB | `1fc9:000b`, descriptor `0x1b00` | NXP LPC11Uxx DFU |
| Keyboard USB | `17ef:6082`, descriptor `0x5380` | Lenovo Y Gaming Mechanical Switch Keyboard |
| Matrix initialization | `0x4264` | Eight inputs, eighteen output lines |
| Row scan / column read | `0x41ba` / `0x418e` | GPIO table use |
| Row-change handler | `0x4086` | `row * 8 + column` key lookup |
| Keyboard report builder | `0x3e7e` | Modifiers, six usages, extra report |
| HID send / endpoint write | `0x3b20` / `0x2a9a` | EP `0x81` and `0x82`, USB SRAM |
| Matrix state | `0x100003e8`, 18 bytes | Literal at `0x4310` |
| Primary HID buffer | `0x100002c1`, 8 bytes | Literal at `0x3d28` + 9 |
| Column table | `0x5492` | Eight pin numbers followed by eight ports |
| Row table | `0x55ce` | Eighteen pin numbers followed by eighteen ports |
| Main key table | `0x54a2`, 144 pairs | Class and value |
| Alternate key table | `0x55c2`, six pairs | Matrix indices 122–127, host-mode flag zero |
| Media masks | `0x548b`, seven bytes | Selectors `0xa0..0xa6` |

The main scan/report loop is gated by RAM state and a timer. Reading its HID buffer through SWD does not prove the original firmware scans without a USB host. The replacement firmware therefore supplies its own scanner and does not call the vendor application or bootloader.

Trailing erased bytes after `0x571a` are not safe patch space: `0x2eb8` erases sector 5 and writes a 256-byte block at `0x5f00`; the last word carries marker `0x27989008`, also checked by the bootloader. An erase affects data from `0x5000` onward.

SWDIO (`PIO0_15`, LQFP48 pin 39) and SWCLK (`PIO0_10`, pin 29) do not appear in the matrix pin tables. They can be GPIO but do not provide the required hardware USART TX/RX functions, so the replacement uses software UART on those pads.

## Optional reproduction

Normal builds use the checked-in header and do not run these commands. If you have your own matching original dump:

```sh
python3 tools/inspect_firmware.py path/to/original.bin --output-dir build/factory_analysis
python3 tools/generate_layout.py path/to/original.bin
```

The scripts reject a different hash instead of applying stale offsets. Extraction produces `summary.json`, `matrix.csv`, and `factory_controls.json`; generation updates `include/bridge/KeyboardLayout.hpp`. The checked-in analysis tables describe the original firmware, not the current BLE state.

Optional Ghidra scripts under `tools/ghidra/` support headless disassembly/decompilation. Ghidra 12.1.2 was used during research. Some Keil switch tables were not fully reconstructed by decompilation and were checked against disassembly. Decompiled vendor source is not distributed or built by this project.

See [factory controls](factory_controls.md) for special buttons and LED commands, [NXP LPC11U1x](https://www.nxp.com/docs/en/data-sheet/LPC11U1X.pdf), and [NXP UM10462](https://www.keil.com/dd/docs/datashts/nxp/lpc11uxx/um10462.pdf) for the chip reference.
