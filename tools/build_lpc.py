#!/usr/bin/env python3
"""Build the standalone LPC firmware. No programmer is invoked."""
import argparse
import struct
import subprocess
from pathlib import Path

from inspect_firmware import PROJECT_ROOT
from toolchain import arm_tool


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, help="Directory containing arm-none-eabi-g++")
    args = parser.parse_args()
    try:
        compiler = arm_tool("g++", args.toolchain)
    except FileNotFoundError as error:
        parser.error(str(error))
    output = PROJECT_ROOT / "build/lpc11u13"
    output.mkdir(parents=True, exist_ok=True)
    source = PROJECT_ROOT / "firmware/lpc11u13"
    elf = output / "keyboard.elf"
    subprocess.run([
        compiler, "-mcpu=cortex-m0", "-mthumb", "-std=c++17", "-Os", "-g",
        "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fno-builtin",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
        "-nostdlib", "-I", str(PROJECT_ROOT / "include"),
        str(source / "startup.S"), str(source / "main.cpp"),
        "-T", str(source / "linker.ld"), "-Wl,--gc-sections",
        f"-Wl,-Map,{output / 'keyboard.map'}", "-Wl,--start-group", "-lc", "-lgcc",
        "-Wl,--end-group", "-o", str(elf),
    ], check=True)
    prefix = str(Path(compiler).parent / "arm-none-eabi-")
    binary = output / "keyboard.bin"
    subprocess.run([prefix + "objcopy", "-O", "binary", "--gap-fill", "0xff", str(elf), str(binary)], check=True)
    data = bytearray(binary.read_bytes())
    struct.pack_into("<I", data, 28, -sum(struct.unpack_from("<7I", data)) & 0xffffffff)
    if len(data) > 0x6000 or data[0x2FC:0x300] != b"\xff" * 4:
        raise RuntimeError("Invalid Flash layout or code-protection word")
    if sum(struct.unpack_from("<8I", data)) & 0xffffffff:
        raise RuntimeError("Invalid vector checksum")
    binary.write_bytes(data)
    # Keep the ELF's vector section consistent with the flashable BIN.
    vector_file = output / "vectors.bin"
    vector_file.write_bytes(data[:192])
    subprocess.run([prefix + "objcopy", "--update-section", f".vectors={vector_file}", str(elf)], check=True)
    vector_file.unlink()
    subprocess.run([prefix + "size", str(elf)], check=True)
    print(f"Built {binary.relative_to(PROJECT_ROOT)} ({len(data)} bytes); hardware not flashed")


if __name__ == "__main__":
    main()
