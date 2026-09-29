#pragma once
#include <stdint.h>

namespace bridge {
// A complete press/release is required. A key held at boot/reconnect is ignored.
class SleepButton {
public:
    bool update(bool pressed) {
        const bool released = primed_ && armed_ && !pressed;
        if (primed_ && pressed && !previous_) armed_ = true;
        if (!pressed) armed_ = false;
        previous_ = pressed;
        primed_ = true;
        return released;
    }
private:
    bool primed_ = false, previous_ = false, armed_ = false;
};

class SleepController {
public:
    enum class Action { None, EnterOff, Recover };
    void begin(uint32_t now) { phase_ = Phase::Saving; started_ = now; }
    bool active() const { return phase_ != Phase::Running; }
    bool requested() const { return phase_ == Phase::Waiting; }
    Action poll(uint32_t now, bool savedAndReleased, bool acknowledged) {
        if (!active()) return Action::None;
        if (phase_ == Phase::Saving && savedAndReleased) {
            phase_ = Phase::Waiting;
            started_ = now;
        } else if (phase_ == Phase::Waiting && acknowledged) {
            return Action::EnterOff;
        }
        if (now - started_ >= (phase_ == Phase::Saving ? 30000u : 3000u)) {
            phase_ = Phase::Running;
            return Action::Recover;
        }
        return Action::None;
    }
private:
    enum class Phase { Running, Saving, Waiting };
    Phase phase_ = Phase::Running;
    uint32_t started_ = 0;
};
}
