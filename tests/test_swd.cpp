#include <assert.h>
#include <deque>
#include <stdio.h>
#include "SwdPort.hpp"

namespace {
struct Edge { bool output, level; };
std::deque<Edge> wire;
unsigned direction = OUTPUT, dataLevel = 1;
bool sample = false;
bool resetAsserted = false;
uint32_t elapsed = 0;
void host(uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) wire.push_back({true, bool((value >> i) & 1)});
}
void target(uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) wire.push_back({false, bool((value >> i) & 1)});
}
void transfer(bool ap, bool read, uint8_t address, uint32_t value, uint8_t ack = 1, bool badParity = false) {
    unsigned fields = unsigned(ap) | unsigned(read) << 1 | (address & 12);
    host(0x81 | fields << 1 | unsigned(__builtin_parity(fields)) << 5, 8);
    target(0, 1); target(ack, 3);
    if (ack == 1 && read) { target(value, 32); target(__builtin_parity(value) ^ badParity, 1); }
    if (ack != 1 && ack != 2 && ack != 4) { target(0, 32); target(0, 1); }
    target(0, 1);
    if (ack == 1 && !read) { host(value, 32); host(__builtin_parity(value), 1); }
    host(0, 8);
}
}
void pinMode(unsigned pin, unsigned mode) {
    if (pin == SwdPort::kData) direction = mode;
    if (pin == SwdPort::kReset) resetAsserted = mode == OUTPUT;
}
void digitalWrite(unsigned pin, unsigned value) {
    if (pin == SwdPort::kData) dataLevel = value;
    if (pin == SwdPort::kClock && value == LOW && !wire.empty()) sample = wire.front().level;
    if (pin == SwdPort::kClock && value == HIGH) {
        assert(!resetAsserted); // LPC11U1x disables SWD while RESET is asserted.
        assert(!wire.empty());
        const auto edge = wire.front(); wire.pop_front();
        assert(edge.output == (direction == OUTPUT));
        if (edge.output) assert(edge.level == bool(dataLevel));
    }
}
int digitalRead(unsigned) { return sample; }
void delay(unsigned long ms) { elapsed += ms * 1000; }
void delayMicroseconds(unsigned int us) { elapsed += us; }
uint32_t millis() { return elapsed / 1000; }

int main() {
    SwdPort port;
    uint32_t value;
    // Posted AP read, followed by DP RDBUFF and sticky-error check.
    transfer(true, false, 4, 0x400483f4);
    transfer(true, true, 12, 0xdeadbeef);
    transfer(false, true, 12, 0x297a802b);
    transfer(false, true, 12, 0x297a802b);
    transfer(false, true, 4, 0xf0000000);
    assert(port.readMemory(0x400483f4, value) && value == 0x297a802b && wire.empty());
    // WAIT retries the same transfer without a data phase.
    transfer(true, false, 4, 0x10000000, 2);
    transfer(true, false, 4, 0x10000000);
    transfer(true, false, 12, 0xbe00be00);
    transfer(false, true, 12, 0);
    transfer(false, true, 4, 0xf0000000);
    assert(port.writeMemory(0x10000000, 0xbe00be00) && wire.empty());
    transfer(true, false, 4, 0x10000000, 4);
    assert(!port.writeMemory(0x10000000, 0) && wire.empty());
    assert((port.diagnostic() & 0xffff00ff) == 0x07010004);
    transfer(true, false, 4, 0x10000000);
    transfer(true, true, 12, 0, 1, true);
    assert(!port.readMemory(0x10000000, value) && wire.empty());
    assert((port.diagnostic() >> 16) == 0x0702);
    transfer(true, false, 4, 0x10000000);
    transfer(true, false, 12, 0);
    transfer(false, true, 12, 0);
    transfer(false, true, 4, 0xf0000020); // STICKYERR must propagate.
    assert(!port.writeMemory(0x10000000, 0) && wire.empty());
    for (unsigned i = 0; i < 100; ++i) transfer(true, false, 4, 0, 2);
    assert(!port.writeMemory(0, 0) && wire.empty());
    assert((port.diagnostic() & 0xffff00ff) == 0x07030002);
    // An open or unpowered target returns no valid ID ACK; preserve that cause.
    for (uint8_t ack : {uint8_t(7), uint8_t(0)}) {
        host(0xffffffff, 32); host(0xffffffff, 32); host(0xe79e, 16);
        host(0xffffffff, 32); host(0xffffffff, 32); host(0, 8);
        transfer(false, true, 0, 0, ack);
        assert(!port.attach() && wire.empty());
        assert(port.diagnostic() == (0x0101a500u | ack));
    }
    puts("PASS: SWD wire directions, request/data parity, posted reads, WAIT/FAULT and retry bounds");
}
