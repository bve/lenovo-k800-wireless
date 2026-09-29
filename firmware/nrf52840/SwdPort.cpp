#include "SwdPort.hpp"

void SwdPort::reset(bool asserted) {
    // Preload LOW, then enable output. Never drive the target reset line HIGH.
    digitalWrite(kReset, LOW);
    pinMode(kReset, asserted ? OUTPUT : INPUT);
}
void SwdPort::output(bool enabled) {
    pinMode(kData, enabled ? OUTPUT : INPUT_PULLUP);
}
bool SwdPort::clock() {
    // Match ARM CMSIS-DAP: sample after the falling edge, before rising.
    // Leave SWCLK HIGH so direction changes precede the next falling edge.
    digitalWrite(kClock, LOW);
    delayMicroseconds(5);
    const bool value = digitalRead(kData);
    digitalWrite(kClock, HIGH);
    delayMicroseconds(5);
    return value;
}
void SwdPort::bits(uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        digitalWrite(kData, (value >> i) & 1);
        clock();
    }
}
uint32_t SwdPort::bits(unsigned count) {
    uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) if (clock()) value |= 1u << i;
    return value;
}

bool SwdPort::transfer(bool ap, bool read, uint8_t address, uint32_t& value) {
    const uint8_t fields = uint8_t(ap) | uint8_t(read) << 1 | (address & 0xc);
    const uint8_t request = 0x81 | fields << 1 | __builtin_parity(fields) << 5;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        bits(request, 8);
        output(false);
        clock(); // Host -> target turnaround, one clock.
        const uint32_t ack = bits(3);
        bool parityOk = true;
        if (ack == 1 && read) {
            value = bits(32);
            parityOk = bits(1) == unsigned(__builtin_parity(value));
        }
        if (ack != 1 && ack != 2 && ack != 4)
            for (unsigned i = 0; i < 33; ++i) clock(); // Back off a malformed reply.
        clock(); // Target -> host turnaround (also for WAIT/FAULT).
        digitalWrite(kData, HIGH);
        output(true);
        if (ack == 1 && !read) {
            bits(value, 32);
            bits(__builtin_parity(value), 1);
        }
        bits(0, 8); // Finish posted AP writes, ORUNDETECT is disabled.
        digitalWrite(kData, HIGH);
        if (ack == 1) return parityOk || fail(2, request, ack);
        if (ack != 2) return fail(1, request, ack);
        delay(1); // Bounded WAIT retry; keep the BLE/RTOS scheduler running.
    }
    return fail(3, request, 2);
}
bool SwdPort::apRead(uint8_t address, uint32_t& value) {
    uint32_t posted;
    return transfer(true, true, address, posted) && transfer(false, true, 0xc, value);
}
bool SwdPort::flush() {
    uint32_t value;
    if (!transfer(false, true, 0xc, value) || !transfer(false, true, 4, value)) return false;
    return !(value & 0xb2) || fail(6, 0, value & 0xb2);
}
bool SwdPort::readMemory(uint32_t address, uint32_t& value) {
    return apWrite(4, address) && apRead(0xc, value) && flush();
}
bool SwdPort::writeMemory(uint32_t address, uint32_t value) {
    return apWrite(4, address) && apWrite(0xc, value) && flush();
}
bool SwdPort::waitHalted(uint32_t timeoutMs) {
    const uint32_t start = millis();
    do {
        uint32_t dhcsr;
        if (!readMemory(0xe000edf0, dhcsr)) return false;
        if (dhcsr & (1u << 17)) return true;
        delay(1);
    } while (millis() - start < timeoutMs);
    return fail(5);
}
bool SwdPort::waitRegisterReady() {
    for (unsigned i = 0; i < 50; ++i) {
        uint32_t dhcsr;
        if (!readMemory(0xe000edf0, dhcsr)) return false;
        if (dhcsr & (1u << 16)) return true;
        delay(1);
    }
    return fail(5);
}
bool SwdPort::writeRegister(uint8_t reg, uint32_t value) {
    return waitRegisterReady() && writeMemory(0xe000edf8, value) &&
           writeMemory(0xe000edf4, 0x10000 | reg) && waitRegisterReady();
}
bool SwdPort::attach() {
    diagnostic_ = 0;
    phase_ = 1;
    reset(true);
    digitalWrite(kClock, LOW);
    pinMode(kClock, OUTPUT);
    digitalWrite(kData, HIGH);
    output(true);
    delay(20);
    // LPC11U1x selects boundary scan while RESET is LOW; SWD requires HIGH.
    // Attach during the application's three-second window before SWDIO is TX.
    reset(false);
    delay(20); // Allow target RESET pull-up and boot ROM pin setup to settle.
    bits(0xffffffff, 32); bits(0xffffffff, 32);
    bits(0xe79e, 16); // JTAG-to-SWD selection, least significant bit first.
    bits(0xffffffff, 32); bits(0xffffffff, 32); bits(0, 8);
    uint32_t id;
    if (!transfer(false, true, 0, id)) return false;
    if ((id & 0x0fffffff) != 0x0bb11477) return fail(4); // LPC11U13 Cortex-M0 SW-DP.
    phase_ = 2;
    if (!dpWrite(0, 0x1e) || !dpWrite(8, 0) || !dpWrite(4, 0x50000000)) return false;
    phase_ = 3;
    bool powered = false;
    for (unsigned i = 0; i < 100; ++i) {
        uint32_t status;
        if (!transfer(false, true, 4, status)) return false;
        if ((status & 0xa0000000) == 0xa0000000) { powered = true; break; }
        delay(1);
    }
    if (!powered) return fail(5);
    phase_ = 4;
    // 32-bit MEM-AP access, no address auto-increment, privileged debug access.
    if (!apWrite(0, 0x23000002) || !flush()) return false;
    phase_ = 5;
    if (!writeMemory(0xe000edf0, 0xa05f0003)) return false;
    phase_ = 6;
    if (!waitHalted(500) || !writeMemory(0xe000edfc, 0)) return false;
    phase_ = 7;
    return true;
}
void SwdPort::release(bool restart) {
    if (restart) reset(true);
    output(false);
    pinMode(kClock, INPUT);
    if (restart) delay(20);
    reset(false);
}
