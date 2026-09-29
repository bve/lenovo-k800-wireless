#include "LpcProgrammer.hpp"
#include <Arduino.h>

namespace {
constexpr uint32_t kCode = 0x10000000, kCommand = 0x10000040;
constexpr uint32_t kResult = 0x10000060, kBuffer = 0x10000100;
}
bool LpcProgrammer::initialize() {
    uint32_t power, clocks;
    return swd_.readMemory(0x40048238, power) && swd_.writeMemory(0x40048238, power & ~3u) &&
           swd_.writeMemory(0x40048070, 0) && swd_.writeMemory(0x40048074, 0) &&
           swd_.writeMemory(0x40048074, 1) && swd_.writeMemory(0x40048078, 1) &&
           swd_.readMemory(0x40048080, clocks) && swd_.writeMemory(0x40048080, clocks | 0x1f) &&
           swd_.writeMemory(0x40048000, 2) && // Map actual Flash at address zero.
           swd_.writeMemory(0xe000e010, 0) && swd_.writeMemory(0xe000e180, 0xffffffff) &&
           swd_.writeMemory(kCode, 0xbe00be00); // BKPT return trampoline in SRAM.
}
bool LpcProgrammer::iap(uint32_t command, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const uint32_t parameters[] = {command, a, b, c, d};
    for (unsigned i = 0; i < 5; ++i)
        if (!swd_.writeMemory(kCommand + 4 * i, parameters[i])) return false;
    // PRIMASK=1, CONTROL=0; ROM uses MSP. Reserve the top 32 bytes for ROM IAP.
    if (!swd_.writeMemory(kResult, 0xffffffff) || !swd_.writeRegister(20, 1) ||
        !swd_.writeRegister(17, 0x10000fe0) || !swd_.writeRegister(0, kCommand) ||
        !swd_.writeRegister(1, kResult) || !swd_.writeRegister(14, kCode | 1) ||
        !swd_.writeRegister(16, 0x01000000) || !swd_.writeRegister(15, 0x1fff1ff0) ||
        !swd_.writeMemory(0xe000edf0, 0xa05f0001) || !swd_.waitHalted(3000) ||
        !swd_.readMemory(kResult, detail_)) return false;
    return detail_ == 0;
}
bridge::UpdateError LpcProgrammer::program(const uint8_t* image, uint32_t size,
                                         Progress progress, void* context) {
    using bridge::UpdateError;
    auto error = bridge::validateLpcImage(image, size);
    if (error != UpdateError::None) return error;
    detail_ = 0;
    // Every exit releases GPIO. An incomplete image remains recoverable through
    // hardware reset/SWD; no CRP or target configuration fuses are changed.
    do {
        error = UpdateError::DebugPort;
        if (!swd_.attach()) break;
        uint32_t target;
        if (!swd_.readMemory(0x400483f4, target)) break;
        if (target != 0x297a802b && target != 0x097a802b) {
            error = UpdateError::Target; detail_ = target; break;
        }
        if (!initialize()) break;
        error = UpdateError::Iap;
        // Erase all 24K, including any stale factory settings at the end.
        if (!iap(50, 0, 5)) break;
        if (!iap(52, 0, 5, 12000) || !iap(53, 0, 5)) break;
        bool failed = false;
        const uint32_t pages = (size + 255) / 256;
        // Write the vector page LAST. An interrupted image has no valid vectors.
        for (uint32_t page = 1; page <= pages; ++page) {
            const uint32_t address = (page == pages ? 0 : page * 256);
            for (uint32_t i = 0; i < 256; i += 4) {
                const uint32_t word = address + i < size ? bridge::read32(image + address + i) : 0xffffffff;
                if (!swd_.writeMemory(kBuffer + i, word)) { failed = true; break; }
            }
            if (failed || !iap(50, address / 4096, address / 4096) ||
                !iap(51, address, kBuffer, 256, 12000)) { failed = true; break; }
            progress(page * 256 < size ? page * 256 : size, context);
            delay(1);
        }
        if (failed) break;
        error = UpdateError::Verify;
        // Verify the whole Flash, including erased bytes beyond the file.
        for (uint32_t address = 0; address < bridge::kLpcFlashSize; address += 4) {
            uint32_t actual;
            const uint32_t expected = address < size ? bridge::read32(image + address) : 0xffffffff;
            if (!swd_.readMemory(address, actual) || actual != expected) {
                detail_ = address; failed = true; break;
            }
            if (!(address & 255)) delay(1);
        }
        if (failed) break;
        error = UpdateError::None;
    } while (false);
    if (error == UpdateError::DebugPort) detail_ = swd_.diagnostic();
    // Clear debug state before the final hardware reset, also on failure.
    swd_.writeMemory(0xe000edfc, 0);
    swd_.writeMemory(0xe000edf0, 0xa05f0000);
    swd_.release(true);
    return error;
}
