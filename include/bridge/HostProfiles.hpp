#pragma once
#include "Protocol.hpp"
#include <string.h>

namespace bridge {
struct PeerIdentity {
    uint8_t type = 0xff;
    uint8_t address[6] = {};
    bool valid() const { return type <= 1; } // Public or static random identity, never an RPA.
    bool operator==(const PeerIdentity& other) const {
        return type == other.type && !memcmp(address, other.address, sizeof(address));
    }
};
struct HidEndpoint {
    uint16_t handle = 0xffff;
    uint32_t generation = 0;
    bool valid() const { return handle != 0xffff; }
    bool operator==(const HidEndpoint& other) const {
        return handle == other.handle && generation == other.generation;
    }
    bool operator!=(const HidEndpoint& other) const { return !(*this == other); }
};

// Stable identities live in flash; connection handles are valid for one session only.
class HostProfiles {
public:
    static constexpr unsigned kCount = 3;
    static constexpr size_t kEncodedSize = 28;
    int connect(const PeerIdentity& peer, HidEndpoint endpoint) {
        if (!peer.valid() || !endpoint.valid()) return -1;
        int free = -1;
        for (unsigned i = 0; i < kCount; ++i) {
            if (peers_[i] == peer) {
                if (links_[i].valid() && links_[i] != endpoint) return -1;
                links_[i] = endpoint; return int(i);
            }
            if (!peers_[i].valid() && free < 0) free = int(i);
        }
        if (free < 0) return -1;
        peers_[free] = peer; links_[free] = endpoint; dirty_ = true;
        return free;
    }
    void disconnect(HidEndpoint endpoint) {
        for (auto& link : links_) if (link == endpoint) link = {};
    }
    bool select(unsigned profile) {
        if (profile >= kCount || profile == selected_) return false;
        selected_ = profile; dirty_ = true; return true;
    }
    uint8_t selected() const { return selected_; }
    HidEndpoint target() const { return links_[selected_]; }
    HidEndpoint endpoint(unsigned i) const { return links_[i]; }
    uint8_t occupiedMask() const {
        uint8_t result = 0;
        for (unsigned i = 0; i < kCount; ++i) if (peers_[i].valid()) result |= 1u << i;
        return result;
    }
    uint8_t connectedMask() const {
        uint8_t result = 0;
        for (unsigned i = 0; i < kCount; ++i) if (links_[i].valid()) result |= 1u << i;
        return result;
    }
    bool dirty() const { return dirty_; }
    void saved() { dirty_ = false; }
    void encode(uint8_t* data) const {
        data[0] = 'B'; data[1] = 'T'; data[2] = 'P'; data[3] = 1; data[4] = selected_;
        for (unsigned i = 0; i < kCount; ++i) {
            data[5 + 7*i] = peers_[i].type;
            memcpy(data + 6 + 7*i, peers_[i].address, 6);
        }
        const auto crc = crc16(data, kEncodedSize - 2);
        data[26] = crc; data[27] = crc >> 8;
    }
    bool restore(const uint8_t* data, size_t size) {
        if (size != kEncodedSize || memcmp(data, "BTP\1", 4) || data[4] >= kCount ||
            crc16(data, 26) != (data[26] | unsigned(data[27]) << 8)) return false;
        HostProfiles restored;
        restored.selected_ = data[4];
        for (unsigned i = 0; i < kCount; ++i) {
            auto& peer = restored.peers_[i];
            peer.type = data[5 + 7*i]; memcpy(peer.address, data + 6 + 7*i, 6);
            if (!peer.valid() && peer.type != 0xff) return false;
            if (peer.valid()) for (unsigned j = 0; j < i; ++j)
                if (peer == restored.peers_[j]) return false;
        }
        *this = restored; return true;
    }
private:
    PeerIdentity peers_[kCount];
    HidEndpoint links_[kCount];
    uint8_t selected_ = 0;
    bool dirty_ = false;
};

// Two complete taps of the same M key, with at most 350 ms between releases.
// Held keys at startup, chords, slow taps and uint32_t timer wrap are handled.
class ProfileDoubleTap {
public:
    int update(uint8_t banks, uint32_t now) {
        banks &= 7;
        if (!primed_) { previous_ = banks; primed_ = true; return -1; }
        if (banks && banks != previous_) {
            if ((banks & (banks - 1)) || previous_) { pressed_ = 0; first_ = 0; }
            else { pressed_ = banks; pressedAt_ = now; }
        }
        int selected = -1;
        if (!banks && previous_) {
            if (pressed_ == previous_ && now - pressedAt_ <= 350) {
                if (first_ == pressed_ && now - releasedAt_ <= 350) {
                    selected = pressed_ == 1 ? 0 : pressed_ == 2 ? 1 : 2;
                    first_ = 0;
                } else { first_ = pressed_; releasedAt_ = now; }
            } else first_ = 0;
            pressed_ = 0;
        }
        previous_ = banks;
        return selected;
    }
private:
    uint32_t pressedAt_ = 0, releasedAt_ = 0;
    uint8_t previous_ = 0, pressed_ = 0, first_ = 0;
    bool primed_ = false;
};
}
