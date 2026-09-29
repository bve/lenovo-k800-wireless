# Third-party notices

The root MIT license covers original project code and documentation. It does not relicense upstream software or vendor firmware.

- `firmware/nrf52840/DfuService.cpp` adapts Adafruit's `BLEDfu.cpp`. Its complete **BSD-3-Clause** copyright and license notice is retained at the top of that file. Preserve it when redistributing source; include it with binary distributions as required by that notice.
- [Adafruit nRF52 Arduino core](https://github.com/adafruit/Adafruit_nRF52_Arduino) and its bundled libraries are downloaded by PlatformIO. They carry their own licenses. The pinned framework package is `framework-arduinoadafruitnrf52@1.10700.0`.
- [Adafruit nRF52 Bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader/tree/0.11.0), its submodules, and Nordic S140 are downloaded into ignored build directories. Their upstream license texts remain with those sources. The project's one-line bootloader patch does not replace those terms.
- [Nordic S140](https://www.nordicsemi.com/Products/Development-software/S140) is a vendor-supplied binary Bluetooth stack. It is not covered by this project's MIT license. Review the license supplied with the SDK before redistributing a combined image.
- Python development dependencies and PlatformIO toolchains retain their own package licenses.

This repository contains independently written replacement firmware and recovered pin/key mapping information. It does not distribute the original Lenovo firmware, Lenovo desktop application, vendor PDF manuals, decompiled vendor code, or full device Flash/bond-store dumps.

If distributing compiled artifacts, also distribute the notices required by every component linked or included in those artifacts. The initial public repository distributes source, tests, documentation, and the bootloader patch.
