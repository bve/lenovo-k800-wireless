#include "LpcLink.hpp"
#include "SwdPort.hpp"
#include "bridge/Protocol.hpp"
#include <nrf_gpio.h>

void LpcLink::isolatePins() {
    // No pull-ups, output drivers or input buffers on the unpowered target.
    const unsigned pins[] = {SwdPort::kData, SwdPort::kClock, SwdPort::kReset};
    for (unsigned pin : pins) nrf_gpio_cfg_default(pin);
}
void LpcLink::begin() {
    isolatePins();
    digitalWrite(PIN_EXT_POWER, LOW);
    pinMode(PIN_EXT_POWER, OUTPUT);
}
void LpcLink::startUart() {
    baud_ = bridge::kBaud;
    lastFrame_ = millis();
    probeAfter_ = 3500; // LPC leaves SWD available for three seconds after reset.
    configureUart();
}
void LpcLink::configureUart() {
    isolatePins();
    pinMode(SwdPort::kReset, INPUT);
    digitalWrite(PIN_SERIAL1_TX, HIGH);
    pinMode(PIN_SERIAL1_TX, OUTPUT);
    Serial1.begin(baud_);
    pinMode(PIN_SERIAL1_RX, INPUT_PULLUP);
    state_ = State::Uart;
}
void LpcLink::powerOff() {
    if (state_ == State::Off) return;
    // Uart::end waits for STOPRX/STOPTX before disabling UARTE and its clock.
    // Never call it before begin(), or while it is already stopped for SWD.
    if (state_ == State::Uart) Serial1.end();
    isolatePins();
    digitalWrite(PIN_EXT_POWER, LOW);
    state_ = State::Off;
    ++powerOffs_;
}
void LpcLink::poll(uint32_t now, bool required) {
    if (state_ == State::Programming || state_ == State::Sleeping) return;
    if (!required) { powerOff(); return; }
    if (state_ == State::Off) {
        digitalWrite(PIN_EXT_POWER, HIGH);
        started_ = now;
        state_ = State::Starting;
    }
    if (state_ == State::Starting && now - started_ >= 50) startUart();
    if (state_ == State::Uart && now - lastFrame_ >= probeAfter_) {
        // A CRC-valid frame locks the rate. Probe both rates after a lost link,
        // including when only one of the two boards has been updated over BLE.
        Serial1.end();
        baud_ = baud_ == bridge::kBaud ? bridge::kLegacyBaud : bridge::kBaud;
        lastFrame_ = now;
        probeAfter_ = 400;
        configureUart();
    }
}
void LpcLink::beginProgramming() {
    if (state_ != State::Uart) return;
    Serial1.end();
    isolatePins();
    state_ = State::Programming;
}
void LpcLink::endProgramming(bool connected) {
    if (state_ != State::Programming) return;
    if (connected) startUart();
    else powerOff();
}
void LpcLink::prepareSleep() {
    if (state_ != State::Uart) return;
    Serial1.end();
    isolatePins();
    // LPC's RX has a pull-up; do not leave an input floating or back-power it.
    // The rail MUST stay high: LPC detects the matrix key while nRF is off.
    digitalWrite(PIN_EXT_POWER, HIGH);
    state_ = State::Sleeping;
}
void LpcLink::restart() {
    if (state_ == State::Uart) Serial1.end();
    isolatePins();
    digitalWrite(SwdPort::kReset, LOW);
    pinMode(SwdPort::kReset, OUTPUT);
    delay(10);
    pinMode(SwdPort::kReset, INPUT);
    startUart();
}
