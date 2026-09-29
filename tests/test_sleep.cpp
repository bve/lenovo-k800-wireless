#include <assert.h>
#include <stdio.h>
#include <vector>
#include "bridge/SleepController.hpp"
#include "bridge/FactoryKeyboard.hpp"
#include "ControlLink.hpp"

FakeUart Serial1;
static std::vector<uint8_t> written;
size_t FakeUart::write(const uint8_t* data, size_t length) {
    written.assign(data, data + length);
    return length;
}
static bridge::FrameParser frame(uint8_t ack, uint8_t flags) {
    bridge::FrameParser parser;
    uint8_t rows[18] = {}, data[bridge::kStatusFrameSize];
    bridge::encodeStatus(rows, 0, ack, flags, data);
    for (auto byte : data) parser.push(byte, 0, rows);
    return parser;
}
int main() {
    using namespace bridge;
    SleepButton key;
    assert(!key.update(true) && !key.update(true) && !key.update(false));
    assert(!key.update(true) && !key.update(true) && key.update(false));
    assert(!key.update(false) && !key.update(false));

    SleepController sleep;
    sleep.begin(0xfffffff0u);
    assert(sleep.active() && !sleep.requested());
    assert(sleep.poll(20, false, true) == SleepController::Action::None); // old ACK cannot skip saving
    assert(!sleep.requested());
    sleep.poll(30, true, false); assert(sleep.requested());
    assert(sleep.poll(3029, false, false) == SleepController::Action::None);
    assert(sleep.poll(3030, false, false) == SleepController::Action::Recover && !sleep.active());
    sleep.begin(10000);
    assert(sleep.poll(40000, false, false) == SleepController::Action::Recover); // FS never completes
    sleep.begin(50000); sleep.poll(50001, true, false);
    assert(sleep.poll(50002, true, true) == SleepController::Action::EnterOff);

    ControlLink link; LightingState desired;
    desired.sleep = true;
    link.received(frame(0, 7), desired);
    assert(!link.sleepSupported() && !link.sleepReady() && written.empty()); // old LPC
    link.received(frame(0, 7 | kSleepSupported), desired);
    assert(link.sleepSupported() && !written.empty() && !link.sleepReady());
    const uint8_t sequence = link.sequence();
    link.received(frame(sequence, 7 | kSleepSupported), desired);
    assert(!link.sleepReady()); // accepted command != ready to sleep
    link.received(frame(sequence + 1, 7 | kSleepSupported | kSleepReady), desired);
    assert(!link.sleepReady());
    link.received(frame(sequence, 7 | kSleepSupported | kSleepReady), desired);
    assert(link.sleepReady());
    desired.sleep = false;
    link.received(frame(sequence, 7 | kSleepSupported | kSleepReady), desired);
    assert(!link.sleepReady());
    link.reset(); assert(!link.sleepReady());

    // Sleep finishes an active recording and releases keys before persisting it.
    FactoryKeyboard keyboard; uint8_t rows[18] = {};
    keyboard.update(rows, 0);
    rows[16] = 0x40; keyboard.update(rows, 10); // MR
    rows[16] = 0; keyboard.update(rows, 20);
    rows[15] = 8; keyboard.update(rows, 30); // G1
    rows[15] = 0; keyboard.update(rows, 40);
    assert(keyboard.recording() == FactoryKeyboard::Recording::Recording);
    keyboard.prepareSleep(50);
    assert(keyboard.recording() == FactoryKeyboard::Recording::Idle && !keyboard.playing());
    assert(equal(keyboard.report(), Reports{}) && keyboard.dirtyMacros() == 1);
    puts("PASS: sleep press/release, held key at startup, persistence/ACK barriers, legacy LPC and timeouts");
}
