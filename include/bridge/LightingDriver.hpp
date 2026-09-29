#pragma once
#include "ControlProtocol.hpp"

namespace bridge {
// Register values and channel routing are recovered from the original image.
// Bus supplies write(address7, register, value); errors never stop scanning.
template<class Bus> class LightingDriver {
public:
    explicit LightingDriver(Bus& bus) : bus_(bus) {}
    uint8_t apply(const LightingState& state, uint32_t nowMs) {
        const uint8_t mr = state.recordLed == 7 ? ((nowMs / 250) & 1 ? 0 : 4) : state.recordLed;
        uint8_t levels[2][4] = {
            {uint8_t(state.bank == 2 ? 4 : 0), uint8_t(state.bank == 1 ? 4 : 0), mr, state.brightnessA},
            {uint8_t(state.bank == 0 ? 4 : 0), uint8_t(state.windowsLocked ? 4 : 0), 0, state.brightnessB},
        };
        if (state.dark()) for (auto& chip : levels) for (auto& level : chip) level = 0;
        for (unsigned chip = 0; chip < 2; ++chip) {
            if (!(online_ & (1u << chip)) && attempted_[chip] && nowMs - attemptedAt_[chip] < 1000) continue;
            attempted_[chip] = true; attemptedAt_[chip] = nowMs;
            const uint8_t address = 0x60 + chip;
            bool ok = true;
            if (!(online_ & (1u << chip))) {
                ok = bus_.write(address, 0, 0x81) && bus_.write(address, 1, 5) &&
                     bus_.write(address, 8, chip ? 0x45 : 0x55);
                for (auto& value : last_[chip]) value = 0xff;
            }
            uint8_t mode = 0;
            for (unsigned channel = 0; channel < 4; ++channel) {
                const uint8_t level = levels[chip][channel];
                mode |= uint8_t(level ? 2 : 1) << (2 * channel);
                if (ok && level && last_[chip][channel] != level)
                    ok = bus_.write(address, 2 + channel, pwm(chip, level));
            }
            bool changed = !(online_ & (1u << chip));
            for (unsigned c = 0; c < 4; ++c) changed |= last_[chip][c] != levels[chip][c];
            if (ok && changed) ok = bus_.write(address, 8, mode);
            // Constant LEDOUT=1 disables the inverted external LED stages.
            // Stop the PWM oscillator only after all outputs have been set off.
            if (ok && (!(online_ & (1u << chip)) || sleeping_[chip] != state.dark()))
                ok = bus_.write(address, 0, state.dark() ? 0x91 : 0x81);
            if (ok) {
                online_ |= 1u << chip;
                sleeping_[chip] = state.dark();
                for (unsigned c = 0; c < 4; ++c) last_[chip][c] = levels[chip][c];
            } else online_ &= ~(1u << chip);
        }
        return online_;
    }
private:
    static uint8_t pwm(unsigned chip, unsigned level) {
        const uint8_t values[] = {0, uint8_t(chip ? 0xe6 : 0xe8), 0xe0, 0xc7, 0x9f};
        return values[level];
    }
    Bus& bus_;
    uint32_t attemptedAt_[2] = {};
    bool attempted_[2] = {};
    bool sleeping_[2] = {};
    uint8_t online_ = 0;
    uint8_t last_[2][4] = {};
};
}
