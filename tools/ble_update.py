#!/usr/bin/env python3
"""Update the keyboard through the PC's native Bluetooth adapter."""
import argparse
import asyncio
from pathlib import Path
import sys

from keyboard_update import dfu, lpc, link
from keyboard_update.images import NrfPackage, validate_lpc
from keyboard_update.connection import connect, request_disconnect


class Progress:
    def __init__(self):
        self.previous = None

    def __call__(self, phase, current, total):
        step = int(current * 100 / total) // 5 * 5
        if (phase, step) != self.previous:
            print(f"{phase}: {step}%", flush=True)
            self.previous = phase, step


async def run(args):
    from bleak import BleakScanner
    if args.command == "scan":
        found = await BleakScanner.discover(timeout=8, return_adv=True)
        for device, advertisement in found.values():
            print(device.address, advertisement.local_name or device.name or "(unnamed)")
        return
    # Fail before opening a BLE connection if the image is unsuitable.
    package = NrfPackage.load(args.file) if args.command == "nrf" else None
    image = args.file.read_bytes() if args.command == "lpc" else None
    if image is not None:
        validate_lpc(image)
    client = await connect(args.address, pair=args.pair)
    try:
        if args.command == "status":
            status = await lpc.LpcUpdater(client).status()
            print(f"LPC: {lpc.STATES[status.state]}, {lpc.ERRORS[status.error]}; "
                  f"received={status.received}/{status.size}, CRC32={status.crc:08x}, detail={status.detail:#x}")
            if status.error == 9 and (diagnostic := lpc.swd_diagnostic(status.detail)):
                print(diagnostic)
            if client.services.get_characteristic(link.STATUS):
                print(link.LinkStatus.decode(bytes(await client.read_gatt_char(link.STATUS))))
            else:
                print("Link health service unavailable in this firmware or OS service cache.")
            if client.services.get_characteristic(link.CONTROLS):
                print(link.ControlStatus.decode(bytes(await client.read_gatt_char(link.CONTROLS))))
            if client.services.get_characteristic(link.PROFILES):
                print(link.ProfileStatus.decode(bytes(await client.read_gatt_char(link.PROFILES))))
            if client.services.get_characteristic(link.TIMING):
                print(link.TimingStatus.decode(bytes(await client.read_gatt_char(link.TIMING))))
            return
        if args.command == "lpc":
            try:
                await lpc.LpcUpdater(client, Progress()).upload(image)
            except Exception:
                print("If COMMIT was sent, the nRF continues programming even after a BLE disconnect. "
                      "Reconnect with the status command; do not assume the update failed.", file=sys.stderr)
                raise
            print("LPC Flash written and read back successfully.")
            return
        revision = bytes(await client.read_gatt_char(dfu.REVISION))
        if args.command == "reset-dfu":
            if revision != b"\x08\x00":
                raise RuntimeError("reset-dfu requires the BLE bootloader, not the application")
            await client.start_notify(dfu.CONTROL, lambda *_: None)
            await request_disconnect(client, dfu.CONTROL, b"\x06")
            print("DFU reset requested. Scan again: the recovery address may change.")
            return
        if revision == b"\x01\x00":
            await client.start_notify(dfu.CONTROL, lambda *_: None)
            await request_disconnect(client, dfu.CONTROL, b"\x01")
            print("nRF entered BLE DFU; reconnecting to the bonded bootloader…", flush=True)
            await asyncio.sleep(2)
            client = await connect(args.bootloader_address or args.address, timeout=40)
        elif revision != b"\x08\x00":
            raise RuntimeError(f"Unsupported DFU revision {revision.hex()}")
        updater = dfu.LegacyDfu(client, Progress())
        try:
            await updater.upload(package)
        except Exception:
            if not updater.activation_requested:
                raise
            print("Connection ended during activation; checking the application before reporting success.")
        print("nRF CRC validated and activation requested. Confirming application…", flush=True)
        await client.disconnect()
        await asyncio.sleep(3)
        client = await connect(args.application_address or args.address, timeout=40)
        # BlueZ 5.85 can still be attaching the HID profile after ServicesResolved.
        # Disconnecting immediately here raced that attachment and crashed the
        # daemon. Give the OS time to finish, then verify the live application.
        if sys.platform == "linux":
            await asyncio.sleep(5)
        if bytes(await client.read_gatt_char(dfu.REVISION)) != b"\x01\x00":
            raise RuntimeError("Updated application did not expose its DFU service")
        await lpc.LpcUpdater(client).status()
        print("nRF application reconnected; BLE update services are available.")
    finally:
        await client.disconnect()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("scan", help="List nearby Bluetooth LE devices (read-only)")
    for command in ("status", "lpc", "nrf", "reset-dfu"):
        sub = commands.add_parser(command)
        sub.add_argument("--address", required=True, help="BLE address/OS UUID of this keyboard")
        sub.add_argument("--pair", action="store_true", help="Request OS pairing before connecting")
        if command in ("lpc", "nrf"):
            sub.add_argument("file", type=Path, help="LPC .bin or nRF application DFU .zip")
        if command == "nrf":
            sub.add_argument("--bootloader-address", help="Override address after buttonless DFU entry")
            sub.add_argument("--application-address", help="Application address when starting in recovery DFU")
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        raise SystemExit("Stopped. After COMMIT/activation, inspect device status before retrying.")
    except Exception as error:
        parser.exit(1, f"Update error: {str(error) or type(error).__name__}\n")


if __name__ == "__main__":
    main()
