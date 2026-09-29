"""Validate images before connecting to any device."""
from dataclasses import dataclass
from pathlib import Path
import binascii
import json
import struct
import zipfile


def validate_lpc(data: bytes) -> None:
    if not 0x300 <= len(data) <= 0x6000 or len(data) % 4:
        raise ValueError("LPC image must be 0x300..0x6000 bytes, word aligned")
    words = struct.unpack_from("<8I", data)
    sp, reset = words[:2]
    if (sum(words) & 0xffffffff or not 0x10000000 < sp <= 0x10001000 or sp % 8
            or not reset & 1 or not 0x20 <= reset & ~1 < len(data)):
        raise ValueError("Invalid LPC vectors/checksum; expected an image linked at address 0")
    if struct.unpack_from("<I", data, 0x2fc)[0] not in (0xffffffff, 0x1a0888a1):
        raise ValueError("LPC CRP word is not in the unprotected whitelist")


@dataclass(frozen=True)
class NrfPackage:
    image: bytes
    init: bytes

    @classmethod
    def load(cls, path: Path):
        with zipfile.ZipFile(path) as archive:
            manifest = json.loads(archive.read("manifest.json"))["manifest"]
            if set(manifest) - {"application", "dfu_version"} or "application" not in manifest:
                raise ValueError("Only application DFU is supported; no SoftDevice/bootloader update")
            app = manifest["application"]
            # Read in memory, never extract archive paths into the filesystem.
            for name in (app["bin_file"], app["dat_file"]):
                if archive.getinfo(name).file_size > 0x61000:
                    raise ValueError("DFU archive member is too large")
            package = cls(archive.read(app["bin_file"]), archive.read(app["dat_file"]))
        package.validate()
        return package

    def validate(self) -> None:
        if not 8 <= len(self.image) <= 0x61000 or len(self.image) % 4:
            raise ValueError("Invalid nRF application size")
        sp, reset = struct.unpack_from("<II", self.image)
        if not (0x20006000 <= sp <= 0x20040000 and sp % 8 == 0 and reset & 1
                and 0x27000 <= (reset & ~1) < 0x27000 + len(self.image)):
            raise ValueError("nRF application must be linked for S140 7.x at 0x27000")
        if len(self.init) != 14:
            raise ValueError("Expected Adafruit legacy DFU init packet with one SoftDevice ID")
        device, _revision, _version, count, softdevice, crc = struct.unpack("<HHIHHH", self.init)
        if device != 0x52 or count != 1 or softdevice != 0x123:
            raise ValueError("DFU init packet must require S140 7.3.0 (0x0123)")
        if binascii.crc_hqx(self.image, 0xffff) != crc:
            raise ValueError("DFU application CRC16 does not match its init packet")
