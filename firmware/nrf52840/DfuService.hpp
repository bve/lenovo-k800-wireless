#pragma once
#include <bluefruit.h>
#include "LpcUpdateService.hpp"
#include "HidService.hpp"

// The Adafruit service layout, with validation and mutual exclusion before its
// bootloader handover. Requires an encrypted, bonded PC connection.
class DfuService : public BLEDfu {
public:
    DfuService(LpcUpdateService& lpc, HidService& hid) : lpc_(lpc), hid_(hid) {}
    err_t begin() override;
private:
    static void authorize(uint16_t handle, BLECharacteristic* chr, ble_gatts_evt_write_t* request);
    LpcUpdateService& lpc_;
    HidService& hid_;
};
