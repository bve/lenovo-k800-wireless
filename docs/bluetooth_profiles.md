# Bluetooth host profiles

The nRF supports three simultaneous secured BLE connections. Ordinary keys, macros, and media input are sent only to the selected profile. Other hosts can stay connected.

## Selecting a host

| Action | Effect |
|---|---|
| Single M1 / M2 / M3 press | Select the macro bank |
| Double-tap M1 / M2 / M3 | Also select BLE profile 1 / 2 / 3 |
| Crossed-out Windows | Sleep the keyboard and disconnect all hosts |

A double-tap requires two complete press/release cycles of the same button, with at most 350 ms between releases. Each hold must also be no longer than 350 ms. A button held during startup, multiple M buttons together, and slow repeats do not switch the BLE profile.

Both taps still select the macro bank. The M1–M3 LEDs show the macro bank, which can differ from the BLE profile after a single press.

Switching sends key releases to the previous recipient, stops macro playback, and finishes recording in the old bank. Held keys are suppressed until released and pressed again; they are not carried to the new host. If BLE is temporarily unable to send a release, switching waits before enabling input on the new connection.

If the selected host is disconnected, input waits for that host. It does not fall back to another connected device. Num/Caps/Scroll indicators reflect the selected host; they are off when it is absent.

## Pairing and persistence

1. Pair **Lenovo BLE Keyboard** with the first host; its first successful secured connection assigns M1.
2. Pair a second host; it takes M2.
3. Pair a third host; it takes M3.
4. Double-tap the corresponding M button to route input.

Slots are assigned in the order of the first successful secured connections. Bond identity addresses, including identities behind rotating BLE addresses, keep the mapping stable across reconnects. Reconnect order does not reorder the profiles. The selected profile is restored after reset and wake.

`/keyboard/profiles` stores three identity addresses and the selected index, with a version and CRC. Encryption keys remain in Bluefruit's bond storage. A background storage worker writes immutable snapshots through temporary files and atomic rename. Manual sleep waits for pending saves.

A fourth device cannot replace an occupied slot. A button sequence for clearing or reassigning full profiles is not implemented. If a host forgets its pairing and establishes a new identity, recognition of the old slot is not guaranteed.

## Updates and power

Any connected, bonded host can start an update. An LPC upload belongs to the connection that started it: other hosts cannot append data, abort it, or cancel it by disconnecting. Once COMMIT is accepted, programming continues independently of BLE connectivity.

Entering nRF DFU releases keys and disconnects every host, handing the initiating peer to the bootloader. Manual sleep also disconnects all hosts.

Advertising continues with one or two connections to admit the remaining hosts and stops at three. The LPC power rail turns off only when no hosts remain, except during a committed update or manual sleep. Manual sleep retains power for matrix-button wake.

## Status and validation

Run `tools/ble_update.py status --address YOUR_KEYBOARD_ADDRESS`. The `Profiles` line shows selected, occupied, connected, and routed profiles plus storage state.

The encrypted, read-only characteristic `6c4b2003-5f0a-479b-9f31-5d67426590b7` has six bytes:

```text
version=1, selected_index, occupied_mask, connected_mask, routed_index, flags
selected_index: 0..2
routed_index: 0..2 or 255 for none
flags: bit0 storage healthy; bit1 unsaved changes
```

It exposes neither key contents nor host identity addresses. Host tests cover three connections, reconnect order, identity mapping, double-tap boundaries, timer wraparound, delayed release, stale queues, connection-handle reuse, per-connection HID reads/LEDs, and LPC update ownership.

On hardware, one assigned/connected/routed M1 host and persistence were confirmed. Three physical hosts at once and the physical double-tap sequence remain to be validated.
