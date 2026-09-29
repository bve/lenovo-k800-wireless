#pragma once
#include "Protocol.hpp"

namespace bridge {
struct LightingState {
    uint8_t bank = 0, recordLed = 0, brightnessA = 4, brightnessB = 4, hostLeds = 0;
    bool windowsLocked = false;
    bool sleep = false;
    bool dark() const { return sleep || brightnessA == 5 || brightnessB == 5; }
    uint8_t visibleHostLeds() const { return dark() ? 0 : hostLeds; }
    bool valid() const {
        return bank < 3 && (recordLed == 0 || recordLed == 4 || recordLed == 7) &&
               brightnessA <= 5 && brightnessB <= 5 && hostLeds < 8;
    }
};
inline bool equal(const LightingState& a, const LightingState& b) {
    return a.bank == b.bank && a.recordLed == b.recordLed && a.brightnessA == b.brightnessA &&
           a.brightnessB == b.brightnessB && a.hostLeds == b.hostLeds &&
           a.windowsLocked == b.windowsLocked && a.sleep == b.sleep;
}
constexpr unsigned kControlFrameSize = 12;
inline void encodeControl(const LightingState& state, uint8_t sequence, uint8_t* data) {
    data[0] = 'L'; data[1] = 'C'; data[2] = 1; data[3] = sequence;
    data[4] = state.bank; data[5] = state.recordLed;
    data[6] = state.brightnessA; data[7] = state.brightnessB;
    data[8] = uint8_t(state.windowsLocked) | (state.hostLeds << 1); data[9] = state.sleep;
    const uint16_t crc = crc16(data, 10);
    data[10] = crc; data[11] = crc >> 8;
}
class ControlParser {
public:
    bool push(uint8_t byte, uint32_t nowUs, LightingState& state, uint8_t& sequence) {
        if (used_ && nowUs - last_ > 10000) used_ = 0;
        last_ = nowUs;
        data_[used_++] = byte;
        if (used_ < kControlFrameSize) return false;
        const LightingState next = {data_[4], data_[5], data_[6], data_[7], uint8_t(data_[8] >> 1),
                                    bool(data_[8] & 1), bool(data_[9])};
        if (data_[0] == 'L' && data_[1] == 'C' && data_[2] == 1 && data_[9] <= 1 && next.valid() &&
            crc16(data_, 10) == (data_[10] | unsigned(data_[11]) << 8)) {
            state = next; sequence = data_[3]; used_ = 0;
            return true;
        }
        for (unsigned i = 1; i < kControlFrameSize; ++i) data_[i - 1] = data_[i];
        used_ = kControlFrameSize - 1;
        return false;
    }
private:
    uint8_t data_[kControlFrameSize] = {};
    unsigned used_ = 0;
    uint32_t last_ = 0;
};
}
