#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "bridge/FactoryKeyboard.hpp"
#include "bridge/MacroCodec.hpp"
#include "bridge/LightingDriver.hpp"
#include "bridge/SaveSnapshot.hpp"

using namespace bridge;
struct Keyboard {
    FactoryKeyboard keyboard;
    uint8_t rows[kRows] = {};
    uint32_t now = 1000;
    Keyboard() { update(); }
    void update() { keyboard.update(rows, now); }
    void key(unsigned kind, unsigned value, bool pressed) {
        for (unsigned i = 0; i < 144; ++i) if (kKeys[i].kind == kind && kKeys[i].value == value) {
            if (pressed) rows[i / 8] |= 1u << (i % 8);
            else rows[i / 8] &= ~(1u << (i % 8));
            now += 20; update(); return;
        }
        assert(false);
    }
    void control(unsigned value, bool pressed) { key(3, value, pressed); }
    void tap(unsigned value) { control(value, true); control(value, false); }
    bool held(unsigned usage) const {
        return KeyState::fromReport(keyboard.report()).key(usage);
    }
    void recordA(unsigned g = 0xb0) {
        tap(0xb6); tap(g); key(1, 4, true); key(1, 4, false); tap(0xb6);
    }
};

static void controls() {
    Keyboard k;
    const unsigned indices[] = {123,127,124,122,125,126,134,128,130,129};
    for (unsigned code = 0; code < 10; ++code) {
        uint8_t rows[18] = {};
        rows[indices[code] / 8] = 1u << (indices[code] % 8);
        const auto b = FactoryButtons::read(rows);
        if (code < 6) assert(b.g == 1u << code && b.m == 0);
        else assert(b.m == 1u << (code - 6) && b.g == 0);
    }
    k.control(0xb0, true); assert(k.held(0x1e));
    k.control(0xb0, false); assert(!k.held(0x1e));
    k.control(0xb5, true); assert(k.held(0x6a));
    k.control(0xb5, false);
    k.key(0x81, 8, true); k.key(0x81, 0x80, true);
    assert(k.keyboard.report().keyboard[0] == 0x88);
    k.control(0xc0, true);
    assert(k.keyboard.report().keyboard[0] == 0x88); // C0 is now sleep, never Windows Lock
    k.update(); assert(k.keyboard.report().keyboard[0] == 0x88);
    k.control(0xc0, false); k.tap(0xc0);
    assert(k.keyboard.report().keyboard[0] == 0x88);
    const unsigned levels[] = {3, 2, 1, 0, 5, 4};
    for (unsigned level : levels) {
        k.tap(0xc1);
        assert(k.keyboard.settings().brightnessA == level);
        assert(k.keyboard.settings().brightnessB == 4);
    }
    k.tap(0xc2); assert(k.keyboard.settings().brightnessB == 3);
    for (unsigned i = 0; i < 4; ++i) k.tap(0xc2);
    assert(k.keyboard.settings().brightnessB == 5);
    k.tap(0xc1);
    assert(k.keyboard.settings().brightnessA == 4 && k.keyboard.settings().brightnessB == 0);
    k.tap(0xb9); assert(k.keyboard.settings().bank == 2);
    k.tap(0xb8); assert(k.keyboard.settings().bank == 1);
    k.tap(0xb7); assert(k.keyboard.settings().bank == 0);
}

static void recordingAndPlayback() {
    Keyboard k;
    k.tap(0xb6); assert(k.keyboard.recordLed() == 4);
    k.control(0xb0, true); assert(k.keyboard.recordLed() == 7 && !k.held(0x1e));
    k.control(0xb0, false);
    k.key(0x81, 2, true); k.key(1, 4, true);
    assert(k.held(4)); // normal typing still reaches the host during recording
    k.tap(0xb6); // finish with Shift+A held: recorded macro must end with releases
    const auto macro = k.keyboard.macro(0);
    assert(macro.count == 4 && macro.valid() && k.keyboard.dirtyMacros() == 1);
    assert(macro.events[0].value == 0xe1 && macro.events[0].delayMs == 0);
    assert(macro.events[1].value == 4 && macro.events[1].delayMs == 20);
    k.key(1, 4, false); k.key(0x81, 2, false);
    k.control(0xb0, true); k.control(0xb0, false);
    const uint32_t start = k.now;
    k.keyboard.tick(start, false); assert(!k.held(0xe1)); // HID backpressure
    k.keyboard.tick(start, true); assert(k.held(0xe1) && !k.held(4));
    k.keyboard.tick(start + 19, true); assert(!k.held(4));
    k.keyboard.tick(start + 20, true); assert(k.held(4));
    k.keyboard.tick(start + 40, true); assert(!k.held(4) && k.held(0xe1));
    k.keyboard.tick(start + 40, true); assert(!k.held(0xe1) && !k.keyboard.playing());
    // A held G does not retrigger. Physical A survives the macro's release of A.
    k.control(0xb0, true);
    k.keyboard.tick(k.now, true);
    k.key(1, 4, true);
    k.keyboard.tick(k.now + 20, true);
    k.keyboard.tick(k.now + 100, true);
    k.keyboard.tick(k.now + 100, true);
    assert(k.held(4));
    k.key(1, 4, false); assert(!k.held(4));
    k.update(); assert(!k.keyboard.playing());
    k.control(0xb0, false);
    k.tap(0xb8); k.recordA();
    assert(k.keyboard.macro(6).count == 2 && k.keyboard.macro(0).count == 4);
    k.tap(0xb6); k.tap(0xb0); k.tap(0xb6); // empty recording restores default
    assert(!k.keyboard.macro(6).count);
    k.control(0xb0, true); assert(k.held(0x1e));
}

static void recordingFailureAndDisconnect() {
    Keyboard k; k.recordA();
    k.tap(0xb6); k.tap(0xb0);
    for (unsigned i = 0; i < 65; ++i) { k.key(1, 5, true); k.key(1, 5, false); }
    assert(k.keyboard.recordingError() && k.keyboard.macro(0).count == 2);
    assert(k.keyboard.macro(0).events[0].value == 4);
    k.keyboard.suspend(); k.update();
    k.tap(0xb6); k.tap(0xb0); k.key(1, 5, true);
    k.keyboard.suspend();
    assert(equal(k.keyboard.report(), Reports{}) && k.keyboard.recording() == FactoryKeyboard::Recording::Idle);
    assert(k.keyboard.macro(0).events[0].value == 4);
    k.key(1, 5, false); k.tap(0xb6);
    k.keyboard.tick(k.now + 60001, true);
    assert(k.keyboard.recording() == FactoryKeyboard::Recording::Idle);
    // Lost link while a macro key is held must release it, including on reconnect.
    k.keyboard.suspend(); k.update(); k.control(0xb0, true);
    k.keyboard.tick(k.now, true); assert(k.held(4));
    k.keyboard.suspend(); assert(!k.held(4));
    k.update(); k.keyboard.tick(k.now + 1000, true); assert(!k.held(4));
    // Sleep key no longer filters Windows keys in recorded macros.
    Keyboard windows;
    windows.tap(0xb6); windows.tap(0xb0);
    windows.key(0x81, 8, true); windows.key(0x81, 8, false); windows.tap(0xb6);
    windows.tap(0xc0); windows.tap(0xb0); windows.keyboard.tick(windows.now, true);
    assert(windows.keyboard.report().keyboard[0] == 8);
}

static void storageFormat() {
    Keyboard k; k.recordA();
    uint8_t data[MacroCodec::kMaxSize];
    const auto size = MacroCodec::encode(0, k.keyboard.macro(0), data);
    Macro restored;
    assert(MacroCodec::decode(0, data, size, restored) && restored.valid());
    assert(!MacroCodec::decode(1, data, size, restored));
    for (unsigned i = 0; i < size; ++i) {
        data[i] ^= 0x80;
        assert(!MacroCodec::decode(0, data, size, restored));
        data[i] ^= 0x80;
    }
    // CRC alone is insufficient: reject an unbalanced, otherwise valid file.
    data[14] = uint8_t(MacroAction::KeyDown);
    const auto crc = crc16(data, size - 2);
    data[size - 2] = crc; data[size - 1] = crc >> 8;
    assert(!MacroCodec::decode(0, data, size, restored));
    FactorySettings settings = {2, 1, 5}, result;
    MacroCodec::encodeSettings(settings, data);
    assert(MacroCodec::decodeSettings(data, 10, result));
    assert(result.bank == 2 && result.brightnessA == 1 && result.brightnessB == 5);
    // Old WinLock=1 settings migrate without losing bank/brightness.
    data[6] = 0; data[7] = 1;
    const auto oldCrc = crc16(data, 8); data[8] = oldCrc; data[9] = oldCrc >> 8;
    assert(MacroCodec::decodeSettings(data, 10, result) && result.bank == 2);
    MacroRecorder recorder;
    KeyState state;
    recorder.start(0xfffffff0u);
    state.key(4, true); assert(recorder.capture(state, 0xfffffff5u));
    state.key(4, false); assert(recorder.capture(state, 5));
    assert(recorder.finish(10));
    assert(recorder.result().events[1].delayMs == 16);
    MacroPlayer player;
    player.start(recorder.result(), 0xfffffff5u); player.tick(0xfffffff5u);
    assert(player.state().key(4)); player.tick(4); assert(player.state().key(4));
    player.tick(5); assert(!player.state().key(4));
    // Simultaneous Shift+A must not briefly emit a lowercase A on replay.
    recorder.start(100);
    state.key(4, true); state.key(0xe1, true);
    assert(recorder.capture(state, 100) && recorder.finish(120));
    const auto& chord = recorder.result();
    assert(chord.count == 4 && chord.events[0].value == 0xe1 && chord.events[1].value == 4);
    assert(chord.events[2].value == 4 && chord.events[3].value == 0xe1);
    player.start(chord, 200); player.tick(200);
    assert(player.state().key(0xe1) && !player.state().key(4));
    player.tick(200); assert(player.state().key(4));

    // A background write completing after a newer edit must keep that edit dirty.
    SaveSnapshot snapshot; HostProfiles hosts;
    assert(snapshot.capture(k.keyboard, hosts) && !snapshot.settings() && snapshot.slot() == 0);
    k.tap(0xb6); k.tap(0xb0); k.key(1, 5, true); k.key(1, 5, false); k.tap(0xb6);
    snapshot.accept(k.keyboard, hosts); assert(k.keyboard.dirtyMacros() == 1);
    assert(snapshot.capture(k.keyboard, hosts)); snapshot.accept(k.keyboard, hosts);
    assert(!k.keyboard.dirtyMacros() && !snapshot.capture(k.keyboard, hosts));
    k.tap(0xc1); assert(snapshot.capture(k.keyboard, hosts) && snapshot.settings());
    k.tap(0xc1); snapshot.accept(k.keyboard, hosts); assert(k.keyboard.settingsDirty());
    assert(snapshot.capture(k.keyboard, hosts)); snapshot.accept(k.keyboard, hosts);
    assert(!k.keyboard.settingsDirty() && !snapshot.capture(k.keyboard, hosts));
}

struct Bus {
    struct Write { uint8_t chip, index, value; };
    std::vector<Write> writes;
    bool missing = false;
    bool write(uint8_t chip, uint8_t index, uint8_t value) {
        writes.push_back({chip, index, value});
        return !(missing && chip == 0x61);
    }
    bool contains(uint8_t chip, uint8_t index, uint8_t value) const {
        for (const auto& w : writes) if (w.chip == chip && w.index == index && w.value == value) return true;
        return false;
    }
};
static void lightingAndWire() {
    Bus bus; LightingDriver<Bus> lights(bus); LightingState state;
    assert(lights.apply(state, 0) == 3);
    assert(bus.contains(0x60, 5, 0x9f) && bus.contains(0x61, 2, 0x9f));
    assert(bus.contains(0x60, 8, 0x95) && bus.contains(0x61, 8, 0x96));
    bus.writes.clear(); assert(lights.apply(state, 10) == 3 && bus.writes.empty());
    state.recordLed = 7; lights.apply(state, 20);
    assert(bus.contains(0x60, 4, 0x9f) && bus.contains(0x60, 8, 0xa5));
    bus.writes.clear(); lights.apply(state, 250); assert(bus.contains(0x60, 8, 0x95));
    state.bank = 2; state.windowsLocked = true;
    bus.writes.clear(); lights.apply(state, 300);
    assert(bus.contains(0x60, 2, 0x9f) && bus.contains(0x61, 3, 0x9f));
    state.hostLeds = 7; state.brightnessA = 0; state.brightnessB = 0;
    assert(!state.dark() && state.visibleHostLeds() == 7); // 0 != global blackout
    state.brightnessA = 5;
    bus.writes.clear(); assert(lights.apply(state, 400) == 3);
    assert(state.dark() && state.visibleHostLeds() == 0);
    assert(bus.contains(0x60, 8, 0x55) && bus.contains(0x61, 8, 0x55));
    assert(bus.contains(0x60, 0, 0x91) && bus.contains(0x61, 0, 0x91));
    state.brightnessA = 4;
    bus.writes.clear(); lights.apply(state, 420);
    assert(bus.contains(0x60, 0, 0x81) && state.visibleHostLeds() == 7);
    state.sleep = true;
    bus.writes.clear(); lights.apply(state, 440);
    assert(state.visibleHostLeds() == 0 && bus.contains(0x61, 8, 0x55));
    Bus failure; failure.missing = true; LightingDriver<Bus> partial(failure);
    assert(partial.apply(state, 0) == 1);
    failure.writes.clear(); partial.apply(state, 20); assert(failure.writes.empty());
    failure.missing = false; assert(partial.apply(state, 1000) == 3);
    uint8_t command[kControlFrameSize]; encodeControl(state, 255, command);
    LightingState decoded; uint8_t ack = 0;
    for (unsigned corrupt = 0; corrupt < sizeof(command); ++corrupt) {
        ControlParser parser; unsigned accepted = 0;
        for (unsigned i = 0; i < sizeof(command); ++i)
            accepted += parser.push(command[i] ^ (i == corrupt ? 0x80 : 0), i * 260, decoded, ack);
        assert(!accepted);
        for (unsigned i = 0; i < sizeof(command); ++i) accepted += parser.push(command[i], 4000 + i * 260, decoded, ack);
        assert(accepted == 1 && ack == 255 && equal(state, decoded));
    }
    uint8_t rows[18] = {1, 2, 3}, frame[kStatusFrameSize], output[18];
    encodeStatus(rows, 10, 255, 7, frame);
    for (unsigned corrupt = 0; corrupt < sizeof(frame); ++corrupt) {
        FrameParser parser; unsigned accepted = 0;
        for (unsigned i = 0; i < sizeof(frame); ++i) accepted += parser.push(frame[i] ^ (i == corrupt ? 1 : 0), 0, output);
        assert(!accepted);
        for (auto byte : frame) accepted += parser.push(byte, 1, output);
        assert(accepted == 1 && parser.version() == 2 && parser.acknowledged() == 255 && parser.flags() == 7);
        assert(memcmp(rows, output, 18) == 0);
        uint8_t legacy[kFrameSize]; encode(rows, 1, legacy);
        for (auto byte : legacy) parser.push(byte, 2, output);
        assert(parser.version() == 1 && parser.flags() == 0);
    }
}

int main() {
    controls(); recordingAndPlayback(); recordingFailureAndDisconnect(); storageFormat(); lightingAndWire();
    puts("PASS: factory controls, macro banks/record/play/release/backpressure/limits, persistence CRC, lighting and reverse-link recovery");
}
