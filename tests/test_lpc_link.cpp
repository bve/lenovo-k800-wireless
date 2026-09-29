#include <assert.h>
#include <stdio.h>
#include <set>
#include "LpcLink.hpp"
#include "SwdPort.hpp"
#include "bridge/Protocol.hpp"

namespace {
bool power = false, uart = false;
bool txHigh = false, txOutput = false;
unsigned starts = 0, stops = 0;
uint32_t fakeNow = 0, currentBaud = 0;
std::set<unsigned> isolated;
const std::set<unsigned> targetPins = {8, 11, 17};
}
FakeUart Serial1;
void FakeUart::begin(unsigned long baud) {
    assert(power && !uart && (baud == bridge::kBaud || baud == bridge::kLegacyBaud) && txHigh && txOutput);
    currentBaud = baud;
    uart = true; ++starts; isolated.erase(11); isolated.erase(8);
}
void FakeUart::end() {
    assert(power && uart); // No double end() on Arduino's UARTE driver.
    uart = false; ++stops;
}
void nrf_gpio_cfg_default(unsigned pin) {
    assert(!uart); // Isolate only after the peripheral stopped driving its pins.
    if (pin == PIN_SERIAL1_TX) txOutput = false;
    isolated.insert(pin);
}
void pinMode(unsigned pin, unsigned mode) {
    if (pin == PIN_SERIAL1_TX) {
        assert(power && txHigh && mode == OUTPUT);
        txOutput = true;
    }
    if (pin != PIN_EXT_POWER) isolated.erase(pin);
    else assert(mode == OUTPUT);
}
void digitalWrite(unsigned pin, unsigned value) {
    if (pin == SwdPort::kReset) { assert(power && !uart && value == LOW); return; }
    if (pin == PIN_SERIAL1_TX) {
        assert(power && !uart && value == HIGH);
        txHigh = true;
        return;
    }
    assert(pin == PIN_EXT_POWER);
    if (value == LOW) assert(!uart && isolated == targetPins);
    power = value;
}
void delay(unsigned long ms) { assert(ms == 10 && power && !uart); }
uint32_t millis() { return fakeNow; }

int main() {
    LpcLink link;
    link.begin();
    assert(!power && !link.powered() && starts == 0 && stops == 0);
    link.poll(0, false);
    link.poll(100, true);
    assert(power && !uart);
    link.poll(149, true);
    assert(!uart);
    link.poll(150, true);
    assert(uart && link.uartRunning());
    link.poll(160, false);
    assert(!power && !uart && link.powerOffs() == 1 && stops == 1);
    link.poll(170, false);
    assert(link.powerOffs() == 1 && stops == 1);
    // Disconnect during rail settling must not call Uart::end before begin.
    link.poll(180, true); link.poll(181, false);
    assert(!power && stops == 1 && link.powerOffs() == 2);
    // Millisecond rollover still waits the full 50 ms.
    link.poll(0xfffffff0u, true); link.poll(33, true);
    assert(!uart);
    fakeNow = 34; link.poll(34, true); assert(uart);
    // A COMMIT owns power until the full programming/verification completes.
    link.beginProgramming();
    assert(power && !uart && isolated == targetPins);
    link.poll(100, false);
    assert(power && link.powered());
    link.endProgramming(false);
    assert(!power && !uart && link.powerOffs() == 3);
    // With a live connection SWD returns the pins to UART, without a power cut.
    link.poll(200, true); link.poll(250, true);
    link.beginProgramming(); link.endProgramming(true);
    assert(power && uart && link.powerOffs() == 3);
    link.prepareSleep();
    assert(power && !uart && link.powered() && !link.uartRunning() && isolated == targetPins);
    link.poll(280, false); // A BLE disconnect must not cut power to the wake detector.
    assert(power && !uart && link.powerOffs() == 3);
    link.restart(); // Lost sleep ACK: pulse RESET and recover UART with the rail on.
    assert(power && uart);
    assert(link.baud() == bridge::kBaud);
    link.poll(fakeNow + 3499, true); assert(link.baud() == bridge::kBaud);
    link.poll(fakeNow + 3500, true); assert(link.baud() == bridge::kLegacyBaud);
    link.receivedFrame(fakeNow + 3510); // Old LPC remains usable during a staged OTA.
    link.poll(fakeNow + 4509, true); assert(link.baud() == bridge::kLegacyBaud);
    link.poll(fakeNow + 4510, true); assert(link.baud() == bridge::kBaud);
    link.poll(fakeNow + 4910, true); assert(link.baud() == bridge::kLegacyBaud);
    link.receivedFrame(0xfffffff0u);
    link.poll(983, true); assert(link.baud() == bridge::kLegacyBaud);
    link.poll(984, true); assert(link.baud() == bridge::kBaud && currentBaud == link.baud());
    link.poll(300, false);
    assert(link.baud() == 0);
    assert(starts == stops && !power && isolated == targetPins);
    puts("PASS: LPC power sequencing, pin isolation, reconnect, SWD retention, dual-baud recovery and timer wrap");
}
