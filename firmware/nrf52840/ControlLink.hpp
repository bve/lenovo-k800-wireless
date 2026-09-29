#pragma once
#include <Arduino.h>
#include "bridge/ControlProtocol.hpp"

// Called only after a complete matrix frame, during LPC's receive window.
class ControlLink {
public:
    void reset() { *this = ControlLink{}; }
    void received(const bridge::FrameParser& frame, const bridge::LightingState& desired) {
        version_ = frame.version(); ack_ = frame.acknowledged(); flags_ = frame.flags();
        if (version_ != 2) return; // Upgrade nRF first: an older LPC remains usable.
        if (desired.sleep && !sleepSupported()) return;
        if (!sent_ || !bridge::equal(desired, last_)) {
            ++sequence_; last_ = desired; sent_ = true;
        } else if ((flags_ & 1) && ack_ == sequence_) return;
        uint8_t command[bridge::kControlFrameSize];
        bridge::encodeControl(last_, sequence_, command);
        Serial1.write(command, sizeof(command));
    }
    uint8_t version() const { return version_; }
    uint8_t sequence() const { return sequence_; }
    uint8_t acknowledged() const { return ack_; }
    uint8_t flags() const { return flags_; }
    bool sleepSupported() const { return flags_ & bridge::kSleepSupported; }
    bool sleepReady() const {
        return sent_ && last_.sleep && ack_ == sequence_ &&
               (flags_ & (1 | bridge::kSleepSupported | bridge::kSleepReady)) ==
                   (1 | bridge::kSleepSupported | bridge::kSleepReady);
    }
private:
    bridge::LightingState last_;
    uint8_t version_ = 0, sequence_ = 0, ack_ = 0, flags_ = 0;
    bool sent_ = false;
};
