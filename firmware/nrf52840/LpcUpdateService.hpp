#pragma once
#include <bluefruit.h>
#include <semphr.h>
#include "LpcProgrammer.hpp"
#include "SwdPort.hpp"

class LpcUpdateService : public BLEService {
public:
    LpcUpdateService();
    err_t begin() override;
    // Called by the Arduino task, never from a BLE callback.
    bool pending();
    void program();
    bool reserveExclusive();
    void cancelExclusive();
    bool exclusiveReserved();
private:
    class Lock {
    public:
        explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(mutex_); }
    private:
        SemaphoreHandle_t mutex_;
    };
    void svc_disconnect_hdl(uint16_t handle) override;
    static void write(uint16_t handle, BLECharacteristic* characteristic, ble_gatts_evt_write_t* request);
    static void progress(uint32_t bytes, void* context);
    void receive(BLECharacteristic* characteristic, const uint8_t* data, uint16_t length);
    void publish(); // Caller owns mutex_. Status is also readable without notifications.
    BLECharacteristic control_, data_, status_;
    SemaphoreHandle_t mutex_ = nullptr;
    bridge::LpcImageBuffer image_;
    SwdPort swd_;
    LpcProgrammer programmer_{swd_};
    bridge::UpdateState state_ = bridge::UpdateState::Idle;
    bridge::UpdateError error_ = bridge::UpdateError::None;
    uint32_t progress_ = 0;
    uint8_t sequence_ = 0;
    bool exclusiveReserved_ = false;
    uint16_t owner_ = BLE_CONN_HANDLE_INVALID;
};
