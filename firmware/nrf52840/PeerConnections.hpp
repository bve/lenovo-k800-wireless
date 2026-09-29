#pragma once
#include "HidService.hpp"

// Bond lookup runs in Bluefruit's deferred callback task, never the scan loop.
// Only public identity addresses cross this mailbox; encryption keys stay in Bluefruit.
class PeerConnections {
public:
    explicit PeerConnections(HidService& hid) : hid_(hid) {}
    void secured(uint16_t handle) {
        const auto endpoint = hid_.endpoint(handle);
        if (!hid_.live(endpoint)) return;
        auto* conn = Bluefruit.Connection(handle);
        bond_keys_t keys;
        if (!conn || !conn->loadBondKey(&keys) || !hid_.live(endpoint)) return;
        bridge::PeerIdentity identity;
        identity.type = keys.peer_id.id_addr_info.addr_type;
        memcpy(identity.address, keys.peer_id.id_addr_info.addr, 6);
        taskENTER_CRITICAL();
        pending_[handle] = {identity, endpoint, true};
        taskEXIT_CRITICAL();
    }
    void poll(bridge::HostProfiles& profiles) {
        for (unsigned i = 0; i < bridge::HostProfiles::kCount; ++i) {
            const auto endpoint = profiles.endpoint(i);
            if (endpoint.valid() && !hid_.live(endpoint)) profiles.disconnect(endpoint);
        }
        for (unsigned handle = 0; handle < BLE_MAX_CONNECTION; ++handle) {
            taskENTER_CRITICAL();
            const auto item = pending_[handle]; pending_[handle].ready = false;
            taskEXIT_CRITICAL();
            if (item.ready && hid_.live(item.endpoint) && profiles.connect(item.identity, item.endpoint) < 0)
                Bluefruit.disconnect(handle);
        }
    }
private:
    struct Pending {
        bridge::PeerIdentity identity;
        bridge::HidEndpoint endpoint;
        bool ready = false;
    };
    HidService& hid_;
    Pending pending_[BLE_MAX_CONNECTION];
};
