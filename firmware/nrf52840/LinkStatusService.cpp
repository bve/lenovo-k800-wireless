#include "LinkStatusService.hpp"
#include "SwdPort.hpp"
#include "bridge/LpcImage.hpp"

namespace {
const uint8_t kService[] = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x00,0x20,0x4b,0x6c};
const uint8_t kStatus[] = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x01,0x20,0x4b,0x6c};
const uint8_t kControls[] = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x02,0x20,0x4b,0x6c};
const uint8_t kProfiles[] = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x03,0x20,0x4b,0x6c};
const uint8_t kTiming[] = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x04,0x20,0x4b,0x6c};
}
LinkStatusService::LinkStatusService() : BLEService(kService), status_(kStatus), controls_(kControls), profiles_(kProfiles), timing_(kTiming) {}
err_t LinkStatusService::begin() {
    setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
    VERIFY_STATUS(BLEService::begin());
    status_.setProperties(CHR_PROPS_READ);
    status_.setFixedLen(20);
    VERIFY_STATUS(status_.begin());
    controls_.setProperties(CHR_PROPS_READ);
    controls_.setFixedLen(20);
    VERIFY_STATUS(controls_.begin());
    profiles_.setProperties(CHR_PROPS_READ);
    profiles_.setFixedLen(6);
    VERIFY_STATUS(profiles_.begin());
    timing_.setProperties(CHR_PROPS_READ);
    timing_.setFixedLen(sizeof(previousTiming_));
    VERIFY_STATUS(timing_.begin());
    published_ = millis() - 250;
    uint8_t initial[20] = {2};
    bridge::write32(initial + 16, UINT32_MAX);
    status_.write(initial, sizeof(initial));
    return ERROR_NONE;
}
void LinkStatusService::publishTiming(const bridge::HostProfiles& profiles, const LpcLink& link) {
    uint8_t data[18] = {1, profiles.selected()};
    bridge::write32(data + 2, link.baud());
    for (unsigned i = 0; i < bridge::HostProfiles::kCount; ++i) {
        const auto peer = profiles.endpoint(i);
        auto* conn = peer.valid() ? Bluefruit.Connection(peer.handle) : nullptr;
        if (!conn || !conn->connected()) continue;
        const uint16_t interval = conn->getConnectionInterval(), latency = conn->getSlaveLatency();
        data[6 + 4*i] = interval; data[7 + 4*i] = interval >> 8;
        data[8 + 4*i] = latency; data[9 + 4*i] = latency >> 8;
    }
    if (memcmp(data, previousTiming_, sizeof(data))) {
        timing_.write(data, sizeof(data));
        memcpy(previousTiming_, data, sizeof(data));
    }
}
void LinkStatusService::publishProfiles(const bridge::HostProfiles& profiles, bridge::HidEndpoint routed, bool storageHealthy) {
    uint8_t output = 0xff;
    if (routed.valid()) for (unsigned i = 0; i < bridge::HostProfiles::kCount; ++i)
        if (profiles.endpoint(i) == routed) output = i;
    const uint8_t data[] = {1, profiles.selected(), profiles.occupiedMask(), profiles.connectedMask(), output,
        uint8_t(uint8_t(storageHealthy) | uint8_t(profiles.dirty()) << 1)};
    if (memcmp(data, previousProfiles_, sizeof(data))) {
        profiles_.write(data, sizeof(data));
        memcpy(previousProfiles_, data, sizeof(data));
    }
}
void LinkStatusService::publishControls(const bridge::FactoryKeyboard& keyboard, const ControlLink& control,
                                       bool storageHealthy, uint8_t hostLeds) {
    const auto& settings = keyboard.settings();
    const auto buttons = keyboard.buttons();
    uint8_t data[20] = {
        1, settings.bank, uint8_t(keyboard.recording()), 0,
        settings.brightnessA, settings.brightnessB, hostLeds,
        uint8_t(uint8_t(storageHealthy) | uint8_t(keyboard.playing()) << 1 |
                uint8_t(keyboard.recordingError()) << 2 | uint8_t(keyboard.settingsDirty()) << 3),
        buttons.g, buttons.m, buttons.controls, control.version(),
        control.sequence(), control.acknowledged(), control.flags(), keyboard.recordLed(),
    };
    bridge::write32(data + 16, keyboard.dirtyMacros());
    if (memcmp(data, previousControls_, sizeof(data))) {
        controls_.write(data, sizeof(data));
        memcpy(previousControls_, data, sizeof(data));
    }
}
void LinkStatusService::poll(uint32_t now, bool linkAlive, const LpcLink& link) {
    if (now - published_ < 250) return;
    published_ = now;
    const bool power = NRF_P0->DIR & NRF_P0->OUT & (1u << PIN_EXT_POWER);
    const uint32_t pins = NRF_P0->IN;
    const uint8_t flags = uint8_t(linkAlive) | uint8_t(power) << 1 |
        uint8_t((pins >> SwdPort::kReset) & 1) << 2 |
        uint8_t((pins >> PIN_SERIAL1_RX) & 1) << 3 |
        uint8_t(link.uartRunning()) << 4 |
        uint8_t(Bluefruit.Advertising.getInterval() >= 3200) << 5 |
        uint8_t(bool(readResetReason() & POWER_RESETREAS_OFF_Msk)) << 6;
    const uint16_t offs = link.powerOffs();
    uint8_t data[20] = {2, flags, uint8_t(offs), uint8_t(offs >> 8)};
    bridge::write32(data + 4, now);
    bridge::write32(data + 8, bytes_);
    bridge::write32(data + 12, frames_);
    bridge::write32(data + 16, frames_ ? now - lastFrame_ : UINT32_MAX);
    status_.write(data, sizeof(data));
}
