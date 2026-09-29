#!/usr/bin/env python3
"""Package the built S140 v7 application as UF2 without touching a board.

UF2 layout: https://github.com/microsoft/uf2
nRF52840 family and application base: https://github.com/adafruit/Adafruit_nRF52_Bootloader
"""
import argparse
import struct
import subprocess
from pathlib import Path

from inspect_firmware import PROJECT_ROOT
from toolchain import arm_tool


def write_uf2(path: Path, data: bytes, address: int):
    count = (len(data) + 255) // 256
    with path.open("wb") as target:
        for number in range(count):
            payload = data[256 * number:256 * (number + 1)].ljust(256, b"\xff")
            header = struct.pack("<8I", 0x0a324655, 0x9e5d5157, 0x2000,
                                 address + 256 * number, 256, number, count, 0xada52840)
            target.write(header + payload + bytes(220) + struct.pack("<I", 0x0ab16f30))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, help="Directory containing arm-none-eabi-objcopy")
    parser.add_argument("--softdevice-hex", type=Path,
                        help="Also create a first-install USB UF2 containing S140 7.3.0 (requires intelhex)")
    args = parser.parse_args()
    try:
        objcopy = arm_tool("objcopy", args.toolchain)
    except FileNotFoundError as error:
        parser.error(str(error))
    build = PROJECT_ROOT / ".pio/build/supermini_ble"
    output = PROJECT_ROOT / "build/nrf52840"
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "keyboard.bin"
    subprocess.run([objcopy, "-O", "binary", str(build / "firmware.elf"), str(binary)], check=True)
    data = binary.read_bytes()
    if len(data) < 8 or len(data) > 0x88000 - 0x27000:
        raise SystemExit("Application does not fit the expected flash region")
    stack, reset = struct.unpack_from("<II", data)
    if not (0x20006000 <= stack <= 0x20040000 and stack % 8 == 0
            and reset & 1 and 0x27000 <= (reset & ~1) < 0x27000 + len(data)):
        raise SystemExit("Unexpected application vectors; do not package with this flash base")
    uf2 = output / "keyboard_s140_7.3.0.uf2"
    write_uf2(uf2, data, 0x27000)
    if args.softdevice_hex:
        from intelhex import IntelHex
        source = IntelHex(str(args.softdevice_hex))
        if any(address >= 0x27000 for address in source.addresses()):
            raise SystemExit("Use the SoftDevice-only HEX, not a merged bootloader HEX")
        sd = source.tobinstr(start=0x1000, end=0x26fff)
        size = struct.unpack_from("<I", sd, 0x2008)[0]
        fwid = struct.unpack_from("<H", sd, 0x200c)[0]
        sd_id, version = struct.unpack_from("<II", sd, 0x2010)
        if (size, fwid, sd_id, version) != (0x27000, 0x123, 140, 7003000):
            raise SystemExit("SoftDevice metadata is not S140 7.3.0")
        bootstrap = output / "usb_first_install_s140_7.3.0.uf2"
        # Keep the board's existing MBR, UICR and bootloader. Old UF2 0.6.0
        # explicitly supports SoftDevice as part of the application image.
        write_uf2(bootstrap, sd + data, 0x1000)
        print(f"Built {bootstrap.relative_to(PROJECT_ROOT)}; USB first installation only")
    print(f"Built {uf2.relative_to(PROJECT_ROOT)}; requires S140 7.3.0, board not flashed")


if __name__ == "__main__":
    main()
