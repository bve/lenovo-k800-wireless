#!/usr/bin/env python3
"""Check flashable artifacts, optionally also a local original firmware dump."""
import argparse
import struct
from pathlib import Path

from inspect_firmware import PROJECT_ROOT, FirmwareImage
from keyboard_update.images import NrfPackage


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, help="Optionally verify a matching original dump")
    args = parser.parse_args()
    if args.original:
        FirmwareImage(args.original)
        print("PASS: original firmware SHA-256")
    lpc = (PROJECT_ROOT / "build/lpc11u13/keyboard.bin").read_bytes()
    require(0x300 <= len(lpc) <= 0x6000, "Invalid LPC image size")
    require(sum(struct.unpack_from("<8I", lpc)) & 0xffffffff == 0, "Invalid LPC vector checksum")
    stack, reset = struct.unpack_from("<II", lpc)
    require(stack == 0x10001000 and reset & 1 and 0x300 <= (reset & ~1) < len(lpc),
            "Invalid LPC reset vectors")
    require(lpc[0x2fc:0x300] == b"\xff" * 4, "Unexpected LPC code protection")

    binary = (PROJECT_ROOT / "build/nrf52840/keyboard.bin").read_bytes()
    uf2 = (PROJECT_ROOT / "build/nrf52840/keyboard_s140_7.3.0.uf2").read_bytes()
    count = (len(binary) + 255) // 256
    require(len(uf2) == count * 512, "Invalid UF2 length")
    recovered = bytearray()
    for index in range(count):
        block = uf2[index * 512:(index + 1) * 512]
        header = struct.unpack_from("<8I", block)
        require(header == (0x0a324655, 0x9e5d5157, 0x2000, 0x27000 + index * 256,
                           256, index, count, 0xada52840), "Invalid UF2 header/address/family")
        require(block[508:] == struct.pack("<I", 0x0ab16f30), "Invalid UF2 trailer")
        require(header[3] + 256 <= 0x88000, "UF2 overlaps reserved flash")
        recovered.extend(block[32:288])
    require(recovered[:len(binary)] == binary, "UF2 differs from application binary")
    require(all(value == 0xff for value in recovered[len(binary):]), "Unexpected UF2 padding")
    package = NrfPackage.load(PROJECT_ROOT / ".pio/build/supermini_ble/firmware.zip")
    require(package.image == binary, "DFU ZIP differs from the application binary")
    print("PASS: LPC vectors/CRP, UF2 addresses/family/content, DFU ZIP CRC/SoftDevice")


if __name__ == "__main__":
    main()
