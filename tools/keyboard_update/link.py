"""Passive keyboard UART health. No key contents or target reset commands."""
from dataclasses import dataclass
import struct

STATUS = "6c4b2001-5f0a-479b-9f31-5d67426590b7"
CONTROLS = "6c4b2002-5f0a-479b-9f31-5d67426590b7"
PROFILES = "6c4b2003-5f0a-479b-9f31-5d67426590b7"
TIMING = "6c4b2004-5f0a-479b-9f31-5d67426590b7"


@dataclass(frozen=True)
class TimingStatus:
    selected: int
    baud: int
    connections: tuple

    @classmethod
    def decode(cls, raw):
        if len(raw) != 18 or raw[0] != 1 or raw[1] > 2:
            raise ValueError("Invalid keyboard timing status")
        baud, *values = struct.unpack_from("<I6H", raw, 2)
        connections = tuple(zip(values[::2], values[1::2]))
        if baud not in (0, 38400, 115200) or any(
                (interval and not 6 <= interval <= 3200) or latency > 499 or
                (not interval and latency) for interval, latency in connections):
            raise ValueError("Invalid keyboard connection timing")
        return cls(raw[1], baud, connections)

    def __str__(self):
        peers = ", ".join(f"M{i+1}: {interval * 1.25:g} ms, latency={latency}"
                          if interval else f"M{i+1}: disconnected"
                          for i, (interval, latency) in enumerate(self.connections))
        return f"Timing: UART={self.baud} baud, selected=M{self.selected+1}; {peers}"


@dataclass(frozen=True)
class ProfileStatus:
    selected: int
    occupied: int
    connected: int
    routed: int
    flags: int

    @classmethod
    def decode(cls, raw):
        if (len(raw) != 6 or raw[0] != 1 or raw[1] > 2 or raw[2] & ~7 or
                raw[3] & ~raw[2] or raw[4] not in (0, 1, 2, 255) or raw[5] & ~3):
            raise ValueError("Invalid keyboard profile status")
        return cls(*raw[1:])

    def __str__(self):
        def names(mask):
            return ','.join(f'M{i+1}' for i in range(3) if mask & (1 << i)) or 'none'
        routed = f'M{self.routed+1}' if self.routed != 255 else 'none'
        return (f"Profiles: selected=M{self.selected+1}, assigned={names(self.occupied)}, "
                f"connected={names(self.connected)}, HID={routed}; "
                f"storage={'ok' if self.flags & 1 else 'error'}, pending={int(bool(self.flags & 2))}")


@dataclass(frozen=True)
class LinkStatus:
    flags: int
    uptime_ms: int
    uart_bytes: int
    valid_frames: int
    frame_age_ms: int
    power_offs: int = 0
    version: int = 1

    @classmethod
    def decode(cls, raw):
        if len(raw) != 20:
            raise ValueError("Invalid keyboard link status length")
        version, flags, reserved, uptime, count, frames, age = struct.unpack("<BBHIIII", raw)
        if (version not in (1, 2) or
                (version == 1 and (reserved or flags & ~15)) or
                (version == 2 and flags & ~127)):
            raise ValueError("Unsupported keyboard link status")
        return cls(flags, uptime, count, frames, age, reserved, version)

    def __str__(self):
        age = "never" if self.frame_age_ms == 0xffffffff else f"{self.frame_age_ms} ms"
        power = (f"; UART_enabled={int(bool(self.flags & 16))}, power_offs={self.power_offs}, "
                 f"last_advertising={'slow' if self.flags & 32 else 'fast'}, "
                 f"woke_from_system_off={int(bool(self.flags & 64))}") if self.version >= 2 else ""
        return (f"Link: {'alive' if self.flags & 1 else 'no valid frames'}; "
                f"UART bytes={self.uart_bytes}, frames={self.valid_frames}, last={age}; "
                f"power_enabled={int(bool(self.flags & 2))}, "
                f"RESET={int(bool(self.flags & 4))}, RX={int(bool(self.flags & 8))}; "
                f"uptime={self.uptime_ms} ms{power}")


@dataclass(frozen=True)
class ControlStatus:
    bank: int
    recording: int
    windows_locked: bool
    brightness_a: int
    brightness_b: int
    host_leds: int
    flags: int
    pressed_g: int
    pressed_m: int
    pressed_controls: int
    protocol: int
    sequence: int
    acknowledged: int
    lpc_flags: int
    record_led: int
    dirty_macros: int

    @classmethod
    def decode(cls, raw):
        if len(raw) != 20:
            raise ValueError("Invalid keyboard controls status length")
        fields = struct.unpack("<16BI", raw)
        if (fields[0] != 1 or fields[1] > 2 or fields[2] > 2 or fields[3] > 1 or
                fields[4] > 5 or fields[5] > 5 or fields[6] & ~7 or fields[7] & ~15 or
                fields[8] & ~63 or fields[9] & ~15 or fields[10] & ~7 or fields[11] > 2 or
                fields[14] & ~31 or fields[15] not in (0, 4, 7) or fields[16] & ~0x3ffff):
            raise ValueError("Unsupported keyboard controls status")
        return cls(*fields[1:])

    def __str__(self):
        state = ("idle", "choose G1–G6", "recording")[self.recording]
        acknowledged = bool(self.lpc_flags & 1) and self.sequence == self.acknowledged
        return (f"Controls: M{self.bank + 1}, MR={state}, WinLock={int(self.windows_locked)}, "
                f"brightness={self.brightness_a}/{self.brightness_b}, host_LEDs={self.host_leds:#x}; "
                f"storage={'ok' if self.flags & 1 else 'error'}, "
                f"pending_macros={self.dirty_macros:#x}, pending_settings={int(bool(self.flags & 8))}, "
                f"playing={int(bool(self.flags & 2))}, record_error={int(bool(self.flags & 4))}; "
                f"matrix_v{self.protocol}, command_ACK={int(acknowledged)} "
                f"({self.acknowledged}/{self.sequence}), "
                f"sleep_supported={int(bool(self.lpc_flags & 8))}, "
                f"all_LEDs_off={int(self.brightness_a == 5 or self.brightness_b == 5)}; "
                f"I2C_60={int(bool(self.lpc_flags & 2))}, I2C_61={int(bool(self.lpc_flags & 4))}")
