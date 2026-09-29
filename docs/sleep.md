# Sleep, wake, and disconnected power saving

Press and release the **crossed-out Windows** button, then release any other held keys. The keyboard turns off its LEDs and Bluetooth. Press the same button again to wake; allow time for host reconnection and the LPC's three-second startup window.

The button remains in the original matrix. **P0.13 stays HIGH during manual sleep**, retaining the LPC supply. A fully unpowered LPC cannot detect this button. No additional button wire is needed.

## Manual sleep sequence

1. The nRF checks LPC sleep support and reserves exclusive access shared with firmware updates. Sleep requests are ignored while an update is receiving or programming.
2. Macro recording finishes, playback stops, and hosts receive key releases. Pending macros, settings, and profile snapshots are saved to Flash.
3. The LPC turns off both backlight groups and every indicator, stops the LED drivers' PWM oscillator, and waits until both I2C drivers acknowledge and all keys are released. Saved brightness is unchanged.
4. LPC stops UART, SysTick, CT32B0, and CT32B1. Row R16 / `PIO1_31` is held LOW; column C7 / `PIO1_25` is a pulled-up interrupt input. The remaining matrix is not scanned in Power-down.
5. After the acknowledgement, the nRF stops UART/BLE, disables USB peripherals and RAM retention, and enters **System OFF** with LOW-level wake on `P0.08`. P0.13 remains HIGH.
6. The button wakes LPC, which holds `PIO0_15` LOW. This existing wire wakes and resets nRF. Its startup reloads saved data, cycles LPC power, and resumes advertising.

The wake button held during startup does not immediately trigger another sleep. nRF RESET also returns to normal operation; USB VBUS can wake the nRF. An all-LEDs-off brightness setting remains off after waking.

Saving has a 30-second limit; LPC acknowledgement has a three-second limit. On timeout the nRF resets LPC and stays awake. Old LPC firmware without the sleep-support flag is not asked to enter Power-down. BLE update tasks are unavailable during System OFF: wake the keyboard first.

## No connected hosts

Ordinary disconnection uses a different state from manual sleep:

- With no hosts left, P0.13 goes LOW and LPC power turns off.
- nRF stops UART and disables output drivers, pull resistors, and input buffers on the LPC signal lines to prevent back-powering through GPIO.
- Advertising uses a 100 ms interval for the first five seconds, then a 2 s interval with no timeout.
- A connection enables the LPC rail, waits 50 ms before UART setup, and allows LPC to finish its recovery window.
- A committed LPC update retains power until full verification completes, even if BLE disconnects.

With one or two hosts connected, advertising remains available for more connections. A background host disconnecting does not stop input while another host remains.

## Validation

Two physical button sleep/wake cycles were confirmed on the development hardware. nRF's `RESETREAS.OFF` flag confirmed GPIO wake; UART and LPC command acknowledgement returned. Pairing and brightness survived those cycles. Earlier development included USB enumeration and pairing failures that required recovery; those failures were not attributed conclusively to the wake logic.

`status` reports `sleep_supported=1`; after GPIO wake it reports `woke_from_system_off=1`. The `all_LEDs_off` field refers to mode 5, not simply zero brightness in both groups.

Host tests cover complete press/release, a button held at startup, recording completion, storage/ACK waiting, mismatched ACK, old firmware, timeouts, and keeping P0.13 enabled through sleep-related disconnection. Actual sleep current and battery runtime have not been measured.

References: [NXP UM10462](https://www.keil.com/dd/docs/datashts/nxp/lpc11uxx/um10462.pdf), [Nordic GPIO](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/gpio.html).
