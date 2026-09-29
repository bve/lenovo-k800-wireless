"""LPC GATT upload. COMMIT occurs only after the complete image is staged."""
import asyncio
import binascii
from dataclasses import dataclass
import struct
import time

from .images import validate_lpc

SERVICE = "6c4b1000-5f0a-479b-9f31-5d67426590b7"
CONTROL = "6c4b1001-5f0a-479b-9f31-5d67426590b7"
DATA = "6c4b1002-5f0a-479b-9f31-5d67426590b7"
STATUS = "6c4b1003-5f0a-479b-9f31-5d67426590b7"
ERRORS = ("OK", "command", "busy", "size", "offset", "incomplete", "CRC",
          "vectors", "CRP", "SWD connection", "wrong chip", "ROM IAP", "verify", "disconnected")
STATES = ("idle", "receiving", "pending", "programming", "done", "failed")


def swd_diagnostic(detail):
    phases = ("unknown", "DP ID", "DP setup", "debug power", "AP setup",
              "CPU halt", "halt wait", "memory access")
    reasons = ("unknown", "invalid ACK", "data parity", "WAIT retry limit",
               "wrong DP ID", "timeout", "sticky AP error")
    phase, reason, request, status = struct.pack(">I", detail)
    if not (0 < phase < len(phases) and 0 < reason < len(reasons)):
        return ""
    return (f"{phases[phase]}: {reasons[reason]}; "
            f"request=0x{request:02x}, ACK/status=0x{status:02x}")


@dataclass(frozen=True)
class Status:
    state: int
    error: int
    sequence: int
    received: int
    size: int
    crc: int
    detail: int

    @classmethod
    def decode(cls, raw):
        if len(raw) != 20:
            raise ValueError("Invalid LPC status length")
        version, state, error, sequence, received, size, crc, detail = struct.unpack("<BBBBIIII", raw)
        if version != 1 or state >= len(STATES) or error >= len(ERRORS):
            raise ValueError("Unsupported LPC update protocol")
        return cls(state, error, sequence, received, size, crc, detail)

    def check(self):
        if self.error:
            diagnostic = swd_diagnostic(self.detail) if self.error == 9 else ""
            suffix = f"; {diagnostic}" if diagnostic else ""
            raise RuntimeError(f"LPC: {ERRORS[self.error]} (detail 0x{self.detail:08x}{suffix})")
        if self.state == 5:
            raise RuntimeError("LPC programming failed")


class LpcUpdater:
    def __init__(self, client, progress=lambda phase, current, total: None):
        self.client = client
        self.progress = progress

    async def status(self):
        return Status.decode(await self.client.read_gatt_char(STATUS))

    async def request(self, characteristic, data, predicate, timeout=10):
        previous = await self.status()
        await self.client.write_gatt_char(characteristic, data, response=True)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = await self.status()
            if status.sequence != previous.sequence:
                status.check()
                if not predicate(status):
                    raise RuntimeError("Unexpected LPC state after acknowledged request")
                return status
            await asyncio.sleep(0.02)
        raise TimeoutError("LPC status did not acknowledge the request")

    async def upload(self, image: bytes):
        validate_lpc(image)
        old = await self.status()
        if old.state in (2, 3):
            raise RuntimeError("LPC is already programming; wait for completion")
        # Clear a previous diagnostic before starting a fresh transfer.
        await self.request(CONTROL, b"\x03", lambda s: s.state == 0)
        crc = binascii.crc32(image) & 0xffffffff
        await self.request(CONTROL, struct.pack("<BII", 1, len(image), crc),
                           lambda s: s.state == 1 and s.size == len(image) and s.crc == crc)
        for offset in range(0, len(image), 16):
            chunk = image[offset:offset + 16]
            await self.request(DATA, struct.pack("<I", offset) + chunk,
                               lambda s: s.state == 1 and s.received == offset + len(chunk))
            self.progress("Upload LPC", offset + len(chunk), len(image))
        await self.request(CONTROL, b"\x02", lambda s: s.state in (2, 3, 4))
        return await self.wait_complete(len(image), crc)

    async def wait_complete(self, size, crc, timeout=180):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = await self.status()
            status.check()
            if status.size != size or status.crc != crc:
                raise RuntimeError("LPC update identity changed; result cannot be confirmed")
            if status.state == 4:
                return status
            if status.state not in (2, 3):
                raise RuntimeError("LPC update was interrupted; upload the complete file again")
            self.progress("Program/verify LPC", min(status.detail, size), size)
            await asyncio.sleep(0.25)
        raise TimeoutError("LPC programming result is unknown; use the status command before retrying")
