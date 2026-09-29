# Hardware, wiring, and power

## Supported assembly

The development keyboard is a **Lenovo K800 / Lenovo Y Gaming Mechanical Switch Keyboard**, USB identity `17ef:6082`, with an **LPC11U13** and an 18-row × 8-column matrix. The controller has 24 KiB Flash, 4 KiB main SRAM, and an additional 2 KiB USB SRAM. This firmware uses the main SRAM.

The second controller is an **nRF52840 SuperMini** running a nice!nano-compatible Adafruit bootloader. Matching the chip name is not enough: verify GPIO numbering, the bootloader board ID, switched VCC polarity, and the regulator/charger circuit for your board revision.

## Signal connections

Disconnect all power while soldering. Use the keyboard PCB's labeled SWD pads and confirm their continuity to the LPC pins. The public project does not include a verified photograph of every PCB revision or a physical pad-order drawing.

| LPC pad / signal | LPC pin | nRF52840 pin | Connection |
|---|---|---|---|
| SWDIO | `PIO0_15` (LQFP48 pin 39) | `P0.08` | 1 kΩ series resistor |
| SWCLK | `PIO0_10` (LQFP48 pin 29) | `P0.11` | Direct signal wire |
| RESET | `PIO0_0/RESET` | `P0.17` | Open-drain drive; pull-up to the LPC supply |
| GND | GND | GND | Common ground |
| LPC supply | VDD, approximately 3.3 V | Switched VCC/3V3 output | Confirm this rail is controlled by `P0.13` |

```text
Keyboard / LPC11U13                    nRF52840 SuperMini

SWDIO / PIO0_15 --------[ 1 kΩ ]-------- P0.08  (RX / wake / SWDIO)
SWCLK / PIO0_10 ----------------------- P0.11  (TX / SWCLK)
RESET -------------------------------- P0.17  (open-drain)
GND ---------------------------------- GND
LPC 3.3 V supply ---------------------- switched VCC/3V3
                                        ^ enabled by P0.13 = HIGH
```

`P0.08` means port 0, pin 8 of the Nordic chip. It is not “pad 8,” and it is not the nRF's own SWDIO. The custom Arduino variant deliberately maps Arduino index 8 to Nordic P0.08. No extra wire to P0.06 is needed.

After reset, the LPC leaves its SWD function available for three seconds. It then uses SWDIO as software UART TX and SWCLK as software UART RX. During an LPC firmware update, the nRF stops UART, resets the target, and uses the same two wires as SWD.

Do not leave an ST-Link or another programmer driving these lines during normal operation. LPC RESET must be released before SWD access: holding RESET low selects boundary scan on this family. Remove a BOOT/ISP strap pulling `PIO0_1` low; otherwise the LPC starts its factory ISP ROM instead of the application.

## Power arrangement

**On the development assembly, the LPC supply wire goes to the SuperMini's switched VCC/3V3 rail. It does not draw power directly from GPIO P0.13.** The firmware drives P0.13 HIGH to enable that rail and LOW to disable it. This polarity is consistent with the [nice!nano v2 configuration in ZMK](https://github.com/zmkfirmware/zmk/blob/v0.3.0/app/boards/arm/nice_nano/nice_nano_v2.dts). Older nice!nano generations and other clones may differ.

Before connecting the rail:

1. Identify which keyboard supply net powers LPC VDD and what else shares it. A pad labeled “power” does not establish its voltage or direction.
2. Measure the SuperMini output in both enabled and disabled states; confirm approximately 3.3 V when enabled.
3. Check the regulator/load-switch ratings against the keyboard controller, LED drivers, and any backlight load connected to that rail. This project has not measured the complete assembly's current budget.
4. Isolate the original keyboard USB power path as required by your PCB so it cannot bypass the switch or feed the SuperMini output. Do not join independently driven supplies. The exact trace cut/component removal is PCB-specific and is not established by this repository.
5. Check ground continuity and shorts before applying power.

The LPC supply range is **1.8–3.6 V**. USB 5 V and a fully charged raw Li-ion cell must not be connected directly to VDD. See the [NXP LPC11U1x datasheet](https://www.nxp.com/docs/en/data-sheet/LPC11U1X.pdf).

A battery-powered installation needs a suitable battery connection, protection, charger, and regulator for the actual SuperMini revision. This project provides firmware power control, not a complete validated battery/charging design. Prototype USB operation does not establish safe charging behavior or battery runtime.

## Power states

| State | P0.13 / LPC supply | LPC | nRF |
|---|---|---|---|
| No BLE hosts connected | LOW / off | Unpowered | Advertising, eventually every 2 s |
| At least one host connected | HIGH / on | Scanning | BLE HID and update services |
| LPC update after COMMIT | HIGH / on until verification ends | SWD/IAP programming | Maintains update ownership |
| Manual sleep button | HIGH / retained | Power-down, button interrupt | System OFF, BLE disabled |

Manual sleep must retain LPC power: the wake button is still inside the original matrix. Pressing it wakes the LPC, which pulls the existing P0.08 signal low to wake the nRF. Details: [sleep.md](sleep.md).

## Backup and recovery

Before replacing LPC firmware, save its full `0x00000000–0x00005fff` Flash using a suitable SWD probe and its LPC11U13 target configuration. Read it twice, compare the files, and keep the original dump outside the public repository. No readout-protection bypass is required or implemented by this project.

The optional analysis tool accepts only the exact known factory revision, identified by SHA-256. A mismatch means the recovered offsets cannot be assumed valid; it does not mean your backup failed.

The new LPC firmware occupies Flash from zero and replaces the original USB behavior. Restoring the complete matching original dump through SWD restores the factory program. Keep physical access to RESET and SWD for recovery, and keep the nRF's USB/RESET accessible as well. See [installation and recovery](ble_updates.md).
