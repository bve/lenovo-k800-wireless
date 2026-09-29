#!/usr/bin/env python3
"""Reproduce the nice!nano bootloader used by this project; never flash it."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "c67f0bcf0fa8e841426335b1bbde91cda6ca1f50"
VERSION = "0.11.0-keyboard1"
PATCH = ROOT / "patches/bootloader/0001-zero-dfu-latency.patch"


def prepare_source(source):
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if actual != COMMIT:
        raise SystemExit("Bootloader source must use the pinned 0.11.0 commit")
    diff_command = ["git", "diff", "--no-color", "--binary", "--no-ext-diff", "HEAD"]
    diff = subprocess.check_output(diff_command, cwd=source)
    if not diff:
        subprocess.run(["git", "apply", "--check", str(PATCH)], cwd=source, check=True)
        subprocess.run(["git", "apply", str(PATCH)], cwd=source, check=True)
        diff = subprocess.check_output(diff_command, cwd=source)
    if diff != PATCH.read_bytes():
        raise SystemExit("Bootloader source has changes beyond the project's reviewed patch")
    subprocess.run(["git", "submodule", "update", "--init", "--depth", "1"], cwd=source, check=True)


def main():
    output = ROOT / "build/bootloader"
    source = output / "source"
    output.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--branch", "0.11.0",
                        "https://github.com/adafruit/Adafruit_nRF52_Bootloader.git", str(source)], check=True)
    prepare_source(source)
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio")))
    compiler = core / "packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-"
    nrfutil = core / "packages/tool-adafruit-nrfutil/adafruit-nrfutil.py"
    command = ["make", "-j4", "BOARD=nice_nano", "SD_VERSION=7.3.0", "DUALBANK_FW=1",
               "DEFAULT_TO_OTA_DFU=0", f"GIT_VERSION={VERSION}", f"PYTHON={shlex.quote(sys.executable)}",
               f"CROSS_COMPILE={compiler}",
               f"CC={compiler}gcc -fno-toplevel-reorder",
               f"NRFUTIL={shlex.quote(sys.executable)} {shlex.quote(str(nrfutil))}", "all"]
    # GCC otherwise may reverse the two .noinit objects, breaking Adafruit's
    # fixed bond-handover ABI (data at 0x20007f80, CRC16 immediately after it).
    subprocess.run(["make", "BOARD=nice_nano", "clean"], cwd=source, check=True)
    subprocess.run(command, cwd=source, check=True)
    symbols = subprocess.check_output([f"{compiler}nm", "-n", str(source /
        f"_build/build-nice_nano/nice_nano_bootloader-{VERSION}.out")], text=True)
    if "20007f80 b m_peer_data\n" not in symbols or "20007fbc b m_peer_data_crc\n" not in symbols:
        raise SystemExit("Bootloader bond-handover ABI is incompatible; do not flash")
    artifacts = {}
    for name in (f"update-nice_nano_bootloader-{VERSION}_nosd.uf2",
                 f"nice_nano_bootloader-{VERSION}_s140_7.3.0.hex",
                 f"nice_nano_bootloader-{VERSION}_s140_7.3.0.zip"):
        destination = output / name
        shutil.copyfile(source / "_build/build-nice_nano" / name, destination)
        artifacts[name] = hashlib.sha256(destination.read_bytes()).hexdigest()
    (output / "manifest.json").write_text(json.dumps({
        "repository": "https://github.com/adafruit/Adafruit_nRF52_Bootloader",
        "commit": COMMIT, "version": VERSION, "board": "nice_nano", "softdevice": "7.3.0",
        "patch_sha256": hashlib.sha256(PATCH.read_bytes()).hexdigest(),
        "dual_bank": True, "bootloader_start": "0xf4000",
        "compiler_flag": "-fno-toplevel-reorder", "peer_data_address": "0x20007f80",
        "peer_crc_address": "0x20007fbc", "sha256": artifacts,
    }, indent=2) + "\n")
    print("Bootloader artifacts prepared in build/bootloader; no device written")


if __name__ == "__main__":
    main()
