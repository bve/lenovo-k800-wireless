#pragma once
#include <Arduino.h>
#include "DebugTarget.hpp"

// Raw Nordic port numbers; variant.cpp uses identity mapping. These are GPIOs
// on the SuperMini, NOT its own SWD pads. Do not attach another SWD master.
class SwdPort : public DebugTarget {
public:
    static constexpr unsigned kData = 8;    // Shared with the LPC keyboard UART RX.
    static constexpr unsigned kClock = 11;
    static constexpr unsigned kReset = 17;  // Open drain, external target pull-up.
    bool attach() override;
    uint32_t diagnostic() const override { return diagnostic_; }
    void release(bool restart) override;
    bool readMemory(uint32_t address, uint32_t& value) override;
    bool writeMemory(uint32_t address, uint32_t value) override;
    bool writeRegister(uint8_t reg, uint32_t value) override;
    bool waitHalted(uint32_t timeoutMs) override;
private:
    // Error detail: phase:8, reason:8, SWD request:8, ACK/status:8.
    // Phases: ID, DP setup, power, AP setup, halt request, halt wait, memory access.
    // Reasons: ACK, parity, WAIT limit, wrong ID, timeout, sticky AP error.
    bool fail(uint8_t reason, uint8_t request = 0, uint8_t status = 0) {
        diagnostic_ = uint32_t(phase_) << 24 | uint32_t(reason) << 16 |
                      uint32_t(request) << 8 | status;
        return false;
    }
    uint32_t diagnostic_ = 0;
    uint8_t phase_ = 7;
    void reset(bool asserted);
    void output(bool enabled);
    bool clock();
    void bits(uint32_t value, unsigned count);
    uint32_t bits(unsigned count);
    bool transfer(bool ap, bool read, uint8_t address, uint32_t& value);
    bool dpWrite(uint8_t address, uint32_t value) { return transfer(false, false, address, value); }
    bool apWrite(uint8_t address, uint32_t value) { return transfer(true, false, address, value); }
    bool apRead(uint8_t address, uint32_t& value);
    bool flush();
    bool waitRegisterReady();
};
