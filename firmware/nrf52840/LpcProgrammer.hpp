#pragma once
#include "DebugTarget.hpp"
#include "bridge/LpcImage.hpp"

class LpcProgrammer {
public:
    explicit LpcProgrammer(DebugTarget& swd) : swd_(swd) {}
    using Progress = void (*)(uint32_t bytes, void* context);
    bridge::UpdateError program(const uint8_t* image, uint32_t size, Progress progress, void* context);
    uint32_t detail() const { return detail_; }
private:
    bool initialize();
    bool iap(uint32_t command, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0);
    DebugTarget& swd_;
    uint32_t detail_ = 0;
};
