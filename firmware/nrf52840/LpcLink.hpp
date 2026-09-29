#pragma once
#include <stdint.h>

// Owns the switched LPC rail and the UART/SWD pin handover. Only the Arduino
// task changes this state; BLE callbacks only wake that task.
class LpcLink {
public:
    void begin();
    void poll(uint32_t now, bool required);
    bool powered() const { return state_ != State::Off; }
    bool uartRunning() const { return state_ == State::Uart; }
    uint32_t baud() const { return uartRunning() ? baud_ : 0; }
    void receivedFrame(uint32_t now) { lastFrame_ = now; probeAfter_ = 1000; }
    uint16_t powerOffs() const { return powerOffs_; }
    void beginProgramming();
    void endProgramming(bool connected);
    void prepareSleep();
    void restart();
private:
    enum class State { Off, Starting, Uart, Programming, Sleeping };
    void startUart();
    void configureUart();
    void powerOff();
    static void isolatePins();
    State state_ = State::Off;
    uint32_t started_ = 0;
    uint32_t baud_ = 0, lastFrame_ = 0, probeAfter_ = 0;
    uint16_t powerOffs_ = 0;
};
