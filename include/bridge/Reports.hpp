#pragma once
#include <stddef.h>
#include <stdint.h>

namespace bridge {
struct Reports {
    uint8_t keyboard[8] = {};
    uint8_t consumer = 0;
};

inline bool equal(const Reports& a, const Reports& b) {
    if (a.consumer != b.consumer) return false;
    for (size_t i = 0; i < 8; ++i)
        if (a.keyboard[i] != b.keyboard[i]) return false;
    return true;
}

// A set allows physical and macro keys to overlap without releasing each other.
class KeyState {
public:
    bool key(uint8_t usage) const { return keys_[usage / 8] & (1u << (usage % 8)); }
    void key(uint8_t usage, bool down) {
        if (down) keys_[usage / 8] |= 1u << (usage % 8);
        else keys_[usage / 8] &= ~(1u << (usage % 8));
    }
    bool consumer(uint8_t bit) const { return consumer_ & (1u << bit); }
    void consumer(uint8_t bit, bool down) {
        if (down) consumer_ |= 1u << bit;
        else consumer_ &= ~(1u << bit);
    }
    void merge(const KeyState& other) {
        for (size_t i = 0; i < sizeof(keys_); ++i) keys_[i] |= other.keys_[i];
        consumer_ |= other.consumer_;
    }
    unsigned count() const {
        unsigned result = 0;
        for (unsigned i = 4; i < 232; ++i) result += key(i);
        for (unsigned i = 0; i < 7; ++i) result += consumer(i);
        return result;
    }
    static KeyState fromReport(const Reports& report) {
        KeyState state;
        for (unsigned i = 0; i < 8; ++i)
            if (report.keyboard[0] & (1u << i)) state.key(0xe0 + i, true);
        for (unsigned i = 2; i < 8; ++i)
            if (report.keyboard[i] >= 4) state.key(report.keyboard[i], true);
        state.consumer_ = report.consumer & 0x7f;
        return state;
    }
    Reports report() const {
        Reports result;
        for (unsigned i = 0; i < 8; ++i)
            if (key(0xe0 + i)) result.keyboard[0] |= 1u << i;
        unsigned count = 0;
        for (unsigned i = 4; i < 0xe0; ++i) if (key(i)) {
            if (count < 6) result.keyboard[2 + count] = i;
            ++count;
        }
        if (count > 6) for (unsigned i = 2; i < 8; ++i) result.keyboard[i] = 1;
        result.consumer = consumer_;
        return result;
    }
private:
    uint8_t keys_[32] = {};
    uint8_t consumer_ = 0;
};
}
