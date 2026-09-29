#include "LpcUpdateService.hpp"

// Custom LPC flashing service, little-endian UUID bytes for Bluefruit.
namespace {
const uint8_t kService[] = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x00,0x10,0x4b,0x6c};
const uint8_t kControl[] = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x01,0x10,0x4b,0x6c};
const uint8_t kData[]    = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x02,0x10,0x4b,0x6c};
const uint8_t kStatus[]  = {0xb7,0x90,0x65,0x42,0x67,0x5d,0x31,0x9f,0x9b,0x47,0x0a,0x5f,0x03,0x10,0x4b,0x6c};
}
LpcUpdateService::LpcUpdateService() : BLEService(kService), control_(kControl), data_(kData), status_(kStatus) {}
err_t LpcUpdateService::begin() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return NRF_ERROR_NO_MEM;
    setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    VERIFY_STATUS(BLEService::begin());
    BLECharacteristic* writable[] = {&control_, &data_};
    for (auto* characteristic : writable) {
        characteristic->setProperties(CHR_PROPS_WRITE);
        characteristic->setMaxLen(20);
        // Execute serially in the BLE event task, not a deferred callback queue.
        characteristic->setWriteAuthorizeCallback(write, false);
        VERIFY_STATUS(characteristic->begin());
    }
    status_.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    status_.setFixedLen(20);
    VERIFY_STATUS(status_.begin());
    publish();
    return ERROR_NONE;
}
void LpcUpdateService::publish() {
    uint8_t status[20] = {1, uint8_t(state_), uint8_t(error_), sequence_};
    bridge::write32(status + 4, image_.received());
    bridge::write32(status + 8, image_.size());
    bridge::write32(status + 12, image_.expectedCrc());
    bridge::write32(status + 16, state_ == bridge::UpdateState::Failed ? programmer_.detail() : progress_);
    status_.write(status, sizeof(status));
    if (owner_ != BLE_CONN_HANDLE_INVALID) status_.notify(owner_, status, sizeof(status));
}
void LpcUpdateService::write(uint16_t handle, BLECharacteristic* chr, ble_gatts_evt_write_t* request) {
    auto& self = static_cast<LpcUpdateService&>(chr->parentService());
    auto* connection = Bluefruit.Connection(handle);
    Lock lock(self.mutex_);
    using bridge::UpdateState;
    const bool busy = self.state_ == UpdateState::Receiving || self.state_ == UpdateState::Pending ||
                      self.state_ == UpdateState::Programming;
    const bool valid = connection && connection->secured() && connection->bonded() &&
        (!busy || self.owner_ == handle) && !self.exclusiveReserved_ &&
        request->op == BLE_GATTS_OP_WRITE_REQ && request->offset == 0;
    ble_gatts_rw_authorize_reply_params_t reply = {};
    reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
    reply.params.write.gatt_status = valid ? BLE_GATT_STATUS_SUCCESS : BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED;
    if (valid) {
        reply.params.write.update = 1; reply.params.write.len = request->len;
        reply.params.write.p_data = request->data;
    }
    if (sd_ble_gatts_rw_authorize_reply(handle, &reply) != NRF_SUCCESS || !valid) return;
    self.receive(chr, request->data, request->len);
    if (self.state_ == UpdateState::Receiving || !busy) self.owner_ = handle;
    ++self.sequence_;
    self.publish();
}
void LpcUpdateService::receive(BLECharacteristic* chr, const uint8_t* data, uint16_t length) {
    using bridge::UpdateState;
    using bridge::UpdateError;
    if (exclusiveReserved_ || state_ == UpdateState::Pending || state_ == UpdateState::Programming) {
        error_ = UpdateError::Busy; return;
    }
    error_ = UpdateError::Command;
    if (chr == &data_) {
        if (state_ == UpdateState::Receiving && length > 4)
            error_ = image_.append(bridge::read32(data), data + 4, length - 4);
    } else if (length == 9 && data[0] == 1) { // BEGIN(size, crc32)
        error_ = image_.begin(bridge::read32(data + 1), bridge::read32(data + 5));
        if (error_ == UpdateError::None) { state_ = UpdateState::Receiving; progress_ = 0; }
    } else if (length == 1 && data[0] == 2 && state_ == UpdateState::Receiving) { // COMMIT
        error_ = image_.validate();
        if (error_ == UpdateError::None) state_ = UpdateState::Pending;
    } else if (length == 1 && data[0] == 3) { // ABORT before programming only
        image_.clear(); progress_ = 0; state_ = UpdateState::Idle; error_ = UpdateError::None;
    }
}
void LpcUpdateService::svc_disconnect_hdl(uint16_t handle) {
    Lock lock(mutex_);
    if (handle != owner_) return;
    owner_ = BLE_CONN_HANDLE_INVALID;
    if (state_ == bridge::UpdateState::Receiving) {
        image_.clear(); state_ = bridge::UpdateState::Idle;
        error_ = bridge::UpdateError::Disconnected; publish();
    }
    // After COMMIT the complete RAM image is independent of the BLE connection.
}
bool LpcUpdateService::pending() {
    Lock lock(mutex_);
    return state_ == bridge::UpdateState::Pending;
}
void LpcUpdateService::progress(uint32_t bytes, void* context) {
    auto& self = *static_cast<LpcUpdateService*>(context);
    Lock lock(self.mutex_);
    self.progress_ = bytes; self.publish();
}
void LpcUpdateService::program() {
    {
        Lock lock(mutex_);
        if (state_ != bridge::UpdateState::Pending) return;
        state_ = bridge::UpdateState::Programming; error_ = bridge::UpdateError::None; publish();
    }
    const auto result = programmer_.program(image_.data(), image_.size(), progress, this);
    Lock lock(mutex_);
    error_ = result;
    state_ = result == bridge::UpdateError::None ? bridge::UpdateState::Done : bridge::UpdateState::Failed;
    publish();
}
bool LpcUpdateService::reserveExclusive() {
    Lock lock(mutex_);
    if (state_ == bridge::UpdateState::Receiving || state_ == bridge::UpdateState::Pending ||
        state_ == bridge::UpdateState::Programming || exclusiveReserved_) return false;
    exclusiveReserved_ = true;
    return true;
}
void LpcUpdateService::cancelExclusive() {
    Lock lock(mutex_); exclusiveReserved_ = false;
}
bool LpcUpdateService::exclusiveReserved() {
    Lock lock(mutex_); return exclusiveReserved_;
}
