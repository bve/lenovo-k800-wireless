#pragma once
#include "WVariant.h"

#define VARIANT_MCK 64000000ul
// Internal LF clock avoids assuming a 32.768 kHz crystal on a clone board.
#define USE_LFRC
#define PINS_COUNT 48
#define NUM_DIGITAL_PINS 48
#define NUM_ANALOG_INPUTS 0
#define NUM_ANALOG_OUTPUTS 0
#define ADC_RESOLUTION 14
#define LED_BUILTIN 255
#define LED_BLUE LED_BUILTIN
#define LED_STATE_ON 1
// Arduino index equals Nordic GPIO number: 8=P0.08, 32+n=P1.n.
#define PIN_SERIAL1_RX 8
#define PIN_SERIAL1_TX 11 // LPC SWCLK becomes control RX after its SWD recovery window.
#define PIN_EXT_POWER 13
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO 14
#define PIN_SPI_MOSI 16
#define PIN_SPI_SCK 15
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA 17
#define PIN_WIRE_SCL 20
#define PIN_A0 2
#define PIN_A1 3
#define PIN_A2 4
#define PIN_A3 5
#define PIN_A4 28
#define PIN_A5 29
#define PIN_A6 30
#define PIN_A7 31
static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
static const uint8_t SS = 10;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;
#define SERIAL_PORT_MONITOR Serial
#define SERIAL_PORT_USBVIRTUAL Serial
#define SERIAL_PORT_HARDWARE Serial1
#define SERIAL_PORT_HARDWARE_OPEN Serial1
#ifdef __cplusplus
extern "C" void initVariant();
#endif
