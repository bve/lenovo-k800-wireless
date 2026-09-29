from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from keyboard_update.link import LinkStatus, ControlStatus, ProfileStatus, TimingStatus


class LinkStatusTests(unittest.TestCase):
    def test_negotiated_timing_and_missing_peers(self):
        raw = struct.pack('<BBI6H', 1, 0, 115200, 6, 4, 24, 4, 0, 0)
        status = TimingStatus.decode(raw)
        self.assertIn('M1: 7.5 ms', str(status))
        self.assertIn('M3: disconnected', str(status))
        for index, value in ((0, 2), (1, 3), (6, 5), (14, 1), (16, 1)):
            damaged = bytearray(raw)
            damaged[index] = value
            with self.assertRaises(ValueError):
                TimingStatus.decode(damaged)

    def test_profiles_wait_for_selected_disconnected_host(self):
        status = ProfileStatus.decode(bytes([1, 1, 7, 5, 255, 1]))
        self.assertIn('selected=M2', str(status))
        self.assertIn('connected=M1,M3', str(status))
        self.assertIn('HID=none', str(status))
        for raw in ([1, 3, 7, 7, 0, 1], [1, 0, 1, 3, 0, 1], [1, 0, 7, 7, 3, 1],
                    [2, 0, 7, 7, 0, 1], [1, 0, 7, 7, 0, 4], [1]):
            with self.assertRaises(ValueError):
                ProfileStatus.decode(bytes(raw))

    def test_legacy_and_power_status(self):
        old = LinkStatus.decode(struct.pack("<BBHIIII", 1, 15, 0, 5000, 240, 10, 7))
        self.assertEqual(old.valid_frames, 10)
        self.assertEqual(old.power_offs, 0)
        new = LinkStatus.decode(struct.pack("<BBHIIII", 2, 63, 12, 6000, 480, 20, 3))
        self.assertEqual(new.power_offs, 12)
        self.assertIn("UART_enabled=1", str(new))
        self.assertIn("last_advertising=slow", str(new))
        woke = LinkStatus.decode(struct.pack("<BBHIIII", 2, 64, 0, 10, 0, 0, 0))
        self.assertIn("woke_from_system_off=1", str(woke))

    def test_invalid_status(self):
        for version, flags, count in ((1, 16, 0), (1, 0, 1), (2, 128, 0), (3, 0, 0)):
            with self.assertRaises(ValueError):
                LinkStatus.decode(struct.pack("<BBHIIII", version, flags, count, 0, 0, 0, 0))

    def test_controls_ack_and_pending_storage(self):
        raw = bytearray([1, 2, 2, 1, 4, 0, 7, 9, 63, 15, 7, 2, 8, 8, 7, 7])
        raw += struct.pack("<I", 1 << 17)
        result = ControlStatus.decode(raw)
        self.assertEqual(result.bank, 2)
        self.assertEqual(result.dirty_macros, 1 << 17)
        self.assertIn("command_ACK=1", str(result))
        self.assertIn("pending_settings=1", str(result))
        for index, invalid in ((0, 2), (1, 3), (2, 3), (3, 2), (4, 6), (5, 6),
                               (6, 8), (7, 16), (8, 64), (9, 16), (10, 8),
                               (11, 3), (14, 32), (15, 6), (19, 1)):
            changed = raw.copy()
            changed[index] = invalid
            with self.subTest(index=index), self.assertRaises(ValueError):
                ControlStatus.decode(changed)
        raw[4] = 5
        raw[14] = 31
        self.assertIn("all_LEDs_off=1", str(ControlStatus.decode(raw)))
        self.assertIn("sleep_supported=1", str(ControlStatus.decode(raw)))
        raw[13] = 7
        self.assertIn("command_ACK=0", str(ControlStatus.decode(raw)))
        raw[13] = 8
        raw[14] = 6
        self.assertIn("command_ACK=0", str(ControlStatus.decode(raw)))
        with self.assertRaises(ValueError):
            ControlStatus.decode(raw[:-1])
