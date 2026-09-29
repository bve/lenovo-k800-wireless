#pragma once
#include <stdint.h>

// Target operations used by the ROM-IAP programmer, independently of SWD pins.
class DebugTarget {
public:
    virtual ~DebugTarget() = default;
    virtual bool attach() = 0;
    virtual uint32_t diagnostic() const = 0;
    virtual void release(bool restart) = 0;
    virtual bool readMemory(uint32_t address, uint32_t& value) = 0;
    virtual bool writeMemory(uint32_t address, uint32_t value) = 0;
    virtual bool writeRegister(uint8_t reg, uint32_t value) = 0;
    virtual bool waitHalted(uint32_t timeoutMs) = 0;
};
