import asyncio
import binascii
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from keyboard_update import dfu, lpc
from keyboard_update.images import NrfPackage, validate_lpc
from keyboard_update.connection import request_disconnect


def lpc_image(size=0x304):
    data = bytearray(b"\xff" * size)
    struct.pack_into("<8I", data, 0, 0x10001000, 0x301, 0, 0, 0, 0, 0, (-0x10001301) & 0xffffffff)
    return bytes(data)


def nrf_package(size=512):
    image = struct.pack("<II", 0x20040000, 0x27009) + bytes(size - 8)
    init = struct.pack("<HHIHHH", 0x52, 0xffff, 0xffffffff, 1, 0x123, binascii.crc_hqx(image, 0xffff))
    return NrfPackage(image, init)


class LpcDevice:
    def __init__(self, reject_offset=False, disconnect=False):
        self.state, self.error, self.sequence = 0, 13, 255
        self.size = self.crc = self.detail = 0
        self.data = bytearray()
        self.reject_offset, self.disconnect = reject_offset, disconnect
        self.previous = None
        self.committed = False

    def status(self):
        return struct.pack("<BBBBIIII", 1, self.state, self.error, self.sequence,
                           len(self.data), self.size, self.crc, self.detail)

    async def read_gatt_char(self, characteristic):
        assert characteristic == lpc.STATUS
        if self.previous is not None:
            previous, self.previous = self.previous, None
            return previous  # Simulate ATT success before the application updates status.
        return self.status()

    async def write_gatt_char(self, characteristic, data, response):
        assert response
        self.previous = self.status()
        self.sequence = (self.sequence + 1) % 256
        self.error = 0
        if characteristic == lpc.DATA:
            offset = struct.unpack_from("<I", data)[0]
            if self.disconnect and offset >= 48:
                raise ConnectionError("radio lost")
            if offset != len(self.data) or self.reject_offset:
                self.error = 4
            else:
                self.data.extend(data[4:])
        elif data == b"\x03":
            self.state = 0
            self.size = self.crc = self.detail = 0
            self.data.clear()
        elif data[0] == 1:
            self.size, self.crc = struct.unpack_from("<II", data, 1)
            self.state = 1
        elif data == b"\x02":
            assert len(self.data) == self.size and binascii.crc32(self.data) == self.crc
            self.committed = True
            self.state = 4
        else:
            raise AssertionError(data)


class DfuDevice:
    def __init__(self, bad_receipt=False, reject_validate=False, revision=b"\x08\x00",
                 package=None, final_receipt=False):
        self.callback = None
        self.bad_receipt, self.reject_validate, self.revision = bad_receipt, reject_validate, revision
        self.step = "idle"
        self.data = bytearray()
        self.activated = False
        self.writes = []
        self.package = package or nrf_package()
        self.receipt_interval = self.packets = 0
        self.final_receipt = final_receipt

    async def read_gatt_char(self, uuid):
        assert uuid == dfu.REVISION
        return self.revision

    async def start_notify(self, uuid, callback):
        assert uuid == dfu.CONTROL
        self.callback = callback

    def notify(self, packet):
        self.callback(dfu.CONTROL, bytearray(packet))

    async def write_gatt_char(self, uuid, data, response):
        self.writes.append((uuid, bytes(data)))
        if uuid == dfu.CONTROL:
            assert response
            if data == b"\x01\x04":
                self.step = "size"
            elif data == b"\x02\x00":
                self.step = "init"
            elif data == b"\x02\x01":
                self.notify(b"\x10\x02\x01")
            elif data[0] == 8:
                self.receipt_interval, = struct.unpack("<H", data[1:])
                assert 1 <= self.receipt_interval <= 16
            elif data == b"\x03":
                self.step = "image"
            elif data == b"\x04":
                assert bytes(self.data) == self.package.image
                self.notify(b"\x10\x04\x05" if self.reject_validate else b"\x10\x04\x01")
            elif data == b"\x05":
                self.activated = True
            else:
                raise AssertionError(data)
        else:
            assert uuid == dfu.PACKET and not response and len(data) <= 20
            if self.step == "size":
                sd, bootloader, self.size = struct.unpack("<III", data)
                assert sd == bootloader == 0
                self.notify(b"\x10\x01\x01")
            elif self.step == "init":
                assert data == self.package.init
            elif self.step == "image":
                assert len(data) % 4 == 0
                self.data.extend(data)
                self.packets += 1
                if len(self.data) == self.size:
                    if self.final_receipt and self.packets % self.receipt_interval == 0:
                        self.notify(b"\x11" + struct.pack("<I", len(self.data)))
                    self.notify(b"\x10\x03\x01")
                elif self.packets % self.receipt_interval == 0:
                    self.notify(b"\x11" + struct.pack("<I", len(self.data) + self.bad_receipt))


class ImageTests(unittest.TestCase):
    def test_lpc_validation_and_full_flash_image(self):
        validate_lpc(lpc_image())
        validate_lpc(lpc_image(0x6000))
        for offset in (0, 4, 28, 0x2fc):
            image = bytearray(lpc_image())
            image[offset] ^= 1
            with self.assertRaises(ValueError):
                validate_lpc(image)
        for image in (b"", bytes(0x7000), lpc_image()[:-1]):
            with self.assertRaises(ValueError):
                validate_lpc(image)

    def test_nrf_rejects_bad_crc_and_incompatible_softdevice(self):
        package = nrf_package()
        package.validate()
        with self.assertRaises(ValueError):
            NrfPackage(package.image[:-1] + b"1", package.init).validate()
        for offset in (0, 8, 10):
            init = bytearray(package.init)
            init[offset] ^= 1
            with self.assertRaises(ValueError):
                NrfPackage(package.image, init).validate()


class ProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def test_reset_requires_actual_disconnect(self):
        class Device:
            is_connected = True

            def __init__(self, disconnect, error):
                self.drops, self.error = disconnect, error

            async def write_gatt_char(self, characteristic, command, response):
                self.is_connected = not self.drops
                if self.error:
                    raise ValueError("ATT rejected")

            async def disconnect(self):
                self.is_connected = False

        for error in (False, True):
            await request_disconnect(Device(True, error), dfu.CONTROL, b"\x06", timeout=0.01)
            with self.assertRaises(ValueError if error else RuntimeError):
                await request_disconnect(Device(False, error), dfu.CONTROL, b"\x06", timeout=0.01)

    async def test_lpc_stale_ack_and_sequence_wrap(self):
        device = LpcDevice()
        status = await lpc.LpcUpdater(device).upload(lpc_image())
        self.assertTrue(device.committed)
        self.assertEqual(bytes(device.data), lpc_image())
        self.assertEqual(status.state, 4)

    async def test_lpc_failed_transfer_never_commits(self):
        for device in (LpcDevice(reject_offset=True), LpcDevice(disconnect=True)):
            with self.assertRaises((RuntimeError, ConnectionError)):
                await lpc.LpcUpdater(device).upload(lpc_image())
            self.assertFalse(device.committed)

    async def test_lpc_busy_does_not_abort_programming(self):
        device = LpcDevice()
        device.state, device.error = 3, 0
        with self.assertRaises(RuntimeError):
            await lpc.LpcUpdater(device).upload(lpc_image())
        self.assertEqual(device.sequence, 255)

    async def test_dfu_full_exchange(self):
        # Short image, exact receipt boundary, partial burst, multiple bursts;
        # accept both bootloader variants with/without a final PRN before ACK.
        for size in (32, 160, 164, 320, 512):
            for final_receipt in (False, True):
                with self.subTest(size=size, final_receipt=final_receipt):
                    package = nrf_package(size)
                    device = DfuDevice(package=package, final_receipt=final_receipt)
                    await dfu.LegacyDfu(device).upload(package)
                    self.assertTrue(device.activated)
                    self.assertEqual(bytes(device.data), package.image)

    async def test_dfu_failure_never_activates(self):
        for device in (DfuDevice(bad_receipt=True), DfuDevice(reject_validate=True),
                       DfuDevice(revision=b"\x01\x00")):
            with self.assertRaises(RuntimeError):
                await dfu.LegacyDfu(device).upload(nrf_package())
            self.assertFalse(device.activated)

    async def test_dfu_invalid_image_never_writes(self):
        device = DfuDevice()
        with self.assertRaises(ValueError):
            await dfu.LegacyDfu(device).upload(NrfPackage(b"bad", b""))
        self.assertFalse(device.writes)

    async def test_dfu_notification_timeout_and_errors(self):
        updater = dfu.LegacyDfu(DfuDevice())
        with self.assertRaises(TimeoutError):
            await updater.response(opcode=1, timeout=0.01)
        for packet in (b"\x10\x01\x04", b"\x10\x02\x01", b"bad"):
            updater.notification(None, packet)
            with self.assertRaises(RuntimeError):
                await updater.response(opcode=1)


if __name__ == "__main__":
    unittest.main()
