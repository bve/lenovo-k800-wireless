"""Connect to a keyboard even when the OS HID driver already connected it."""
import asyncio
import sys
import time


async def request_disconnect(client, characteristic, command, timeout=10):
    """Accept a reboot's ATT error only if the device actually disconnects."""
    write_error = None
    try:
        await client.write_gatt_char(characteristic, command, response=True)
    except Exception as error:
        write_error = error
    deadline = time.monotonic() + timeout
    while client.is_connected and time.monotonic() < deadline:
        await asyncio.sleep(min(0.1, max(0, deadline - time.monotonic())))
    if client.is_connected:
        if write_error is not None:
            raise write_error
        raise RuntimeError("Device acknowledged the reset request but did not disconnect")
    await client.disconnect()


async def known_bluez_device(address):
    # A connected peripheral no longer advertises. BlueZ already has its object;
    # looking it up avoids Bleak's address-only implicit discovery timeout.
    # BLEDevice details below match the pinned Bleak 3.0.2 BlueZ backend.
    from bleak.backends.device import BLEDevice
    from dbus_fast import BusType, Message, MessageType
    from dbus_fast.aio import MessageBus
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    try:
        reply = await bus.call(Message(destination="org.bluez", path="/",
                                       interface="org.freedesktop.DBus.ObjectManager",
                                       member="GetManagedObjects"))
        if reply.message_type == MessageType.ERROR:
            raise RuntimeError(f"BlueZ device lookup: {reply.error_name}")
        matches = []
        for path, interfaces in reply.body[0].items():
            variants = interfaces.get("org.bluez.Device1", {})
            props = {key: value.value for key, value in variants.items()}
            if props.get("Address", "").lower() == address.lower():
                matches.append(BLEDevice(address, props.get("Name"), {"path": path, "props": props}))
        if len(matches) > 1:
            raise RuntimeError("Keyboard is known to multiple Bluetooth adapters; use one adapter")
        # Only bypass discovery for an already connected HID device. After DFU
        # or a BlueZ restart, a disconnected cached object can have stale
        # advertising/connection state; let Bleak rediscover it normally.
        if matches and matches[0].details["props"].get("Connected"):
            return matches[0]
        return address
    finally:
        bus.disconnect()


async def connect(address, pair=False, timeout=30):
    from bleak import BleakClient
    deadline = time.monotonic() + timeout
    for attempt in range(3):
        target = await known_bluez_device(address) if sys.platform == "linux" else address
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("BLE connection deadline exceeded")
        client = BleakClient(target, pair=pair, timeout=remaining)
        try:
            await client.connect()
            return client
        except BaseException as error:
            await client.disconnect()
            # BlueZ's automatic HID reconnect can replace the first connection
            # while Bleak waits for service discovery. Retry setup only; never
            # replay a firmware write, pairing denial, or a cancelled operation.
            transient = (isinstance(error, Exception) and sys.platform == "linux" and
                         str(error) == "failed to discover services, device disconnected")
            if not transient or attempt == 2:
                raise
            await asyncio.sleep(min(1, max(0, deadline - time.monotonic())))
