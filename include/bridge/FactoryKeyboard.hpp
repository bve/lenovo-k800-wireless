#pragma once
#include "KeyMapper.hpp"
#include "Macros.hpp"

namespace bridge {
struct FactoryButtons {
    uint8_t g = 0, m = 0, controls = 0;
    static FactoryButtons read(const uint8_t* rows) {
        FactoryButtons result;
        for (unsigned i = 120; i < 136; ++i) {
            if (!(rows[i / 8] & (1u << (i % 8))) || kKeys[i].kind != 3) continue;
            const uint8_t code = kKeys[i].value;
            if (code >= 0xb0 && code <= 0xb5) result.g |= 1u << (code - 0xb0);
            else if (code >= 0xb6 && code <= 0xb9) result.m |= 1u << (code - 0xb6);
            else if (code >= 0xc0 && code <= 0xc2) result.controls |= 1u << (code - 0xc0);
        }
        return result;
    }
};
struct FactorySettings {
    uint8_t bank = 0, brightnessA = 4, brightnessB = 4;
    bool valid() const { return bank < 3 && brightnessA <= 5 && brightnessB <= 5; }
};

class FactoryKeyboard {
public:
    enum class Recording : uint8_t { Idle, Armed, Recording };
    static constexpr unsigned kSlots = 18;
    void update(const uint8_t* rows, uint32_t now) {
        const auto buttons = FactoryButtons::read(rows);
        blockedG_ &= buttons.g;
        const uint8_t risingG = primed_ ? buttons.g & ~previous_.g : 0;
        const uint8_t risingM = primed_ ? buttons.m & ~previous_.m : 0;
        const uint8_t risingControls = primed_ ? buttons.controls & ~previous_.controls : 0;
        if (!primed_) blockedG_ = buttons.g;
        primed_ = true;
        previous_ = buttons;
        if (risingControls & 2) { cycle(settings_.brightnessA); settingsDirty_ = true; }
        if (risingControls & 4) { cycle(settings_.brightnessB); settingsDirty_ = true; }
        for (unsigned bank = 0; bank < 3; ++bank) if (risingM & (2u << bank)) {
            finishRecording(now);
            player_.cancel();
            settings_.bank = bank;
            settingsDirty_ = true;
            blockedG_ |= buttons.g;
        }
        if (risingM & 1) {
            player_.cancel();
            blockedG_ |= buttons.g;
            if (recording_ == Recording::Idle) {
                recording_ = Recording::Armed;
                armedAt_ = now;
                recordingError_ = false;
            } else finishRecording(now);
        }
        if (!(risingM & 1)) for (unsigned g = 0; g < 6; ++g) if ((risingG & ~blockedG_) & (1u << g)) {
            if (recording_ == Recording::Armed) {
                recordSlot_ = settings_.bank * 6 + g;
                recorder_.start(now);
                recording_ = Recording::Recording;
                break;
            }
            if (recording_ == Recording::Idle && macros_[settings_.bank * 6 + g].count) {
                player_.start(macros_[settings_.bank * 6 + g], now);
                break;
            }
        }
        uint8_t defaultKeys = 0;
        if (recording_ == Recording::Idle)
            for (unsigned g = 0; g < 6; ++g)
                if (!macros_[settings_.bank * 6 + g].count) defaultKeys |= 1u << g;
        physical_ = mapper_.map(rows, defaultKeys & ~blockedG_);
        if (recording_ == Recording::Recording) {
            // ErrorRollOver cannot be reconstructed into individual keys.
            bool rollover = false;
            for (unsigned i = 2; i < 8; ++i) rollover |= physical_.keyboard[i] == 1;
            if (rollover || !recorder_.capture(KeyState::fromReport(physical_), now)) recordFailed(now);
        }
    }
    void tick(uint32_t now, bool outputReady) {
        if (recording_ == Recording::Armed && now - armedAt_ > Macro::kMaxDurationMs) recordFailed(now);
        if (recordingError_ && now - errorAt_ >= 2000) recordingError_ = false;
        if (outputReady) player_.tick(now);
    }
    Reports report() const {
        for (unsigned i = 2; i < 8; ++i) if (physical_.keyboard[i] == 1) return physical_;
        KeyState state = KeyState::fromReport(physical_);
        state.merge(player_.state());
        return state.report();
    }
    void prepareSleep(uint32_t now) { finishRecording(now); suspend(); }
    void prepareHostChange(uint32_t now) {
        finishRecording(now); player_.cancel(); blockedG_ |= previous_.g;
        // Keep physical keys visible to the router's release barrier.
    }
    void suspend() {
        player_.cancel(); recording_ = Recording::Idle; physical_ = Reports{};
        previous_ = FactoryButtons{}; primed_ = false; blockedG_ = 0;
    }
    Recording recording() const { return recording_; }
    uint8_t recordLed() const {
        if (recordingError_ || recording_ == Recording::Recording) return 7;
        return recording_ == Recording::Armed ? 4 : 0;
    }
    bool recordingError() const { return recordingError_; }
    bool playing() const { return player_.active(); }
    const FactoryButtons& buttons() const { return previous_; }
    const FactorySettings& settings() const { return settings_; }
    void restoreSettings(const FactorySettings& settings) { if (settings.valid()) settings_ = settings; }
    const Macro& macro(unsigned slot) const { return macros_[slot]; }
    void restoreMacro(unsigned slot, const Macro& macro) { if (slot < kSlots && macro.valid()) macros_[slot] = macro; }
    uint32_t dirtyMacros() const { return dirtyMacros_; }
    void macroSaved(unsigned slot) { dirtyMacros_ &= ~(1u << slot); }
    bool settingsDirty() const { return settingsDirty_; }
    void settingsSaved() { settingsDirty_ = false; }
private:
    void cycle(uint8_t& value) {
        if (settings_.brightnessA == 5 || settings_.brightnessB == 5) {
            if (settings_.brightnessA == 5) settings_.brightnessA = 0;
            if (settings_.brightnessB == 5) settings_.brightnessB = 0;
            value = 4;
        } else value = value ? value - 1 : 5;
    }
    void recordFailed(uint32_t now) {
        recording_ = Recording::Idle; recordingError_ = true; errorAt_ = now;
        blockedG_ |= previous_.g;
    }
    void finishRecording(uint32_t now) {
        if (recording_ == Recording::Recording) {
            if (recorder_.finish(now)) {
                macros_[recordSlot_] = recorder_.result();
                dirtyMacros_ |= 1u << recordSlot_;
            } else recordFailed(now);
        }
        recording_ = Recording::Idle;
        blockedG_ |= previous_.g;
    }
    KeyMapper mapper_;
    FactorySettings settings_;
    Macro macros_[kSlots];
    MacroRecorder recorder_;
    MacroPlayer player_;
    Reports physical_;
    FactoryButtons previous_;
    Recording recording_ = Recording::Idle;
    uint32_t dirtyMacros_ = 0, armedAt_ = 0, errorAt_ = 0;
    unsigned recordSlot_ = 0;
    uint8_t blockedG_ = 0;
    bool primed_ = false, settingsDirty_ = false, recordingError_ = false;
};
}
