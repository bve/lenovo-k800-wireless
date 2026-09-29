#include "SystemSleep.hpp"
#include <Arduino.h>
#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>
#include "bridge/HostProfiles.hpp"

[[noreturn]] void SystemSleep::enter() {
    Bluefruit.Advertising.restartOnDisconnect(false);
    Bluefruit.Advertising.stop();
    uint16_t handles[bridge::HostProfiles::kCount];
    const auto count = Bluefruit.getConnectedHandles(handles, bridge::HostProfiles::kCount);
    for (unsigned i = 0; i < count; ++i) Bluefruit.disconnect(handles[i]);
    TinyUSBDevice.detach();
    NRF_USBD->ENABLE = USBD_ENABLE_ENABLE_Disabled;
    // Disable retention only, never the RAM sections powering the live stack.
    for (uint8_t bank = 0; bank < 9; ++bank)
        sd_power_ram_power_clr(bank, bank == 8 ? 0xffff0000u : 0x00030000u);
    // P0.13 is deliberately retained HIGH. LPC pulls P0.08 LOW on its wake key.
    // GPIO wake resets nRF; normal startup reloads macros and cycles LPC power.
    systemOff(PIN_SERIAL1_RX, LOW);
    // An unexpected SoftDevice error must recover, never leave LPC asleep alone.
    NVIC_SystemReset();
    for (;;) {}
}
