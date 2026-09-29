# Firmware architecture and timing

## Responsibilities

The original LPC11U13 remains the matrix/LED controller. Its replacement firmware is a standalone Cortex-M0 application at Flash address zero, built without the original Lenovo binary. The nRF52840 runs an Arduino/Bluefruit application with S140 7.3.0 and owns BLE HID, host routing, macros, persistent storage, power control, and firmware updates.

Shared logic under `include/bridge/` separates state machines from hardware transport so it can run in host tests. `Debouncer` owns stable/candidate matrix state; `KeyboardDebounceTiming` owns the adaptive timing policy. SWD transport, LPC programming, BLE update service, and PC upload logic are separate components.

## Matrix and link

The recovered matrix is 18 × 8. GPIO/key tables are checked in as `KeyboardLayout.hpp`; build scripts do not require a private original dump. Optional extraction tools verify the known SHA-256 before using revision-specific offsets.

LPC runs from its internal 12 MHz RC clock. Its main loop requests scans at a nominal 1 ms step and sleeps between work. A stable press requires 5 ms; ordinary-key release is [adaptive](debouncing.md). GPIO work, UART transmission, and incoming command interrupts can delay a scan, so 1 ms is not a hard worst-case guarantee.

The link is software UART, 115200 baud, 8N1. LPC CT32B1 runs at 12 MHz with 104 ticks per bit. A 26-byte matrix frame occupies about 2.3 ms of wire time; scanning pauses during transmission. Frames are sent on changes and as a 100 ms heartbeat. At least 4 ms between transfers leaves room for the nRF poll, reverse command, and receive guard. A contiguous 12-byte reverse command occupies the LPC receive interrupt for about 1 ms.

CT32B0 supplies the microsecond time base. Flash is configured for single-cycle reads at 12 MHz; the reset default's slower reads prevented reliable software UART reception at the higher baud rate during development. The receive interrupt has higher priority than SysTick. Both timers are stopped during manual Power-down.

The nRF polls received UART data every 1 ms while LPC is powered. It initially waits 3.5 seconds for 115200 through the LPC recovery window, then alternates 115200/38400 probes every 400 ms until it receives a valid CRC frame. After one second without valid frames it probes again. This permits updating one controller at a time across the earlier 38400 firmware generation.

For packet fields and LED commands, see [the inter-controller protocol](factory_controls.md#inter-controller-protocol).

## HID routing and latency

The active BLE profile requests a 7.5 ms interval; background connections request 30 ms. Peripheral latency is 4. The central chooses the actual parameters; requests use bounded retry rather than assuming acceptance. `status` exposes the interval and latency actually negotiated for each profile.

A 16-entry report queue preserves short transitions during BLE congestion. Notifications use nonblocking SoftDevice calls with four notification buffers per connection. A busy transport retains the report for retry. Queue overflow forces release and recovery to the latest state. Losing valid matrix frames for more than 350 ms discards stale queued input and releases keys.

Profile switching tracks partially transmitted release reports and waits until the old recipient is released before routing to the new host. Held physical keys and stale queued presses do not carry across that switch. See [profiles](bluetooth_profiles.md).

The keyboard report has six ordinary usages and eight modifiers; more than six ordinary keys produces HID ErrorRollOver. Media actions have a separate report. The implementation does not reproduce Lenovo's additional multi-key USB report.

## Storage, power, and updates

A background `KeyboardStore` worker writes immutable snapshots with CRC and atomic replacement. The main HID path does not wait synchronously for Flash writes. Sleep waits for pending storage and LPC acknowledgement before System OFF. [Power states](sleep.md) distinguish a disconnected, unpowered LPC from a manually sleeping, powered LPC.

The nRF includes a software SWD master and an LPC ROM-IAP programmer. BLE stages and validates the complete LPC image before erasing Flash. Programming verifies all 24 KiB, including erased space after the image. Exclusive ownership prevents overlapping LPC/nRF update and sleep operations.

nRF self-update hands the initiating bonded peer to the Adafruit legacy DFU bootloader. The application linker reserves its handover RAM and limits application Flash to the dual-bank region. See [update layout and recovery](ble_updates.md).

## Measurements and limits

The development Linux host accepted a 7.5 ms BLE interval. The 115200 UART link and 64 reverse LED commands with acknowledgements were checked. These observations do not measure switch-to-application latency. The adaptive LPC image is 5772 bytes in the development build; stack analysis reported 1152 bytes for `main`, 80 bytes for the debounce update, and 32 bytes for the UART handler, within the available main SRAM.

Exact image size can vary with compiler versions. Hardware tests must still establish complete matrix/media coverage, complex rollover behavior, simultaneous physical hosts, sleep current, and battery runtime.
