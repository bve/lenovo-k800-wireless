#pragma once
#include <bluefruit.h>
#include "LpcLink.hpp"
#include "ControlLink.hpp"
#include "bridge/FactoryKeyboard.hpp"
#include "bridge/HostProfiles.hpp"

// Read-only link/control health. No ordinary keys or macro contents are exposed.
class LinkStatusService : public BLEService {
public:
    LinkStatusService();
    err_t begin() override;
    void receivedByte() { ++bytes_; }
    void receivedFrame(uint32_t now) { ++frames_; lastFrame_ = now; }
    void poll(uint32_t now, bool linkAlive, const LpcLink& link);
    void publishControls(const bridge::FactoryKeyboard& keyboard, const ControlLink& control,
                         bool storageHealthy, uint8_t hostLeds);
    void publishProfiles(const bridge::HostProfiles& profiles, bridge::HidEndpoint routed, bool storageHealthy);
    void publishTiming(const bridge::HostProfiles& profiles, const LpcLink& link);
private:
    BLECharacteristic status_;
    BLECharacteristic controls_;
    BLECharacteristic profiles_;
    BLECharacteristic timing_;
    uint8_t previousControls_[20] = {};
    uint8_t previousProfiles_[6] = {};
    uint8_t previousTiming_[18] = {};
    uint32_t bytes_ = 0, frames_ = 0, lastFrame_ = 0, published_ = 0;
};
