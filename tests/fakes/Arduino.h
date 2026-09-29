#pragma once
#include <stdint.h>
#include <stddef.h>
constexpr unsigned LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2;
void pinMode(unsigned pin, unsigned mode);
void digitalWrite(unsigned pin, unsigned value);
int digitalRead(unsigned pin);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
uint32_t millis();
constexpr unsigned PIN_EXT_POWER = 13, PIN_SERIAL1_RX = 8, PIN_SERIAL1_TX = 11;
class FakeUart {
public:
    void begin(unsigned long baud);
    void end();
    size_t write(const uint8_t* data, size_t length);
};
extern FakeUart Serial1;
