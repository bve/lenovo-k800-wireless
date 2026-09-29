#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bridge/Debouncer.hpp"
#include "bridge/KeyboardDebounce.hpp"
#include "bridge/KeyMapper.hpp"
#include "bridge/ReportQueue.hpp"

static void testWireRecovery() {
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    assert(bridge::crc16(check, sizeof(check)) == 0x29b1);
    uint8_t rows[bridge::kRows] = {0x4b, 0x42, 0xff, 0x01};
    uint8_t frame[bridge::kFrameSize];
    bridge::encode(rows, 255, frame);
    for (size_t corrupt = 0; corrupt < bridge::kFrameSize; ++corrupt) {
        bridge::FrameParser parser;
        uint8_t result[bridge::kRows] = {};
        unsigned accepted = 0;
        // Corruption must not produce any state, then a good frame must recover.
        for (size_t i = 0; i < bridge::kFrameSize; ++i)
            accepted += parser.push(frame[i] ^ (i == corrupt ? 0x80 : 0), 0, result);
        assert(accepted == 0);
        for (auto value : frame) accepted += parser.push(value, 1, result);
        assert(accepted == 1 && memcmp(result, rows, sizeof(rows)) == 0);

        bridge::FrameParser dropped;
        accepted = 0;
        for (size_t i = 0; i < bridge::kFrameSize; ++i)
            if (i != corrupt) accepted += dropped.push(frame[i], 0, result);
        for (auto value : frame) accepted += dropped.push(value, 1, result);
        assert(accepted == 1 && memcmp(result, rows, sizeof(rows)) == 0);

        bridge::FrameParser inserted;
        accepted = 0;
        for (size_t i = 0; i < bridge::kFrameSize; ++i) {
            if (i == corrupt) accepted += inserted.push(0x93, 0, result);
            accepted += inserted.push(frame[i], 0, result);
        }
        for (auto value : frame) accepted += inserted.push(value, 1, result);
        assert(accepted >= 1 && memcmp(result, rows, sizeof(rows)) == 0);
    }
    bridge::FrameParser timeout;
    uint8_t result[bridge::kRows] = {};
    for (size_t i = 0; i < 12; ++i) assert(!timeout.push(frame[i], 0, result));
    for (size_t i = 12; i < bridge::kFrameSize; ++i) assert(!timeout.push(frame[i], 30, result));
    bool received = false;
    for (auto value : frame) received |= timeout.push(value, 31, result);
    assert(received);
}

static void testDebounce() {
    bridge::Debouncer debounce;
    uint8_t raw[bridge::kRows] = {};
    raw[2] = 1;
    assert(!debounce.update(raw, 0xfffff000));
    assert(!debounce.update(raw, 0xfffffff0));
    assert(debounce.update(raw, 0x1000)); // microsecond counter rollover
    assert(debounce.rows()[2] == 1);
    raw[2] = 0;
    assert(!debounce.update(raw, 0x2000));
    raw[2] = 1;
    assert(!debounce.update(raw, 0x2100)); // contact bounced back
    raw[2] = 0;
    assert(!debounce.update(raw, 0x2200));
    assert(!debounce.update(raw, 0x2300));
    assert(debounce.update(raw, 0x4000));
    assert(debounce.rows()[2] == 0);
}

static void testMapping() {
    bridge::KeyMapper mapper;
    uint8_t rows[bridge::kRows] = {};
    rows[0] = 1; // First recovered entry: HID Q (0x14).
    auto report = mapper.map(rows);
    assert(report.keyboard[2] == 0x14);
    assert(report.keyboard[0] == 0 && report.consumer == 0);
    rows[0] = 0x7f; // Seven different keyboard usages -> ErrorRollOver.
    report = mapper.map(rows);
    for (size_t i = 2; i < 8; ++i) assert(report.keyboard[i] == 1);

    memset(rows, 0, sizeof(rows));
    unsigned modifiers = 0, consumer = 0;
    for (size_t i = 0; i < bridge::kRows * bridge::kColumns; ++i) {
        const auto key = bridge::kKeys[i];
        if (key.kind == 0x81 || key.kind == 2) {
            rows[i / 8] |= 1u << (i % 8);
            if (key.kind == 0x81) modifiers |= key.value;
            else consumer |= bridge::kConsumerMasks[key.value - 0xa0];
        }
    }
    report = mapper.map(rows);
    assert(modifiers == 0xff && report.keyboard[0] == 0xff);
    assert(consumer == 0x7f && report.consumer == 0x7f);

    memset(rows, 0, sizeof(rows));
    rows[122 / 8] = 1u << (122 % 8);
    report = mapper.map(rows);
    assert(report.keyboard[2] == 0x21); // Default alternate map: 4.
    memset(rows, 0, sizeof(rows));
    report = mapper.map(rows);
    const uint8_t released[8] = {};
    assert(memcmp(report.keyboard, released, 8) == 0 && report.consumer == 0);
}

static void testLongContactChatter() {
    // Openings longer than the previous 5 ms filter resemble the observed
    // release/press pairs. These are synthetic contact samples, not raw traces.
    constexpr uint8_t affected[] = {0x14, 0x19};
    for (const uint8_t usage : affected) {
        size_t key = 0;
        while (key < bridge::kRows * bridge::kColumns &&
               (bridge::kKeys[key].kind != 1 || bridge::kKeys[key].value != usage)) ++key;
        assert(key < bridge::kRows * bridge::kColumns);
        bridge::Debouncer<bridge::KeyboardDebounceTiming> tuned;
        bridge::Debouncer<> original;
        uint8_t raw[bridge::kRows] = {};
        const uint8_t mask = 1u << (key % 8);
        unsigned presses = 0, releases = 0, oldPresses = 0;
        bool last = false, oldLast = false;
        for (uint32_t ms = 0; ms <= 400; ++ms) {
            const bool contact = ms < 70 || (ms >= 92 && ms < 200) ||
                (ms >= 206 && ms < 220) || (ms >= 320 && ms < 350);
            raw[key / 8] = contact ? mask : 0;
            // Shift must retain 5 ms release timing while ordinary keys learn.
            raw[14] = ms >= 120 && ms < 140 ? 2 : 0;
            tuned.update(raw, ms * 1000); original.update(raw, ms * 1000);
            const bool held = tuned.rows()[key / 8] & mask;
            const bool oldHeld = original.rows()[key / 8] & mask;
            if (held && !last) { ++presses; assert(ms == 5 || ms == 325); }
            if (!held && last) { ++releases; assert(ms == 260 || ms == 390); }
            if (oldHeld && !oldLast) ++oldPresses;
            assert(bool(tuned.rows()[14] & 2) == (ms >= 125 && ms < 145));
            last = held; oldLast = oldHeld;
        }
        assert(presses == 2 && releases == 2 && oldPresses == 4);
    }
    const unsigned gaps[] = {39, 50};
    for (unsigned gap : gaps) {
        bridge::Debouncer<bridge::KeyboardDebounceTiming> taps;
        uint8_t raw[bridge::kRows] = {};
        unsigned presses = 0, releases = 0;
        bool previous = false;
        for (uint32_t ms = 0; ms <= 90 + gap; ++ms) {
            raw[0] = ms < 20 || (ms >= 20 + gap && ms < 40 + gap) ? 1 : 0;
            taps.update(raw, ms * 1000);
            const bool held = taps.rows()[0] & 1;
            if (held && !previous) ++presses;
            if (!held && previous) ++releases;
            previous = held;
        }
        // Preserve fast intentional taps with a 50 ms opening; explicitly
        // document the tradeoff that a 39 ms opening is treated as chatter.
        assert(presses == (gap == 50 ? 2u : 1u) && releases == presses);
    }
    bridge::Debouncer<bridge::KeyboardDebounceTiming> wrap;
    uint8_t raw[bridge::kRows] = {1}; // Physical Q key, independent of OS layout.
    wrap.update(raw, 0xffff0000u);
    assert(wrap.update(raw, 0xffff1388u));
    raw[0] = 0;
    wrap.update(raw, 0xffff9000u);
    assert(!wrap.update(raw, 0xffff9000u + 39999u));
    assert(wrap.update(raw, 0xffff9000u + 40000u));
    assert(!wrap.rows()[0]);
}

static void testAdaptiveDebounce() {
    bridge::Debouncer<bridge::KeyboardDebounceTiming> debounce;
    uint8_t raw[bridge::kRows] = {};
    uint32_t nowUs = 0;
    unsigned presses = 0, releases = 0;
    bool previous = false;
    const auto sample = [&](bool contact, unsigned durationMs) {
        raw[0] = contact ? 1 : 0; // Q; all other switches remain idle.
        for (unsigned ms = 0; ms < durationMs; ++ms) {
            debounce.update(raw, nowUs);
            const bool held = debounce.rows()[0] & 1;
            if (held && !previous) ++presses;
            if (!held && previous) ++releases;
            previous = held;
            nowUs += 1000;
        }
    };
    const auto cleanTap = [&] { sample(true, 20); sample(false, 100); };
    assert(debounce.releasePeriodUs(0) == 40000);
    // Training only follows completed clean cycles, not idle time or scans.
    sample(false, 1000);
    assert(debounce.releasePeriodUs(0) == 40000);
    for (unsigned group = 1; group <= 7; ++group) {
        for (unsigned cycle = 0; cycle < 15; ++cycle) cleanTap();
        assert(debounce.releasePeriodUs(0) == (45 - group * 5) * 1000);
        cleanTap();
        assert(debounce.releasePeriodUs(0) == (40 - group * 5) * 1000);
        assert(debounce.releasePeriodUs(1) == 40000); // Another ordinary key has not trained.
        assert(debounce.releasePeriodUs(113) == 5000); // Left Shift stays fast.
    }
    assert(presses == 112 && releases == 112);
    for (unsigned cycle = 0; cycle < 32; ++cycle) cleanTap();
    assert(debounce.releasePeriodUs(0) == 5000); // Lower bound.

    // A new, longer fault can escape the learned 5 ms filter once. The next
    // identical 22 ms opening must be suppressed by the new 30 ms window.
    unsigned before = presses;
    sample(true, 30); sample(false, 22); sample(true, 30); sample(false, 100);
    assert(presses == before + 2 && releases == presses);
    assert(debounce.releasePeriodUs(0) == 30000);
    before = presses;
    sample(true, 30); sample(false, 22); sample(true, 30); sample(false, 100);
    assert(presses == before + 1 && releases == presses);
    assert(debounce.releasePeriodUs(0) == 30000);

    // Chatter interrupts the quiet streak; it must not make the next clean
    // release reduce protection immediately.
    for (unsigned cycle = 0; cycle < 15; ++cycle) cleanTap();
    assert(debounce.releasePeriodUs(0) == 30000);
    sample(true, 30); sample(false, 22); sample(true, 30); sample(false, 100);
    cleanTap();
    assert(debounce.releasePeriodUs(0) == 30000);
    for (unsigned cycle = 0; cycle < 15; ++cycle) cleanTap();
    assert(debounce.releasePeriodUs(0) == 25000);

    // Increasing openings during a held key teach a larger window, bounded
    // at 60 ms. Press confirmation remains 5 ms even at maximum protection.
    before = presses;
    sample(true, 5);
    assert(presses == before);
    sample(true, 1);
    assert(presses == before + 1);
    const unsigned openings[] = {24, 31, 38, 45, 52, 59, 59};
    for (unsigned opening : openings) {
        sample(false, opening); sample(true, 20);
        assert(presses == before + 1 && releases == before);
        assert(debounce.releasePeriodUs(0) <= 60000);
    }
    assert(debounce.releasePeriodUs(0) == 60000);
    sample(false, 60);
    assert(releases == before);
    sample(false, 1);
    assert(releases == presses);
    sample(false, 100);
    before = presses;
    sample(true, 5);
    assert(presses == before);
    sample(true, 1);
    assert(presses == before + 1);
    sample(false, 100);
    assert(releases == presses);
    assert(debounce.releasePeriodUs(1) == 40000);

    // A new LPC instance starts protected; learning is deliberately volatile.
    bridge::Debouncer<bridge::KeyboardDebounceTiming> restarted;
    assert(restarted.releasePeriodUs(0) == 40000);
}

static void testQueue() {
    bridge::ReportQueue queue;
    bridge::Reports pressed;
    pressed.keyboard[2] = 4;
    queue.push(pressed);
    queue.push(pressed); // Heartbeat must not consume queue space.
    queue.push(bridge::Reports{});
    assert(queue.front().keyboard[2] == 4);
    queue.pop();
    assert(queue.front().keyboard[2] == 0);
    queue.pop();
    assert(queue.empty());
    for (uint8_t code = 4; code < 40; ++code) {
        pressed.keyboard[2] = code;
        queue.push(pressed);
    }
    bridge::Reports last;
    while (!queue.empty()) { last = queue.front(); queue.pop(); }
    assert(last.keyboard[2] == 39);
    queue.push(pressed);
    queue.reset(bridge::Reports{}); // Link loss discards stale pending presses.
    assert(queue.front().keyboard[2] == 0);
    queue.pop();
    assert(queue.empty());
}

int main() {
    testWireRecovery();
    testDebounce();
    testLongContactChatter();
    testAdaptiveDebounce();
    testMapping();
    testQueue();
    puts("PASS: CRC, stream recovery, adaptive per-key debounce, counter rollover, key/media mapping, HID rollover and event queue");
}
