/* Bootloader peer handover adapted from Adafruit BLEDfu.cpp, BSD-3-Clause.
 * Copyright (c) 2018, Adafruit Industries. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "DfuService.hpp"
#include "bridge/Protocol.hpp"

err_t DfuService::begin() {
    setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    VERIFY_STATUS(BLEDfu::begin());
    // Bond storage and bootloader handover must run outside the BLE event task,
    // as in Adafruit's BLEDfu: filesystem/SoftDevice calls can wait for events.
    _chr_control.setWriteAuthorizeCallback(authorize);
    return ERROR_NONE;
}
void DfuService::authorize(uint16_t handle, BLECharacteristic* chr, ble_gatts_evt_write_t* request) {
    auto& self = static_cast<DfuService&>(chr->parentService());
    auto* connection = Bluefruit.Connection(handle);
    bond_keys_t keys;
    ble_gatts_rw_authorize_reply_params_t reply = {};
    reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
    reply.params.write.gatt_status = BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED;
    // Accept only the buttonless START opcode; reject empty/prepared/offset writes.
    bool valid = connection && connection->secured() && connection->loadBondKey(&keys) &&
        request->op == BLE_GATTS_OP_WRITE_REQ && request->offset == 0 &&
        request->len == 1 && request->data[0] == 1 && chr->notifyEnabled(handle) &&
        NRF_UICR->NRFFW[0] == 0xf4000;
    if (valid) valid = self.lpc_.reserveExclusive();
    if (valid) {
        reply.params.write.gatt_status = BLE_GATT_STATUS_SUCCESS;
        // S140 requires update=1 and the actual data for authorized writes.
        reply.params.write.update = 1;
        reply.params.write.offset = request->offset;
        reply.params.write.len = request->len;
        reply.params.write.p_data = request->data;
    }
    const auto result = sd_ble_gatts_rw_authorize_reply(handle, &reply);
    if (!valid) return;
    if (result != NRF_SUCCESS) { self.lpc_.cancelExclusive(); return; }

    struct PeerData {
        ble_gap_addr_t address;
        ble_gap_irk_t irk;
        ble_gap_enc_key_t encryption;
        uint8_t attributes[8];
        uint16_t crc;
    };
    static_assert(offsetof(PeerData, crc) == 60, "Bootloader peer ABI changed");
    auto* peer = reinterpret_cast<PeerData*>(0x20007f80);
    memset(peer, 0, sizeof(*peer));
    uint16_t size = sizeof(peer->attributes);
    sd_ble_gatts_sys_attr_get(handle, peer->attributes, &size, BLE_GATTS_SYS_ATTR_FLAG_SYS_SRVCS);
    peer->address = keys.peer_id.id_addr_info;
    peer->irk = keys.peer_id.id_info;
    peer->encryption = keys.own_enc;
    peer->crc = bridge::crc16(reinterpret_cast<const uint8_t*>(peer), offsetof(PeerData, crc));

    Bluefruit.Advertising.restartOnDisconnect(false);
    Bluefruit.Advertising.stop();
    self.hid_.releaseAll();
    // Let the ATT response leave before disconnecting. A system reset clears
    // Arduino's active peripherals/RTOS state; the bootloader retains .noinit
    // peer data and recognizes 0xa8 as BLE DFU with a fresh SoftDevice init.
    delay(50);
    uint16_t handles[bridge::HostProfiles::kCount];
    const auto count = Bluefruit.getConnectedHandles(handles, bridge::HostProfiles::kCount);
    for (unsigned i = 0; i < count; ++i) Bluefruit.disconnect(handles[i]);
    delay(100);
    sd_power_gpregret_clr(0, 0xff);
    sd_power_gpregret_set(0, 0xa8);
    NVIC_SystemReset();
}
