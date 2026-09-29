#!/usr/bin/env python3
"""Build/run host tests without hardware access or a factory firmware dump."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXTRA_SOURCES = {
    "test_ble_services": ["HidService.cpp", "LpcUpdateService.cpp"],
    "test_lpc_link": ["LpcLink.cpp"],
    "test_lpc_update": ["LpcProgrammer.cpp"],
    "test_swd": ["SwdPort.cpp"],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-sanitizers", action="store_true", help="For compilers without ASan/UBSan")
    args = parser.parse_args()
    output = ROOT / "build/tests"
    output.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-g",
             "-Iinclude", "-Ifirmware/nrf52840", "-Itests/fakes"]
    if not args.no_sanitizers:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    for source in sorted((ROOT / "tests").glob("test_*.cpp")):
        target = output / source.stem
        extra = [str(ROOT / "firmware/nrf52840" / name)
                 for name in EXTRA_SOURCES.get(source.stem, [])]
        print(f"Testing {source.name}", flush=True)
        subprocess.run(compiler + flags + [str(source)] + extra + ["-o", str(target)], cwd=ROOT, check=True)
        subprocess.run([str(target)], cwd=ROOT, check=True)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests",
                    "-p", "test_*.py", "-v"], cwd=ROOT, check=True)
    print("All host tests passed.")


if __name__ == "__main__":
    main()
