# BLE DFU bootloader compatibility patch

Base: [Adafruit_nRF52_Bootloader 0.11.0](https://github.com/adafruit/Adafruit_nRF52_Bootloader/tree/0.11.0), commit `c67f0bcf0fa8e841426335b1bbde91cda6ca1f50`. The project build identifies itself as `0.11.0-keyboard1`.

`0001-zero-dfu-latency.patch` changes only `SLAVE_LATENCY` from 4 to 0. On the development Linux/Intel BE200 connection, the initial parameters were interval 45 ms, latency 0, and supervision timeout 420 ms. The stock bootloader requested latency 4 without changing the timeout. That violates the requirement for timeout to exceed `2 × (1 + latency) × interval` (450 ms in that case).

S140 returned `NRF_ERROR_INVALID_PARAM` (7) from `sd_ble_gap_conn_param_update()`, causing `APP_ERROR_CHECK` to reset the board. A temporary diagnostic build identified `dfu_transport_ble.c`, line 802, as the failure site. Diagnostic instrumentation is not part of this patch.

Zero latency preserves the valid established timing and avoids skipping connection events during DFU. `tools/build_bootloader.py` pins the source, rejects unrelated changes, applies the patch, builds `nice_nano` with S140 7.3.0 and dual-bank support, and checks the bond-handover data/CRC addresses. GCC uses `-fno-toplevel-reorder` to preserve that ABI.

Bootloader sources and Nordic headers retain their upstream licenses. The source of the connection-parameter contract is Nordic's `ble_gap.h` supplied with the S140 SDK. Build output is placed under ignored `build/bootloader/`; the script does not flash a board.
