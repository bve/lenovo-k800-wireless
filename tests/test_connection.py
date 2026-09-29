from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import AsyncMock, Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from keyboard_update.connection import connect


class ConnectionTests(unittest.IsolatedAsyncioTestCase):
    async def test_hid_discovery_disconnect_retries_setup(self):
        first = SimpleNamespace(connect=AsyncMock(side_effect=RuntimeError(
            "failed to discover services, device disconnected")), disconnect=AsyncMock())
        second = SimpleNamespace(connect=AsyncMock(), disconnect=AsyncMock())
        factory = Mock(side_effect=[first, second])
        with patch.dict(sys.modules, {"bleak": SimpleNamespace(BleakClient=factory)}), \
                patch("keyboard_update.connection.sys.platform", "linux"), \
                patch("keyboard_update.connection.known_bluez_device", AsyncMock(return_value="device")), \
                patch("keyboard_update.connection.asyncio.sleep", AsyncMock()):
            self.assertIs(await connect("device"), second)
        first.disconnect.assert_awaited_once()
        second.disconnect.assert_not_awaited()
        self.assertEqual(factory.call_count, 2)

    async def test_other_connection_errors_are_not_retried(self):
        client = SimpleNamespace(connect=AsyncMock(side_effect=RuntimeError("Authentication rejected")),
                                 disconnect=AsyncMock())
        factory = Mock(return_value=client)
        with patch.dict(sys.modules, {"bleak": SimpleNamespace(BleakClient=factory)}), \
                patch("keyboard_update.connection.sys.platform", "linux"), \
                patch("keyboard_update.connection.known_bluez_device", AsyncMock(return_value="device")):
            with self.assertRaisesRegex(RuntimeError, "Authentication rejected"):
                await connect("device")
        self.assertEqual(factory.call_count, 1)
        client.disconnect.assert_awaited_once()
