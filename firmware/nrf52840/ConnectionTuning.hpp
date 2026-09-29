#pragma once
#include "HidService.hpp"

// Request once per connection/profile change. A central may choose other
// parameters; do not fight it with repeated requests after acceptance.
class ConnectionTuning {
public:
    void poll(uint32_t now, const bridge::HostProfiles& profiles, const HidService& hid) {
        for (unsigned i = 0; i < bridge::HostProfiles::kCount; ++i) {
            auto& state = states_[i];
            const auto peer = profiles.endpoint(i);
            const bool active = i == profiles.selected();
            if (state.peer != peer || state.active != active) {
                state = {peer, now, active, 0, true};
            }
            if (!state.pending || !hid.live(peer) || now - state.requestedAt < 1000) continue;
            auto* conn = Bluefruit.Connection(peer.handle);
            const uint16_t interval = active ? 6 : 24; // 7.5 / 30 ms.
            state.requestedAt = now;
            ++state.attempts;
            if ((conn->getConnectionInterval() == interval && conn->getSlaveLatency() == 4) ||
                conn->requestConnectionParameter(interval, 4, 400) || state.attempts >= 3)
                state.pending = false;
        }
    }
private:
    struct State {
        bridge::HidEndpoint peer;
        uint32_t requestedAt = 0;
        bool active = false;
        uint8_t attempts = 0;
        bool pending = false;
    } states_[bridge::HostProfiles::kCount];
};
