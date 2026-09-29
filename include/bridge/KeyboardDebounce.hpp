#pragma once
#include "Debouncer.hpp"
#include "KeyboardLayout.hpp"

namespace bridge {
// Per-key release adaptation. An observed short reopening is evidence to
// increase filtering, not proof of chatter: very fast taps look the same.
// Modifier/control releases stay fast to preserve chord and profile timing.
struct KeyboardDebounceTiming : DefaultDebounceTiming {
    static constexpr unsigned kKeysCount = kRows * kColumns;
    static constexpr uint8_t kInitialMs = 40, kMinimumMs = 5, kMaximumMs = 60;
    static constexpr uint8_t kMarginMs = 8, kCleanCycles = 16, kDecayMs = 5;
    KeyboardDebounceTiming() {
        for (auto& value : releaseMs_) value = kInitialMs;
    }
    uint32_t periodUs(size_t index, bool pressed) const {
        return !pressed && adaptive(index) ? releaseMs_[index] * 1000u : 5000u;
    }
    void candidateChanged(size_t index, bool pressed, uint32_t previousUs, bool stablePressed) {
        if (!adaptive(index) || !pressed || !bit(seen_, index)) return;
        // While the accepted key is still down, a reclosure cancels an opening.
        // After an already reported release, only learn from gaps <= 40 ms;
        // a longer gap is treated as intentional. An emitted release cannot be
        // undone, so a gap exceeding the learned window may escape once.
        const uint32_t limit = stablePressed ? kMaximumMs * 1000u : kInitialMs * 1000u;
        if (previousUs > limit) return;
        const uint32_t required = (previousUs + 999u) / 1000u + kMarginMs;
        const uint8_t bounded = required > kMaximumMs ? kMaximumMs : uint8_t(required);
        if (bounded > releaseMs_[index]) releaseMs_[index] = bounded;
        clean_[index] = 0;
        set(noisy_, index, true);
    }
    void accepted(size_t index, bool pressed) {
        if (!adaptive(index)) return;
        if (pressed) { set(seen_, index, true); return; }
        if (bit(noisy_, index)) clean_[index] = 0;
        else if (++clean_[index] >= kCleanCycles) {
            const uint8_t value = releaseMs_[index];
            releaseMs_[index] = value > kMinimumMs + kDecayMs ? value - kDecayMs : kMinimumMs;
            clean_[index] = 0;
        }
        set(noisy_, index, false);
    }
private:
    static bool adaptive(size_t index) { return kKeys[index].kind == 1; }
    static bool bit(const uint8_t* rows, size_t index) { return rows[index / 8] & (1u << (index % 8)); }
    static void set(uint8_t* rows, size_t index, bool enabled) {
        const uint8_t mask = 1u << (index % 8);
        if (enabled) rows[index / 8] |= mask;
        else rows[index / 8] &= ~mask;
    }
    uint8_t releaseMs_[kKeysCount];
    uint8_t clean_[kKeysCount] = {};
    uint8_t seen_[kRows] = {}, noisy_[kRows] = {};
};
}
