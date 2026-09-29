#include "HidService.hpp"

namespace {
const uint8_t kReportMap[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x75,0x08, 0x95,0x01, 0x81,0x01,
    0x05,0x08, 0x19,0x01, 0x29,0x05, 0x75,0x01, 0x95,0x05, 0x91,0x02,
    0x75,0x03, 0x95,0x01, 0x91,0x01,
    0x05,0x07, 0x19,0x00, 0x2a,0xe7,0x00, 0x15,0x00, 0x26,0xe7,0x00,
    0x75,0x08, 0x95,0x06, 0x81,0x00, 0xc0,
    0x05,0x0c, 0x09,0x01, 0xa1,0x01, 0x85,0x02,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x07,
    0x09,0xb5, 0x09,0xb6, 0x09,0xb7, 0x09,0xcd,
    0x09,0xe9, 0x09,0xea, 0x09,0xe2, 0x81,0x02,
    0x95,0x01, 0x81,0x01, 0xc0
};
}
HidService::HidService() : BLEService(UUID16_SVC_HUMAN_INTERFACE_DEVICE),
    protocol_(UUID16_CHR_PROTOCOL_MODE), keys_(UUID16_CHR_REPORT), consumer_(UUID16_CHR_REPORT),
    leds_(UUID16_CHR_REPORT), map_(UUID16_CHR_REPORT_MAP),
    bootKeys_(UUID16_CHR_BOOT_KEYBOARD_INPUT_REPORT), bootLeds_(UUID16_CHR_BOOT_KEYBOARD_OUTPUT_REPORT),
    info_(UUID16_CHR_HID_INFORMATION), control_(UUID16_CHR_HID_CONTROL_POINT) {}
err_t HidService::input(BLECharacteristic& chr, unsigned size, uint8_t reportId) {
    chr.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    chr.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
    chr.setFixedLen(size);
    chr.setReadAuthorizeCallback(read, false);
    if (reportId) chr.setReportRefDescriptor(reportId, 1);
    return chr.begin();
}
err_t HidService::output(BLECharacteristic& chr, uint8_t reportId) {
    chr.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    chr.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    chr.setFixedLen(1);
    chr.setReadAuthorizeCallback(read, false);
    chr.setWriteCallback(write, false);
    if (reportId) chr.setReportRefDescriptor(reportId, 2);
    return chr.begin();
}
err_t HidService::begin() {
    VERIFY_STATUS(BLEService::begin());
    // Preserve the previous BLEHidGeneric attribute order for bonded hosts.
    protocol_.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE_WO_RESP);
    protocol_.setFixedLen(1);
    protocol_.setReadAuthorizeCallback(read, false);
    protocol_.setWriteCallback(write, false);
    VERIFY_STATUS(protocol_.begin());
    VERIFY_STATUS(input(keys_, 8, 1));
    VERIFY_STATUS(input(consumer_, 1, 2));
    VERIFY_STATUS(output(leds_, 1));
    map_.setProperties(CHR_PROPS_READ);
    map_.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
    map_.setFixedLen(sizeof(kReportMap));
    VERIFY_STATUS(map_.begin());
    map_.write(kReportMap, sizeof(kReportMap));
    VERIFY_STATUS(input(bootKeys_, 8));
    VERIFY_STATUS(output(bootLeds_));
    info_.setProperties(CHR_PROPS_READ);
    info_.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
    info_.setFixedLen(4);
    VERIFY_STATUS(info_.begin());
    const uint8_t info[] = {0x11, 0x01, 0, 2};
    info_.write(info, sizeof(info));
    control_.setProperties(CHR_PROPS_WRITE_WO_RESP);
    control_.setPermission(SECMODE_NO_ACCESS, SECMODE_ENC_NO_MITM);
    control_.setFixedLen(1);
    return control_.begin();
}
void HidService::svc_connect_hdl(uint16_t handle) {
    if (handle >= BLE_MAX_CONNECTION) return;
    taskENTER_CRITICAL();
    const uint32_t generation = state_[handle].generation + 1;
    state_[handle] = State{};
    state_[handle].generation = generation;
    taskEXIT_CRITICAL();
}
void HidService::svc_disconnect_hdl(uint16_t handle) { svc_connect_hdl(handle); }
bridge::HidEndpoint HidService::endpoint(uint16_t handle) const {
    if (handle >= BLE_MAX_CONNECTION) return {};
    taskENTER_CRITICAL();
    const bridge::HidEndpoint result{handle, state_[handle].generation};
    taskEXIT_CRITICAL();
    return result;
}
bool HidService::live(bridge::HidEndpoint peer) const {
    if (!peer.valid() || endpoint(peer.handle) != peer) return false;
    auto* conn = Bluefruit.Connection(peer.handle);
    return conn && conn->connected() && conn->secured();
}
bool HidService::bootMode(bridge::HidEndpoint peer) const {
    if (!live(peer)) return false;
    taskENTER_CRITICAL(); const bool result = state_[peer.handle].boot; taskEXIT_CRITICAL();
    return result;
}
uint8_t HidService::leds(bridge::HidEndpoint peer) const {
    if (!live(peer)) return 0;
    taskENTER_CRITICAL(); const auto result = state_[peer.handle].leds; taskEXIT_CRITICAL();
    return result;
}
void HidService::select(bridge::HidEndpoint peer) {
    taskENTER_CRITICAL(); selected_ = peer; taskEXIT_CRITICAL();
}
bool HidService::notify(BLECharacteristic& chr, uint16_t handle, const void* data, uint16_t size) {
    if (!chr.notifyEnabled(handle)) return false;
    // Bluefruit notify() can wait on a semaphore and stall matrix processing.
    // These reports fit one ATT packet. Let SoftDevice report a full queue
    // immediately; HidRouter retains the report and retries on the next poll.
    ble_gatts_hvx_params_t packet = {};
    packet.handle = chr.handles().value_handle;
    packet.type = BLE_GATT_HVX_NOTIFICATION;
    packet.p_len = &size;
    packet.p_data = static_cast<const uint8_t*>(data);
    return sd_ble_gatts_hvx(handle, &packet) == NRF_SUCCESS;
}
bool HidService::keyboard(bridge::HidEndpoint peer, const uint8_t* report) {
    if (!live(peer)) return false;
    auto& chr = bootMode(peer) ? bootKeys_ : keys_;
    if (!notify(chr, peer.handle, report, 8)) return false;
    taskENTER_CRITICAL();
    if (state_[peer.handle].generation == peer.generation) {
        memcpy(state_[peer.handle].report.keyboard, report, 8);
        state_[peer.handle].released = 0;
    }
    taskEXIT_CRITICAL();
    return true;
}
bool HidService::consumer(bridge::HidEndpoint peer, uint8_t report) {
    if (!live(peer)) return false;
    if (!bootMode(peer) && !notify(consumer_, peer.handle, &report, 1)) return false;
    taskENTER_CRITICAL();
    if (state_[peer.handle].generation == peer.generation) {
        state_[peer.handle].report.consumer = report;
        state_[peer.handle].released = 0;
    }
    taskEXIT_CRITICAL();
    return true;
}
bool HidService::release(bridge::HidEndpoint peer) {
    if (!live(peer)) return true;
    const uint8_t zero[8] = {};
    bool done = true;
    BLECharacteristic* inputs[] = {&keys_, &bootKeys_, &consumer_};
    for (unsigned i = 0; i < 3; ++i) {
        auto* chr = inputs[i];
        taskENTER_CRITICAL();
        const bool sent = state_[peer.handle].released & (1u << i);
        taskEXIT_CRITICAL();
        if (sent || !chr->notifyEnabled(peer.handle)) continue;
        if (!notify(*chr, peer.handle, zero, chr == &consumer_ ? 1 : 8)) { done = false; continue; }
        // Remember partial progress: a queue with one free slot must eventually
        // release all reports, without re-enqueuing the first one on each poll.
        taskENTER_CRITICAL();
        if (state_[peer.handle].generation == peer.generation) state_[peer.handle].released |= 1u << i;
        taskEXIT_CRITICAL();
    }
    if (done) {
        taskENTER_CRITICAL();
        if (state_[peer.handle].generation == peer.generation) state_[peer.handle].report = {};
        taskEXIT_CRITICAL();
    }
    return done;
}
bool HidService::releaseAll() {
    uint16_t handles[bridge::HostProfiles::kCount];
    const auto count = Bluefruit.getConnectedHandles(handles, bridge::HostProfiles::kCount);
    bool done = true;
    for (unsigned i = 0; i < count; ++i) done = release(endpoint(handles[i])) && done;
    return done;
}
void HidService::write(uint16_t handle, BLECharacteristic* chr, uint8_t* data, uint16_t size) {
    if (handle >= BLE_MAX_CONNECTION || size != 1) return;
    auto& self = static_cast<HidService&>(chr->parentService());
    taskENTER_CRITICAL();
    if (chr == &self.protocol_) { if (*data <= 1) self.state_[handle].boot = *data == 0; }
    else self.state_[handle].leds = *data & 7;
    taskEXIT_CRITICAL();
}
void HidService::read(uint16_t handle, BLECharacteristic* chr, ble_gatts_evt_read_t* request) {
    auto& self = static_cast<HidService&>(chr->parentService());
    uint8_t data[8] = {};
    uint16_t length = 1;
    taskENTER_CRITICAL();
    if (handle < BLE_MAX_CONNECTION) {
        const auto& state = self.state_[handle];
        const bool selected = self.selected_ == bridge::HidEndpoint{handle, state.generation};
        if (chr == &self.protocol_) data[0] = !state.boot;
        else if (chr == &self.leds_ || chr == &self.bootLeds_) data[0] = state.leds;
        else if (chr == &self.consumer_) data[0] = selected ? state.report.consumer : 0;
        else { length = 8; if (selected) memcpy(data, state.report.keyboard, 8); }
    }
    taskEXIT_CRITICAL();
    ble_gatts_rw_authorize_reply_params_t reply = {};
    reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
    reply.params.read.gatt_status = BLE_GATT_STATUS_ATTERR_INVALID_OFFSET;
    if (request->offset <= length) {
        reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
        reply.params.read.update = 1;
        reply.params.read.offset = request->offset;
        reply.params.read.len = length - request->offset;
        reply.params.read.p_data = data + request->offset;
    }
    sd_ble_gatts_rw_authorize_reply(handle, &reply);
}
