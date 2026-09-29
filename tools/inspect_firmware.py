#!/usr/bin/env python3
"""Read-only extraction of verified tables from the original Lenovo firmware.

Offsets are specific to the backup identified by SHA-256 below. This tool never
changes firmware and refuses a different image instead of applying stale offsets.
"""

import argparse
import csv
import hashlib
import json
import struct
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA256 = "ae89e2b3028a767ad9787610cf032206ca518004adc4202b9b3b9675a610648e"


class FirmwareImage:
    """Validated immutable image and its version-specific binary layout."""

    ROWS = 18
    COLUMNS = 8

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        if len(self.data) != 0x6000 or self.sha256 != EXPECTED_SHA256:
            raise ValueError("Image does not match the verified 24 KiB backup")

    def word(self, offset: int) -> int:
        return struct.unpack_from("<I", self.data, offset)[0]

    def vectors(self, offset: int) -> dict:
        return {
            "table": f"0x{offset:08x}",
            "initial_sp": f"0x{self.word(offset):08x}",
            "reset_thumb": f"0x{self.word(offset + 4):08x}",
            "first_eight_words_sum": f"0x{sum(struct.unpack_from('<8I', self.data, offset)) & 0xffffffff:08x}",
        }

    def usb_string(self, offset: int) -> str:
        length, descriptor_type = self.data[offset : offset + 2]
        if descriptor_type != 3 or length < 2 or length % 2:
            raise ValueError(f"Invalid USB string descriptor at {offset:#x}")
        return self.data[offset + 2 : offset + length].decode("utf-16-le")

    def usb_device(self, offset: int) -> dict:
        vendor, product, revision = struct.unpack_from("<HHH", self.data, offset + 8)
        return {
            "offset": f"0x{offset:04x}",
            "vid": f"0x{vendor:04x}",
            "pid": f"0x{product:04x}",
            "bcd_device": f"0x{revision:04x}",
        }

    def pin_table(self, offset: int, count: int) -> list[str]:
        return [
            f"PIO{self.data[offset + count + index]}_{self.data[offset + index]}"
            for index in range(count)
        ]

    def matrix(self) -> list[dict]:
        rows = self.pin_table(0x55CE, self.ROWS)
        columns = self.pin_table(0x5492, self.COLUMNS)
        kinds = {0x01: "keyboard_usage", 0x81: "modifier_mask", 0x02: "consumer_selector",
                 0x03: "vendor_or_special", 0x7F: "unused"}
        result = []
        for row, row_pin in enumerate(rows):
            for column, column_pin in enumerate(columns):
                offset = 0x54A2 + 2 * (row * self.COLUMNS + column)
                key_class, key_value = self.data[offset : offset + 2]
                result.append({
                    "row": row,
                    "column": column,
                    "row_pin": row_pin,
                    "column_pin": column_pin,
                    "table_offset": f"0x{offset:04x}",
                    "class": f"0x{key_class:02x}",
                    "value": f"0x{key_value:02x}",
                    "interpretation": kinds.get(key_class, "unknown"),
                })
        return result

    def factory_controls(self) -> dict:
        """Extract controls using the protocol recovered in factory_controls.md.

        Names are established by Lenovo's manual and HID.exe v1.15, not by
        text labels in the LPC image. GPIOs, masks and fallback usages come
        directly from the hash-verified image. No physical key test is implied.
        """
        groups = (
            (0xB0, tuple(f"G{i}" for i in range(1, 7)), 0x5485, 2),
            (0xB6, ("MR", "M1", "M2", "M3"), 0x5481, 3),
            (0xC0, ("Windows lock", "Backlight C1", "Backlight C2"), 0x547E, 1),
        )
        matrix = self.matrix()
        controls = []
        for first_code, names, mask_table, report_byte in groups:
            for number, name in enumerate(names):
                code = first_code + number
                entry = next(row for row in matrix
                             if row["class"] == "0x03" and int(row["value"], 16) == code)
                index = entry["row"] * self.COLUMNS + entry["column"]
                fallback = None
                if 122 <= index < 128:
                    offset = 0x55C2 + 2 * (index - 122)
                    fallback = {
                        "table_offset": f"0x{offset:04x}",
                        "class": f"0x{self.data[offset]:02x}",
                        "usage": f"0x{self.data[offset + 1]:02x}",
                    }
                controls.append({
                    "name": name,
                    "matrix_index": index,
                    "row": entry["row"],
                    "column": entry["column"],
                    "row_pin": entry["row_pin"],
                    "column_pin": entry["column_pin"],
                    "table_offset": entry["table_offset"],
                    "factory_code": entry["value"],
                    "report_byte_including_id": report_byte,
                    "mask_offset": f"0x{mask_table + number:04x}",
                    "mask": f"0x{self.data[mask_table + number]:02x}",
                    "fallback_when_host_mode_is_zero": fallback,
                })
        return {
            "sha256": self.sha256,
            "evidence": "analysis/factory_controls.md",
            "physical_key_validation": "Not performed for these controls",
            "host_mode_flag": "0x10000239",
            "input_report": {
                "usage_page": "0xff00", "report_id": 3, "length_with_id": 4,
                "endpoint": "0x82", "buffer": "0x100002bd", "handler": "0x3bc0",
            },
            "controls": controls,
        }

    def summary(self) -> dict:
        erased_start = len(self.data.rstrip(b"\xff"))
        row_pins = self.pin_table(0x55CE, self.ROWS)
        column_pins = self.pin_table(0x5492, self.COLUMNS)
        return {
            "sha256": self.sha256,
            "size_bytes": len(self.data),
            "boot_vectors": self.vectors(0),
            "application_vectors": self.vectors(0x2000),
            "crp_word": f"0x{self.word(0x2fc):08x}",
            "boot_usb": self.usb_device(0x1B00),
            "application_usb": self.usb_device(0x5380),
            "manufacturer": self.usb_string(0x53D2),
            "product": self.usb_string(0x53E0),
            "trailing_ff": {
                "start": f"0x{erased_start:04x}",
                "length": len(self.data) - erased_start,
                "safe_patch_space_verified": False,
                "reason": "Function 0x2eb8 erases sector 5 and writes 256 bytes at 0x5f00",
            },
            "matrix_rows": row_pins,
            "matrix_columns": column_pins,
            "swd_pins_in_matrix": sorted({"PIO0_10", "PIO0_15"} & set(row_pins + column_pins)),
            "alternate_key_table": {
                "offset": "0x55c2",
                "first_matrix_index": 122,
                "entries": [self.data[offset : offset + 2].hex(" ")
                            for offset in range(0x55C2, 0x55CE, 2)],
                "selection": "Firmware condition in 0x4086 / RAM byte 0x10000239 equals zero",
            },
            "hardware_validation": "Not performed; extracted from static code and data only",
        }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="Your own original LPC Flash dump")
    parser.add_argument("--output-dir", type=Path,
                        help="Save summary.json, matrix.csv and factory_controls.json")
    args = parser.parse_args()
    try:
        firmware = FirmwareImage(args.image)
        summary = json.dumps(firmware.summary(), indent=2, ensure_ascii=False) + "\n"
        if args.output_dir is None:
            print(summary, end="")
        else:
            args.output_dir.mkdir(parents=True, exist_ok=True)
            (args.output_dir / "summary.json").write_text(summary, encoding="utf-8")
            controls_path = args.output_dir / "factory_controls.json"
            controls_path.write_text(
                json.dumps(firmware.factory_controls(), indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
            matrix = firmware.matrix()
            with (args.output_dir / "matrix.csv").open("w", newline="", encoding="utf-8") as output:
                writer = csv.DictWriter(output, fieldnames=list(matrix[0]), lineterminator="\n")
                writer.writeheader()
                writer.writerows(matrix)
            print(f"Saved summary.json, matrix.csv and factory_controls.json in {args.output_dir}")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
