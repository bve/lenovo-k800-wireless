# Adaptive debounce and key chatter

Mechanical switches can briefly open and close several times around a press or release. Unexpected repeated letters are usually called **key chatter**, **switch chatter**, or **double key presses**. Debouncing filters those transitions. [ZMK's debounce documentation](https://zmk.dev/docs/features/debouncing) uses the same terminology; this project's adaptive policy is its own implementation, not ZMK's algorithm.

## Current policy

The LPC applies debouncing before sending matrix states to the nRF. See [`KeyboardDebounce.hpp`](../include/bridge/KeyboardDebounce.hpp) and the generic [`Debouncer.hpp`](../include/bridge/Debouncer.hpp).

| Parameter | Value |
|---|---|
| Stable press confirmation | 5 ms |
| Initial release confirmation, ordinary keys | 40 ms |
| Adaptive release range | 5–60 ms per key |
| Margin after an observed short opening | 8 ms |
| Clean cycles before lowering protection | 16 |
| Reduction after a clean series | 5 ms, bounded at 5 ms |
| Modifier, media, and special-button press/release | 5/5 ms |

A short opening followed by reclosure resets the clean-cycle counter and raises the release window to at least `ceil(opening_ms) + 8`, capped at 60 ms. While a key is still logically held, openings up to 60 ms are considered. After a release has already been reported, only gaps up to 40 ms are treated as possible chatter.

A clean completed press/release cycle contributes to reducing protection. Idle time alone does not train the filter. Each key learns independently, with no special case for Q or V and no dependence on the host's language layout. Modifiers keep a short release delay to avoid carrying Shift into the following letter.

Learning is stored in LPC RAM. Reset, a firmware update, or loss of LPC power returns ordinary keys to 40 ms. Disconnecting all BLE hosts powers off the LPC, so that also restarts learning on reconnection.

## Tradeoffs

This is a bounded heuristic, not a reliable way to infer a person's intent from an electrical signal. A deliberate second tap with a release shorter than the current window can merge into one press. Conversely, a newly developed contact gap longer than a trained window can escape once before the filter learns a larger value. Gaps outside the observation limits may not be classified as chatter.

The 5 ms press threshold is not an end-to-end latency guarantee. Matrix scanning, UART transfers, BLE scheduling, and the host add time. A longer release threshold also means a key remains logically held longer after physical release.

## Evidence

Before the change, a Linux input capture of the reported Q switch contained release-to-press gaps of approximately 22.057 ms and 6.288 ms during a requested slow single-key test. These were separate up/down events rather than OS autorepeat events. They were host timestamps, not raw-contact measurements. The reported V issue did not reproduce in that capture.

Host tests cover synthetic 22/6 ms openings, separate taps with a 50 ms release, training to 5 ms, a new fault that strengthens protection, interruption of clean training, the 60 ms cap, independent keys, fast modifiers, and timer wraparound. The adaptive LPC image was installed and its Flash verified. A subsequent capture received no test presses, so the physical chatter fix after that installation remains unconfirmed.
