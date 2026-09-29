# Firmware installation, BLE updates, and recovery

## Required nRF memory layout

This project uses an nRF52840 SuperMini with the Adafruit bootloader built for `nice_nano`:

| Component | Required configuration |
|---|---|
| Bootloader | **0.11.0-keyboard1**, dual bank |
| Bluetooth stack | **S140 7.3.0**, firmware ID `0x0123` |
| Application Flash | `0x27000..0x87fff`, maximum `0x61000` bytes |
| Application RAM | Starts at `0x20008000` |
| Bootloader | Starts at `0xf4000` |
| Bond handover | Data at `0x20007f80`, CRC16 at `0x20007fbc` |

Bootloader and SoftDevice version numbers are separate. An application UF2 for S140 7.3.0 must not be treated as a bootloader/SoftDevice upgrade.

## First installation over USB

Follow the [hardware guide](hardware.md) and keep your original LPC backup before writing either controller. Keep USB and RESET accessible throughout installation.

Install development dependencies and build the nRF application first so PlatformIO downloads the required toolchain and packaging tools:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements-dev.txt
.venv/bin/pio run -e supermini_ble
.venv/bin/python tools/build_lpc.py
.venv/bin/python tools/build_bootloader.py
.venv/bin/python tools/package_nrf.py --softdevice-hex \
  build/bootloader/source/lib/softdevice/s140_nrf52_7.3.0/s140_nrf52_7.3.0_softdevice.hex
.venv/bin/python tools/verify_artifacts.py
```

The bootloader build also needs `git` and `make`. It fetches upstream commit `c67f0bcf0fa8e841426335b1bbde91cda6ca1f50` (0.11.0), applies the [single compatibility patch](../patches/bootloader/README.md), and verifies bond-handover addresses in the output ELF. Files are built locally; none of these build commands flashes hardware.

For a **matching nice!nano-compatible UF2 bootloader**:

1. Connect SuperMini by USB and double-tap RESET. Inspect `INFO_UF2.TXT` on its UF2 volume. Confirm nRF52840 / nice!nano compatibility; a different board ID needs its own installation procedure.
2. Copy `build/bootloader/update-nice_nano_bootloader-0.11.0-keyboard1_nosd.uf2` to the UF2 volume. Wait for the volume to disconnect/reappear, then double-tap RESET again if needed. This updater changes the bootloader, not S140.
3. Copy `build/nrf52840/usb_first_install_s140_7.3.0.uf2` to the UF2 volume. It contains S140 plus the application and preserves the existing MBR, UICR, and bootloader. Use it only for this USB installation step.
4. Let the application start, complete the inter-controller wiring with power off, power the assembly, and pair **Lenovo BLE Keyboard** through the host OS.
5. Flash LPC over BLE using the command below. The nRF resets LPC, connects over SWD, programs through ROM IAP, and verifies the full 24 KiB Flash.

The development board started with a nice!nano 0.6.0 UF2 bootloader and was upgraded by this route. That is not evidence that every SuperMini ships with the same layout. A blank nRF or an incompatible bootloader needs a separate SWD installation of the matching MBR/SoftDevice/bootloader and UICR settings; this project does not provide a universal one-command procedure for that case.

`keyboard_s140_7.3.0.uf2` is **application only**. The `usb_first_install_...uf2` file is **S140 plus application**. Neither is a file for the BLE `nrf` command, which takes the PlatformIO DFU ZIP.

## PC updater

For BLE updates only, install `tools/requirements-ble.txt` in a Python virtual environment. A normal BLE adapter is sufficient. Linux/BlueZ with an Intel BE200 was tested.

```sh
.venv/bin/python tools/ble_update.py scan
.venv/bin/python tools/ble_update.py status --address YOUR_KEYBOARD_ADDRESS

# LPC application at Flash address zero.
.venv/bin/python tools/ble_update.py lpc --address YOUR_KEYBOARD_ADDRESS \
  build/lpc11u13/keyboard.bin

# nRF application; built by PlatformIO for S140 7.3.0.
.venv/bin/python tools/ble_update.py nrf --address YOUR_KEYBOARD_ADDRESS \
  .pio/build/supermini_ble/firmware.zip
```

Replace the address placeholder with the scanner result; macOS uses an OS device UUID. On Windows use `.venv\Scripts\python.exe`. Pair in the OS first. `--pair` can request pairing, but Linux needs a running Bluetooth agent, usually provided by the desktop environment.

Do not run both update commands simultaneously. Wake the keyboard before connecting. A BLE disconnect after COMMIT does not prove that LPC programming failed: use `status` before retrying. The nRF keeps LPC powered until programming and verification complete.

nRF updates follow **application → bootloader → transfer → CRC validation → activation → application reconnect**. The tool checks the image vectors, size, CRC16, and required SoftDevice ID. It sends 20-byte packets with receipts every eight packets. BLE updates replace only the application; S140/bootloader installation remains a separate USB operation.

The updater supports a keyboard already connected by the OS HID driver. On Linux it retries only a specific transient service-discovery disconnect, at most three times within the connection deadline. It does not blindly replay firmware writes. After nRF activation it waits five seconds before the final application check because immediate disconnect raced HID attachment on the development BlueZ host.

## VS Code tasks

Use **Terminal → Run Task…**:

- **Keyboard: flash LPC over BLE** builds LPC and waits for programming/readback verification.
- **Keyboard: flash nRF over BLE** builds nRF and performs the BLE DFU cycle.

Both ask for the keyboard address and use the project's `.venv`. Setup/build failures prevent flashing. The tasks use the same CLI as the commands above and do not automatically fall back to USB.

## Recovery

If a transfer leaves the nRF in its DFU bootloader, it may advertise as `AdaDFU` with a different address. Scan again. If an unfinished session rejects START, reset that bootloader session:

```sh
.venv/bin/python tools/ble_update.py reset-dfu --address BOOTLOADER_ADDRESS
.venv/bin/python tools/ble_update.py scan
.venv/bin/python tools/ble_update.py nrf --address BOOTLOADER_ADDRESS \
  --application-address YOUR_KEYBOARD_ADDRESS \
  .pio/build/supermini_ble/firmware.zip
```

The reset command checks the bootloader service revision and waits for a real disconnect. After reset, either the application or the bootloader can return; rescan rather than assuming its address. `--bootloader-address` is also available when starting from the application and the DFU address is known to change.

If BLE is unavailable, double-tap nRF RESET and use its USB UF2 bootloader. With the correct S140 already installed, copy the application-only UF2. An Arduino 1200-baud USB touch may enter serial DFU without mounting a UF2 volume; use double RESET for the volume, or the matching Adafruit serial DFU tool with the ZIP.

For LPC recovery, use its reset/SWD window and a suitable external probe with the nRF signal drivers disconnected. Restore your own complete original dump if reverting to stock USB behavior. Do not hold LPC RESET low throughout SWD access. See [hardware.md](hardware.md).

## Update access and integrity

The application update services require a secured/bonded BLE peer. The LPC upload belongs to its initiating connection; another connected host cannot write into or abort that session. After COMMIT, programming continues even if the initiating host disconnects. Entering nRF DFU releases keys and disconnects all hosts.

The Adafruit legacy DFU bootloader has writable DFU characteristics and this project does not enable signed-image verification or anti-rollback. Bond handover is not cryptographic image authorization. CRC detects corruption; it does not authenticate firmware authorship.

## LPC update protocol

Service: `6c4b1000-5f0a-479b-9f31-5d67426590b7`. Characteristics ending in `1001`, `1002`, and `1003` are control (write), data (write), and status (read/notify). Multi-byte fields are little-endian.

| Message | Payload |
|---|---|
| BEGIN | `01 size:u32 crc32:u32` |
| DATA | `offset:u32 bytes[1..16]` |
| COMMIT | `02` |
| ABORT before programming | `03` |
| STATUS, 20 bytes | `version:u8 state:u8 error:u8 sequence:u8 received:u32 size:u32 crc32:u32 detail:u32` |

The sequence increments after each processed write so the CLI can reject a stale acknowledgement. States: idle=0, receiving=1, pending=2, programming=3, done=4, failed=5. Error definitions are in `include/bridge/LpcImage.hpp` and `tools/keyboard_update/lpc.py`.

LPC images must be `0x300..0x6000` bytes and four-byte aligned, with valid vectors/checksum. Accepted CRP words are `0xffffffff` and the known factory word `0x1a0888a1`. The complete image is staged and validated before COMMIT starts erasing/programming.

For SWD error 9, `detail` encodes phase, reason, request, and ACK/status from most-significant to least-significant byte. The CLI decodes these fields. An invalid ACK of 7 can indicate that the target is not responding; inspect power, wiring, ISP, and reset timing first.

## Read-only status

`status` reads LPC update state plus these encrypted characteristics in service `6c4b2000-5f0a-479b-9f31-5d67426590b7`:

| Suffix | Information |
|---|---|
| `2001` | UART health, uptime, GPIO levels, power-off count, wake flag |
| `2002` | [Macros/lighting and LPC command ACK](factory_controls.md#status) |
| `2003` | [Host profiles](bluetooth_profiles.md#status-and-validation) |
| `2004` | UART baud rate and actual BLE interval/latency per connection |

The 20-byte link status is `version:u8 flags:u8 power_offs:u16 uptime_ms:u32 uart_bytes:u32 valid_frames:u32 frame_age_ms:u32`. Version 2 flags are: bit0 live link, bit1 supply enabled, bit2 RESET level, bit3 RX level, bit4 UART enabled, bit5 last advertising interval at least 2 s, bit6 `RESETREAS.OFF` observed at nRF startup. `frame_age_ms=0xffffffff` means no valid frame yet. The CLI also accepts version 1.

Counters reset when nRF restarts and persist across LPC power cycling. A HIGH power-control GPIO does not measure the voltage at LPC VDD. Status contains no ordinary key contents and cannot reset or program hardware by itself.

The 18-byte timing status is `version=1, selected:u8, baud:u32, (interval:u16, latency:u16)[3]`. Intervals are in 1.25 ms units; zero means disconnected. The host decides the accepted connection parameters.
