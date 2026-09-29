#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bridge {
constexpr uint32_t kLpcFlashSize = 24 * 1024;

inline uint32_t read32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void write32(uint8_t* p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8 * i);
}
inline uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

enum class UpdateError : uint8_t {
    None, Command, Busy, Size, Offset, Incomplete, Crc, Vectors, Protection,
    DebugPort, Target, Iap, Verify, Disconnected
};
enum class UpdateState : uint8_t { Idle, Receiving, Pending, Programming, Done, Failed };

inline UpdateError validateLpcImage(const uint8_t* data, uint32_t size) {
    if (size < 0x300 || size > kLpcFlashSize || (size & 3)) return UpdateError::Size;
    const uint32_t sp = read32(data), reset = read32(data + 4);
    uint32_t sum = 0;
    for (unsigned i = 0; i < 8; ++i) sum += read32(data + 4 * i);
    if (sum || sp <= 0x10000000 || sp > 0x10001000 || (sp & 7) ||
        !(reset & 1) || (reset & ~1u) < 0x20 || (reset & ~1u) >= size)
        return UpdateError::Vectors;
    // Whitelist unprotected words used by our firmware and the saved Lenovo image.
    const uint32_t crp = read32(data + 0x2fc);
    if (crp != 0xffffffff && crp != 0x1a0888a1) return UpdateError::Protection;
    return UpdateError::None;
}

// Fully stage an image before the first destructive operation. The BLE service
// owns synchronization; this class has no radio or hardware dependencies.
class LpcImageBuffer {
public:
    UpdateError begin(uint32_t size, uint32_t crc) {
        if (size < 0x300 || size > kLpcFlashSize || (size & 3)) return UpdateError::Size;
        size_ = size; crc_ = crc; received_ = 0;
        return UpdateError::None;
    }
    UpdateError append(uint32_t offset, const uint8_t* data, uint32_t count) {
        if (!count || offset != received_) return UpdateError::Offset;
        if (count > size_ - received_) return UpdateError::Size;
        memcpy(data_ + offset, data, count);
        received_ += count;
        return UpdateError::None;
    }
    UpdateError validate() const {
        if (!size_ || received_ != size_) return UpdateError::Incomplete;
        if (crc32(data_, size_) != crc_) return UpdateError::Crc;
        return validateLpcImage(data_, size_);
    }
    void clear() { size_ = crc_ = received_ = 0; }
    const uint8_t* data() const { return data_; }
    uint32_t size() const { return size_; }
    uint32_t expectedCrc() const { return crc_; }
    uint32_t received() const { return received_; }
private:
    alignas(4) uint8_t data_[kLpcFlashSize] = {};
    uint32_t size_ = 0, crc_ = 0, received_ = 0;
};
}
