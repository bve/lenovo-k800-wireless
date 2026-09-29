"""Nordic legacy BLE DFU as implemented by Adafruit bootloader 0.11.0.

Protocol reference: Adafruit_nRF52_Bootloader/0.11.0/lib/sdk11/components/
libraries/bootloader_dfu/dfu_transport_ble.c and ble_services/ble_dfu/ble_dfu.c.
"""
import asyncio
import struct
import time

SERVICE = "00001530-1212-efde-1523-785feabcd123"
CONTROL = "00001531-1212-efde-1523-785feabcd123"
PACKET = "00001532-1212-efde-1523-785feabcd123"
REVISION = "00001534-1212-efde-1523-785feabcd123"
PACKET_SIZE = 20
RECEIPT_INTERVAL = 8


class LegacyDfu:
    def __init__(self, client, progress=lambda phase, current, total: None):
        self.client = client
        self.progress = progress
        self.events = asyncio.Queue()
        self.activation_requested = False

    def notification(self, _characteristic, value):
        self.events.put_nowait(bytes(value))

    async def response(self, opcode=None, receipt=None, timeout=30):
        deadline = time.monotonic() + timeout
        expected = f"command 0x{opcode:02x}" if opcode is not None else "notification"
        if receipt is not None:
            expected = f"receipt at {receipt} bytes"
        timeout_message = f"DFU timeout waiting for {expected}"
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(timeout_message)
            try:
                packet = await asyncio.wait_for(self.events.get(), remaining)
            except TimeoutError as error:
                raise TimeoutError(timeout_message) from error
            if len(packet) == 3 and packet[0] == 0x10:
                if packet[2] != 1:
                    raise RuntimeError(f"DFU command 0x{packet[1]:02x} rejected: status {packet[2]}")
                if packet[1] != opcode:
                    raise RuntimeError(f"Unexpected DFU response: {packet.hex()}")
                return
            if len(packet) == 5 and packet[0] == 0x11:
                count = struct.unpack_from("<I", packet, 1)[0]
                if receipt is not None:
                    if count != receipt:
                        raise RuntimeError(f"DFU receipt mismatch: {count} != {receipt}")
                    return
                continue  # Final PRN may precede RECEIVE completion.
            raise RuntimeError(f"Malformed DFU notification: {packet.hex()}")

    async def control(self, data):
        await self.client.write_gatt_char(CONTROL, data, response=True)

    async def upload(self, package):
        package.validate()
        revision = bytes(await self.client.read_gatt_char(REVISION))
        if revision != b"\x08\x00":
            raise RuntimeError(f"Expected DFU bootloader revision 0.8, got {revision.hex()}")
        await self.client.start_notify(CONTROL, self.notification)
        await self.control(b"\x01\x04")  # START, application only.
        await self.client.write_gatt_char(PACKET, struct.pack("<III", 0, 0, len(package.image)), response=False)
        await self.response(opcode=1)
        await self.control(b"\x02\x00")
        # 14 bytes for the strict S140 7.3.0 init packet accepted by images.py.
        await self.client.write_gatt_char(PACKET, package.init, response=False)
        await self.control(b"\x02\x01")
        await self.response(opcode=2)
        # Eight 20-byte packets fit comfortably in the bootloader's 16 x 600 B
        # receive pool. Bound each burst with an exact cumulative byte receipt.
        await self.control(b"\x08" + struct.pack("<H", RECEIPT_INTERVAL))
        await self.control(b"\x03")
        for number, offset in enumerate(range(0, len(package.image), PACKET_SIZE), 1):
            chunk = package.image[offset:offset + PACKET_SIZE]
            await self.client.write_gatt_char(PACKET, chunk, response=False)
            end = offset + len(chunk)
            if number % RECEIPT_INTERVAL == 0 and end < len(package.image):
                await self.response(receipt=end)
            self.progress("Upload nRF", end, len(package.image))
        await self.response(opcode=3)
        await self.control(b"\x04")
        await self.response(opcode=4)
        # Do not swallow a disconnect/write error here: activation is then
        # uncertain, and the CLI must confirm the application after reconnecting.
        self.activation_requested = True
        await self.control(b"\x05")
