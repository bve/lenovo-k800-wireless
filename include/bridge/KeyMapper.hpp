#pragma once
#include "KeyboardLayout.hpp"
#include "Reports.hpp"

namespace bridge {
class KeyMapper {
public:
    Reports map(const uint8_t* rows, uint8_t defaultGKeys = 0x3f) const {
        Reports result;
        uint8_t count = 0;
        bool seen[256] = {};
        for (size_t index = 0; index < kRows * kColumns; ++index) {
            if (!(rows[index / kColumns] & (1u << (index % kColumns)))) continue;
            if (kKeys[index].kind == 3 && kKeys[index].value >= 0xb0 &&
                kKeys[index].value <= 0xb5 &&
                !(defaultGKeys & (1u << (kKeys[index].value - 0xb0)))) continue;
            // Unassigned G keys keep their factory default: 1..5 and F15.
            const Key key = index >= 122 && index < 128 ? kAlternate[index - 122] : kKeys[index];
            if (key.kind == 0x81) {
                result.keyboard[0] |= key.value;
            } else if (key.kind == 1 && key.value && !seen[key.value]) {
                seen[key.value] = true;
                if (count < 6) result.keyboard[2 + count] = key.value;
                ++count;
            } else if (key.kind == 2 && key.value >= 0xa0 && key.value <= 0xa6) {
                result.consumer |= kConsumerMasks[key.value - 0xa0];
            }
            // FactoryKeyboard handles the remaining vendor controls locally.
        }
        if (count > 6)
            for (size_t i = 2; i < 8; ++i) result.keyboard[i] = 1; // ErrorRollOver
        return result;
    }
};
}
