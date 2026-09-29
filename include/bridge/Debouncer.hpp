#pragma once
#include "Protocol.hpp"

namespace bridge {
struct DefaultDebounceTiming {
    static constexpr uint32_t periodUs(size_t, bool) { return 5000; }
    void candidateChanged(size_t, bool, uint32_t, bool) {}
    void accepted(size_t, bool) {}
};

template<class Timing = DefaultDebounceTiming>
class Debouncer {
public:
    bool update(const uint8_t* raw, uint32_t nowUs) {
        bool changed = false;
        for (size_t row = 0; row < kRows; ++row) {
            for (size_t column = 0; column < kColumns; ++column) {
                const uint8_t mask = 1u << column;
                const size_t index = row * kColumns + column;
                if ((raw[row] ^ candidate_[row]) & mask) {
                    timing_.candidateChanged(index, raw[row] & mask, nowUs - since_[index], stable_[row] & mask);
                    candidate_[row] ^= mask;
                    since_[index] = nowUs;
                } else if (((stable_[row] ^ candidate_[row]) & mask) &&
                           nowUs - since_[index] >= timing_.periodUs(index, candidate_[row] & mask)) {
                    stable_[row] ^= mask;
                    timing_.accepted(index, stable_[row] & mask);
                    changed = true;
                }
            }
        }
        return changed;
    }
    const uint8_t* rows() const { return stable_; }
    uint32_t releasePeriodUs(size_t index) const { return timing_.periodUs(index, false); }

private:
    Timing timing_;
    uint8_t stable_[kRows] = {};
    uint8_t candidate_[kRows] = {};
    uint32_t since_[kRows * kColumns] = {};
};
}
