#include <Arduino.h>
#include <bluefruit.h>
#include "DfuService.hpp"
#include "LinkStatusService.hpp"
#include "LpcLink.hpp"
#include "ControlLink.hpp"
#include "KeyboardStore.hpp"
#include "SystemSleep.hpp"
#include "PeerConnections.hpp"
#include "ConnectionTuning.hpp"
#include "bridge/SleepController.hpp"
#include "bridge/HidRouter.hpp"

namespace {
class KeyboardBridge {
public:
    void begin(ble_connect_callback_t connected, ble_disconnect_callback_t disconnected,
               BLESecurity::secured_conn_cb_t secured, BLESecurity::pair_complete_cb_t paired) {
        task_ = xTaskGetCurrentTaskHandle();
        vTaskPrioritySet(task_, TASK_PRIO_NORMAL);
        lpcLink_.begin();
        Bluefruit.autoConnLed(false);
        Bluefruit.configAttrTableSize(4096);
        // Queue key/consumer reports without waiting for the next radio event.
        Bluefruit.configPrphConn(23, BLE_GAP_EVENT_LENGTH_MIN, 4, 1);
        if (!Bluefruit.begin(bridge::HostProfiles::kCount, 0)) return;
        store_.begin(factory_, profiles_);
        sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
        Bluefruit.Periph.setConnectCallback(connected);
        Bluefruit.Periph.setDisconnectCallback(disconnected);
        Bluefruit.Security.setSecuredCallback(secured);
        Bluefruit.Security.setPairCompleteCallback(paired);
        Bluefruit.setName("Lenovo BLE Keyboard");
        Bluefruit.setAppearance(BLE_APPEARANCE_HID_KEYBOARD);
        Bluefruit.setTxPower(0);
        if (hid_.begin() != ERROR_NONE) return;
        if (lpcUpdate_.begin() != ERROR_NONE || dfu_.begin() != ERROR_NONE) return;
        if (linkStatus_.begin() != ERROR_NONE) return;
        Bluefruit.Periph.setConnInterval(6, 12);
        Bluefruit.Periph.setConnSlaveLatency(4);
        Bluefruit.Periph.setConnSupervisionTimeoutMS(4000);
        Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
        Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);
        Bluefruit.Advertising.addService(hid_);
        Bluefruit.ScanResponse.addName();
        // Bluefruit's automatic restart only handles zero connections. Own all restarts here.
        Bluefruit.Advertising.restartOnDisconnect(false);
        Bluefruit.Advertising.setIntervalMS(100, 2000);
        Bluefruit.Advertising.setFastTimeout(5);
        ready_ = Bluefruit.Advertising.start(0);
    }
    void connectionChanged() { if (task_) xTaskNotifyGive(task_); }
    void secured(uint16_t handle) { peers_.secured(handle); connectionChanged(); }
    void wait() { ulTaskNotifyTake(pdTRUE, ms2tick(lpcLink_.powered() ? 1 : 1000)); }
    void poll() {
        if (!ready_) return;
        peers_.poll(profiles_);
        const bool pending = lpcUpdate_.pending();
        const bool wasPowered = lpcLink_.powered();
        const uint32_t previousBaud = lpcLink_.baud();
        lpcLink_.poll(millis(), Bluefruit.connected() || pending || sleep_.active());
        if (wasPowered != lpcLink_.powered() || previousBaud != lpcLink_.baud()) clearLink();
        if (pending && lpcLink_.uartRunning()) {
            if (!hid_.releaseAll()) return;
            lpcLink_.beginProgramming();
            lpcUpdate_.program();
            clearLink();
            lpcLink_.endProgramming(Bluefruit.connected());
        }
        const uint32_t now = millis();
        route();
        while (lpcLink_.uartRunning() && Serial1.available()) {
            linkStatus_.receivedByte();
            if (parser_.push(static_cast<uint8_t>(Serial1.read()), now, rows_)) {
                lpcLink_.receivedFrame(now);
                linkStatus_.receivedFrame(now);
                if (!sleep_.active()) {
                    factory_.update(rows_, now);
                    const auto buttons = bridge::FactoryButtons::read(rows_);
                    const int profile = doubleTap_.update(buttons.m >> 1, now);
                    if (profile >= 0 && profiles_.select(profile)) route();
                    router_.update(factory_.report());
                    if (sleepButton_.update(buttons.controls & 1) && control_.sleepSupported() &&
                        lpcUpdate_.reserveExclusive()) {
                        factory_.prepareSleep(now);
                        router_.reset();
                        sleep_.begin(now);
                    }
                }
                if (!Serial1.available()) control_.received(parser_, lighting());
                lastFrame_ = now; linkAlive_ = true;
            }
        }
        if (linkAlive_ && now - lastFrame_ > 350) {
            factory_.suspend(); linkAlive_ = false; router_.reset();
            doubleTap_ = bridge::ProfileDoubleTap{};
        }
        const bool selectedConnected = hid_.live(profiles_.target());
        factory_.tick(now, selectedConnected && linkAlive_ && router_.idle());
        if (linkAlive_) router_.update(factory_.report());
        const auto buttons = factory_.buttons();
        if (!bridge::equal(factory_.report(), bridge::Reports{}) || !router_.idle() ||
            buttons.g || buttons.m || buttons.controls || factory_.playing() ||
            factory_.recording() != bridge::FactoryKeyboard::Recording::Idle) idleAt_ = now;
        // Profile changes are small and must be retained even with an absent selected host.
        store_.poll(now, factory_, profiles_, sleep_.active() || profiles_.dirty() || now - idleAt_ >= 100);
        if (sleep_.active()) {
            const bool saved = store_.idle() && !factory_.dirtyMacros() && !factory_.settingsDirty() && !profiles_.dirty();
            const auto action = sleep_.poll(now, saved && hid_.releaseAll(), control_.sleepReady());
            if (action == bridge::SleepController::Action::EnterOff) {
                lpcLink_.prepareSleep(); SystemSleep::enter();
            }
            if (action == bridge::SleepController::Action::Recover) {
                lpcLink_.restart(); clearLink(); lpcUpdate_.cancelExclusive();
            }
        }
        if (!sleep_.active()) router_.poll(now, hid_);
        if (!sleep_.active() && !lpcUpdate_.exclusiveReserved()) tuning_.poll(now, profiles_, hid_);
        linkStatus_.poll(now, linkAlive_, lpcLink_);
        linkStatus_.publishControls(factory_, control_, store_.healthy(), hid_.leds(profiles_.target()));
        linkStatus_.publishProfiles(profiles_, router_.endpoint(), store_.healthy());
        linkStatus_.publishTiming(profiles_, lpcLink_);
        if (!sleep_.active() && !lpcUpdate_.exclusiveReserved() &&
            Bluefruit.connected() < bridge::HostProfiles::kCount && !Bluefruit.Advertising.isRunning())
            Bluefruit.Advertising.start(0);
    }
private:
    void route() {
        if (router_.route(profiles_.target(), hid_)) {
            // End a recording on its original host and never carry playback to another host.
            factory_.prepareHostChange(millis());
            hid_.select(router_.endpoint());
        }
    }
    bridge::LightingState lighting() const {
        const auto& s = factory_.settings();
        return {s.bank, factory_.recordLed(), s.brightnessA, s.brightnessB,
                hid_.leds(profiles_.target()), false, sleep_.requested()};
    }
    void clearLink() {
        parser_ = bridge::FrameParser{};
        linkAlive_ = false; factory_.suspend(); control_.reset(); router_.reset();
        sleepButton_ = bridge::SleepButton{}; doubleTap_ = bridge::ProfileDoubleTap{};
    }
    LpcLink lpcLink_;
    TaskHandle_t task_ = nullptr;
    HidService hid_;
    PeerConnections peers_{hid_};
    ConnectionTuning tuning_;
    bridge::HostProfiles profiles_;
    bridge::HidRouter router_;
    bridge::ProfileDoubleTap doubleTap_;
    LpcUpdateService lpcUpdate_;
    DfuService dfu_{lpcUpdate_, hid_};
    LinkStatusService linkStatus_;
    bridge::FrameParser parser_;
    bridge::FactoryKeyboard factory_;
    KeyboardStore store_;
    ControlLink control_;
    bridge::SleepButton sleepButton_;
    bridge::SleepController sleep_;
    uint8_t rows_[bridge::kRows] = {};
    uint32_t lastFrame_ = 0, idleAt_ = 0;
    bool ready_ = false, linkAlive_ = false;
};
KeyboardBridge keyboard;
}
void setup() {
    keyboard.begin([](uint16_t) { keyboard.connectionChanged(); },
                   [](uint16_t, uint8_t) { keyboard.connectionChanged(); },
                   [](uint16_t handle) { keyboard.secured(handle); },
                   [](uint16_t handle, uint8_t status) { if (!status) keyboard.secured(handle); });
}
void loop() { keyboard.poll(); keyboard.wait(); }
