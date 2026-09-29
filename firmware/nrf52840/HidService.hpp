#pragma once
#include <bluefruit.h>
#include "bridge/HostProfiles.hpp"
#include "bridge/KeyMapper.hpp"

// HID values, protocol mode and host LEDs belong to a connection, not the GATT table.
class HidService : public BLEService {
public:
    HidService();
    err_t begin() override;
    bridge::HidEndpoint endpoint(uint16_t handle) const;
    bool live(bridge::HidEndpoint peer) const;
    bool bootMode(bridge::HidEndpoint peer) const;
    uint8_t leds(bridge::HidEndpoint peer) const;
    void select(bridge::HidEndpoint peer);
    bool keyboard(bridge::HidEndpoint peer, const uint8_t* report);
    bool consumer(bridge::HidEndpoint peer, uint8_t report);
    bool release(bridge::HidEndpoint peer);
    bool releaseAll();
private:
    struct State {
        uint32_t generation = 0;
        uint8_t leds = 0;
        bool boot = false;
        uint8_t released = 0;
        bridge::Reports report;
    };
    void svc_connect_hdl(uint16_t handle) override;
    void svc_disconnect_hdl(uint16_t handle) override;
    static void write(uint16_t handle, BLECharacteristic* chr, uint8_t* data, uint16_t size);
    static void read(uint16_t handle, BLECharacteristic* chr, ble_gatts_evt_read_t* request);
    err_t input(BLECharacteristic& chr, unsigned size, uint8_t reportId = 0);
    err_t output(BLECharacteristic& chr, uint8_t reportId = 0);
    static bool notify(BLECharacteristic& chr, uint16_t handle, const void* data, uint16_t size);
    BLECharacteristic protocol_, keys_, consumer_, leds_, map_, bootKeys_, bootLeds_, info_, control_;
    State state_[BLE_MAX_CONNECTION];
    bridge::HidEndpoint selected_;
};
