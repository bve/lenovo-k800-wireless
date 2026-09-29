#pragma once
#include <Arduino.h>
#include <atomic>
#include "bridge/SaveSnapshot.hpp"

// Files use a private directory; BLE bonds and bootloader state are untouched.
class KeyboardStore {
public:
    void begin(bridge::FactoryKeyboard& keyboard, bridge::HostProfiles& hosts);
    void poll(uint32_t now, bridge::FactoryKeyboard& keyboard, bridge::HostProfiles& hosts, bool idle);
    bool healthy() const { return !corrupt_ && !ioError_; }
    bool idle() const { return result_.load() == Result::Idle; }
private:
    enum class Result : uint8_t { Idle, Running, Saved, Failed };
    static void worker(void* context);
    static void path(unsigned slot, char* output, size_t size);
    static int read(const char* filename, uint8_t* data, size_t capacity);
    static bool replace(const char* filename, const uint8_t* data, size_t length);
    uint32_t lastAttempt_ = 0;
    TaskHandle_t task_ = nullptr;
    bridge::SaveSnapshot pending_;
    std::atomic<Result> result_{Result::Idle};
    bool corrupt_ = false, ioError_ = false;
};
