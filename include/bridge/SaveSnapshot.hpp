#pragma once
#include "MacroCodec.hpp"
#include "HostProfiles.hpp"
#include <string.h>

namespace bridge {
// Immutable while the storage worker writes it; only the keyboard task touches
// FactoryKeyboard. A late save cannot acknowledge a newer, unsaved edit.
class SaveSnapshot {
public:
    bool capture(const FactoryKeyboard& keyboard, const HostProfiles& hosts) {
        slot_ = FactoryKeyboard::kSlots;
        for (unsigned i = 0; i < FactoryKeyboard::kSlots; ++i)
            if (keyboard.dirtyMacros() & (1u << i)) { slot_ = i; break; }
        if (hosts.dirty()) slot_ = FactoryKeyboard::kSlots + 1;
        else if (slot_ == FactoryKeyboard::kSlots && !keyboard.settingsDirty()) return false;
        size_ = encode(keyboard, hosts, data_);
        return size_ != 0;
    }
    void accept(FactoryKeyboard& keyboard, HostProfiles& hosts) const {
        uint8_t current[MacroCodec::kMaxSize];
        const size_t length = encode(keyboard, hosts, current);
        if (length != size_ || memcmp(current, data_, length)) return;
        if (profiles()) hosts.saved();
        else if (settings()) keyboard.settingsSaved();
        else keyboard.macroSaved(slot_);
    }
    bool settings() const { return slot_ == FactoryKeyboard::kSlots; }
    bool profiles() const { return slot_ == FactoryKeyboard::kSlots + 1; }
    unsigned slot() const { return slot_; }
    size_t size() const { return size_; }
    const uint8_t* data() const { return data_; }
private:
    size_t encode(const FactoryKeyboard& keyboard, const HostProfiles& hosts, uint8_t* output) const {
        if (profiles()) { hosts.encode(output); return HostProfiles::kEncodedSize; }
        if (!settings()) return MacroCodec::encode(slot_, keyboard.macro(slot_), output);
        MacroCodec::encodeSettings(keyboard.settings(), output);
        return 10;
    }
    uint8_t data_[MacroCodec::kMaxSize] = {};
    size_t size_ = 0;
    unsigned slot_ = FactoryKeyboard::kSlots;
};
}
