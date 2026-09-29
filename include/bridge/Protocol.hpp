#pragma once
#include <stddef.h>
#include <stdint.h>

namespace bridge {
constexpr size_t kRows = 18;
constexpr size_t kColumns = 8;
constexpr size_t kFrameSize = 24;
constexpr size_t kStatusFrameSize = 26;
constexpr uint32_t kBaud = 115200;
constexpr uint32_t kLegacyBaud = 38400;
constexpr uint8_t kSleepSupported = 8, kSleepReady = 16;

inline uint16_t crc16(const uint8_t* data, size_t size) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

inline void encode(const uint8_t* rows, uint8_t sequence, uint8_t* frame) {
    frame[0] = 0x4b;
    frame[1] = 0x42;
    frame[2] = 1;
    frame[3] = sequence;
    for (size_t i = 0; i < kRows; ++i) frame[4 + i] = rows[i];
    const uint16_t crc = crc16(frame, kFrameSize - 2);
    frame[22] = crc & 0xff;
    frame[23] = crc >> 8;
}

inline void encodeStatus(const uint8_t* rows, uint8_t sequence, uint8_t acknowledged,
                         uint8_t flags, uint8_t* frame) {
    encode(rows, sequence, frame);
    frame[2] = 2;
    frame[22] = acknowledged; frame[23] = flags;
    const uint16_t crc = crc16(frame, 24);
    frame[24] = crc; frame[25] = crc >> 8;
}

class FrameParser {
public:
    bool push(uint8_t value, uint32_t nowMs, uint8_t* rows) {
        if (used_ && nowMs - lastByteMs_ > 20) used_ = 0;
        lastByteMs_ = nowMs;
        data_[used_++] = value;
        while (used_ >= 3) {
            if (data_[0] == 0x4b && data_[1] == 0x42 && (data_[2] == 1 || data_[2] == 2)) {
                const unsigned length = data_[2] == 1 ? kFrameSize : kStatusFrameSize;
                if (used_ < length) return false;
                const unsigned received = data_[length - 2] | unsigned(data_[length - 1]) << 8;
                if (received == crc16(data_, length - 2)) {
                    for (size_t i = 0; i < kRows; ++i) rows[i] = data_[i + 4];
                    version_ = data_[2];
                    ack_ = version_ == 2 ? data_[22] : 0;
                    flags_ = version_ == 2 ? data_[23] : 0;
                    discard(length);
                    return true;
                }
            }
            discard(1);
        }
        return false;
    }
    uint8_t version() const { return version_; }
    uint8_t acknowledged() const { return ack_; }
    uint8_t flags() const { return flags_; }

private:
    void discard(size_t count) {
        for (size_t i = count; i < used_; ++i) data_[i - count] = data_[i];
        used_ -= count;
    }
    uint8_t data_[kStatusFrameSize] = {};
    uint8_t version_ = 0, ack_ = 0, flags_ = 0;
    size_t used_ = 0;
    uint32_t lastByteMs_ = 0;
};
}
