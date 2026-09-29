#pragma once
#include "FactoryKeyboard.hpp"

namespace bridge {
class MacroCodec {
public:
    static constexpr size_t kMaxSize = 10 + 4 * Macro::kMaxEvents;
    static size_t encode(unsigned slot, const Macro& macro, uint8_t* output) {
        if (slot >= FactoryKeyboard::kSlots || !macro.valid()) return 0;
        output[0] = 'M'; output[1] = 'A'; output[2] = 'C'; output[3] = 1;
        output[4] = slot; output[5] = 0;
        output[6] = macro.count; output[7] = macro.count >> 8;
        for (unsigned i = 0; i < macro.count; ++i) {
            const auto& event = macro.events[i];
            auto* p = output + 8 + 4 * i;
            p[0] = event.delayMs; p[1] = event.delayMs >> 8;
            p[2] = uint8_t(event.action); p[3] = event.value;
        }
        const size_t end = 8 + 4 * macro.count;
        const auto crc = crc16(output, end);
        output[end] = crc; output[end + 1] = crc >> 8;
        return end + 2;
    }
    static bool decode(unsigned slot, const uint8_t* input, size_t length, Macro& macro) {
        if (length < 10 || length > kMaxSize || input[0] != 'M' || input[1] != 'A' ||
            input[2] != 'C' || input[3] != 1 || input[4] != slot || input[5] ||
            slot >= FactoryKeyboard::kSlots) return false;
        const unsigned count = input[6] | unsigned(input[7]) << 8;
        if (count > Macro::kMaxEvents || length != 10 + 4 * count ||
            crc16(input, length - 2) != (input[length - 2] | unsigned(input[length - 1]) << 8)) return false;
        macro.count = count;
        for (unsigned i = 0; i < count; ++i) {
            const auto* p = input + 8 + 4 * i;
            macro.events[i] = {uint16_t(p[0] | unsigned(p[1]) << 8), MacroAction(p[2]), p[3]};
        }
        return macro.valid();
    }
    static void encodeSettings(const FactorySettings& s, uint8_t* output) {
        output[0] = 'K'; output[1] = 'C'; output[2] = 'F'; output[3] = 1;
        output[4] = s.bank; output[5] = s.brightnessA; output[6] = s.brightnessB;
        output[7] = 0; // Legacy Windows Lock byte; the key now requests sleep.
        const auto crc = crc16(output, 8);
        output[8] = crc; output[9] = crc >> 8;
    }
    static bool decodeSettings(const uint8_t* data, size_t length, FactorySettings& s) {
        if (length != 10 || data[0] != 'K' || data[1] != 'C' || data[2] != 'F' || data[3] != 1 ||
            data[7] > 1 || crc16(data, 8) != (data[8] | unsigned(data[9]) << 8)) return false;
        s = {data[4], data[5], data[6]}; // Accept old files without retaining Windows Lock.
        return s.valid();
    }
};
}
