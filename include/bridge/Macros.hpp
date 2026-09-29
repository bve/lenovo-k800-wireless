#pragma once
#include "Reports.hpp"

namespace bridge {
enum class MacroAction : uint8_t { KeyDown = 1, KeyUp, ConsumerDown, ConsumerUp };
struct MacroEvent {
    uint16_t delayMs = 0;
    MacroAction action = MacroAction::KeyDown;
    uint8_t value = 0;
    bool valid() const {
        return ((action == MacroAction::KeyDown || action == MacroAction::KeyUp) &&
                value >= 4 && value <= 0xe7) ||
               ((action == MacroAction::ConsumerDown || action == MacroAction::ConsumerUp) && value < 7);
    }
    bool apply(KeyState& state) const {
        if (!valid()) return false;
        const bool keyboard = action == MacroAction::KeyDown || action == MacroAction::KeyUp;
        const bool down = action == MacroAction::KeyDown || action == MacroAction::ConsumerDown;
        if ((keyboard ? state.key(value) : state.consumer(value)) == down) return false;
        if (keyboard) state.key(value, down);
        else state.consumer(value, down);
        return true;
    }
};
struct Macro {
    static constexpr unsigned kMaxEvents = 128;
    static constexpr uint32_t kMaxDurationMs = 60000;
    uint16_t count = 0;
    MacroEvent events[kMaxEvents];
    bool valid() const {
        if (count > kMaxEvents) return false;
        KeyState state;
        uint32_t duration = 0;
        for (unsigned i = 0; i < count; ++i) {
            duration += events[i].delayMs;
            if (duration > kMaxDurationMs || !events[i].apply(state)) return false;
        }
        return state.count() == 0;
    }
};

class MacroRecorder {
public:
    void start(uint32_t now) {
        recording_.count = 0;
        previous_ = KeyState{};
        started_ = last_ = now;
        failed_ = false;
    }
    bool capture(const KeyState& current, uint32_t now) {
        if (failed_) return false;
        if (now - started_ > Macro::kMaxDurationMs) return fail();
        unsigned changes = 0;
        for (unsigned i = 4; i <= 0xe7; ++i) changes += current.key(i) != previous_.key(i);
        for (unsigned i = 0; i < 7; ++i) changes += current.consumer(i) != previous_.consumer(i);
        // Reserve enough room to release every key if MR is pressed while held.
        if (recording_.count + changes + current.count() > Macro::kMaxEvents) return fail();
        for (unsigned down = 0; down < 2; ++down) {
            // A chord may arrive in one matrix snapshot: press modifiers first
            // and release them last so replay never emits an unmodified letter.
            for (unsigned group = 0; group < 2; ++group) {
                const bool modifiers = group != down;
                for (unsigned i = modifiers ? 0xe0 : 4; i < (modifiers ? 0xe8 : 0xe0); ++i)
                    if (current.key(i) == bool(down) && current.key(i) != previous_.key(i))
                        append(down ? MacroAction::KeyDown : MacroAction::KeyUp, i, now);
            }
            for (unsigned i = 0; i < 7; ++i)
                if (current.consumer(i) == bool(down) && current.consumer(i) != previous_.consumer(i))
                    append(down ? MacroAction::ConsumerDown : MacroAction::ConsumerUp, i, now);
        }
        previous_ = current;
        return true;
    }
    bool finish(uint32_t now) { return capture(KeyState{}, now) && recording_.valid(); }
    const Macro& result() const { return recording_; }
private:
    bool fail() { failed_ = true; return false; }
    void append(MacroAction action, uint8_t value, uint32_t now) {
        auto& event = recording_.events[recording_.count];
        event.delayMs = recording_.count ? uint16_t(now - last_) : 0;
        event.action = action;
        event.value = value;
        ++recording_.count;
        last_ = now;
    }
    Macro recording_;
    KeyState previous_;
    uint32_t started_ = 0, last_ = 0;
    bool failed_ = false;
};

class MacroPlayer {
public:
    void start(const Macro& macro, uint32_t now) {
        cancel();
        if (!macro.count) return;
        macro_ = &macro;
        due_ = now + macro.events[0].delayMs;
    }
    void cancel() { macro_ = nullptr; index_ = 0; state_ = KeyState{}; }
    bool active() const { return macro_ != nullptr; }
    const KeyState& state() const { return state_; }
    void tick(uint32_t now) {
        if (!macro_ || int32_t(now - due_) < 0) return;
        macro_->events[index_++].apply(state_);
        if (index_ == macro_->count) macro_ = nullptr;
        else due_ = now + macro_->events[index_].delayMs;
    }
private:
    const Macro* macro_ = nullptr;
    unsigned index_ = 0;
    uint32_t due_ = 0;
    KeyState state_;
};
}
