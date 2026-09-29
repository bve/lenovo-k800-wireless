#pragma once
#include "Arduino.h"
#include <vector>
#include <cstring>
using err_t = uint32_t;
constexpr unsigned BLE_MAX_CONNECTION=20, BLE_CONN_HANDLE_INVALID=0xffff;
constexpr unsigned NRF_SUCCESS=0, NRF_ERROR_NO_MEM=4, ERROR_NONE=0;
constexpr unsigned NRF_ERROR_RESOURCES=19, BLE_GATT_HVX_NOTIFICATION=1;
struct ble_gatts_char_handles_t { uint16_t value_handle=0; };
struct ble_gatts_hvx_params_t { uint16_t handle=0; uint8_t type=0; uint16_t offset=0; uint16_t* p_len=nullptr; const uint8_t* p_data=nullptr; };
constexpr unsigned BLE_GATT_STATUS_SUCCESS=0, BLE_GATT_STATUS_ATTERR_INVALID_OFFSET=7, BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED=3;
constexpr unsigned BLE_GATTS_AUTHORIZE_TYPE_READ=1, BLE_GATTS_AUTHORIZE_TYPE_WRITE=2, BLE_GATTS_OP_WRITE_REQ=1;
constexpr unsigned CHR_PROPS_READ=1, CHR_PROPS_WRITE=2, CHR_PROPS_NOTIFY=4, CHR_PROPS_WRITE_WO_RESP=8;
constexpr unsigned SECMODE_ENC_NO_MITM=1, SECMODE_NO_ACCESS=0;
constexpr unsigned UUID16_SVC_HUMAN_INTERFACE_DEVICE=0x1812, UUID16_CHR_PROTOCOL_MODE=0x2a4e,
 UUID16_CHR_REPORT=0x2a4d, UUID16_CHR_REPORT_MAP=0x2a4b, UUID16_CHR_BOOT_KEYBOARD_INPUT_REPORT=0x2a22,
 UUID16_CHR_BOOT_KEYBOARD_OUTPUT_REPORT=0x2a32, UUID16_CHR_HID_INFORMATION=0x2a4a, UUID16_CHR_HID_CONTROL_POINT=0x2a4c;
#define taskENTER_CRITICAL() do {} while(0)
#define taskEXIT_CRITICAL() do {} while(0)
#define VERIFY_STATUS(expr) do { auto error=(expr); if(error) return error; } while(0)
struct ble_gatts_evt_read_t { uint16_t offset=0; };
struct ble_gatts_evt_write_t { uint16_t op=1, offset=0, len=0; uint8_t data[20]={}; };
struct AuthorizeParams { uint16_t gatt_status=0,update=0,offset=0,len=0; const uint8_t* p_data=nullptr; };
struct ble_gatts_rw_authorize_reply_params_t { unsigned type=0; struct { AuthorizeParams read, write; } params; };
inline uint16_t fakeStatus=0; inline std::vector<uint8_t> fakeRead;
inline uint32_t sd_ble_gatts_rw_authorize_reply(uint16_t, const ble_gatts_rw_authorize_reply_params_t* reply) {
 const auto& p=reply->type==1 ? reply->params.read : reply->params.write;
 fakeStatus=p.gatt_status;
 if(reply->type==1 && p.p_data) fakeRead.assign(p.p_data,p.p_data+p.len);
 return NRF_SUCCESS;
}
class BLEService;
inline BLEService* fakeCurrentService=nullptr;
class BLEService {
public:
 template<class T> explicit BLEService(T) {}
 virtual ~BLEService()=default;
 virtual err_t begin() { fakeCurrentService=this; return 0; }
 void setPermission(unsigned,unsigned) {}
 virtual void svc_connect_hdl(uint16_t) {}
 virtual void svc_disconnect_hdl(uint16_t) {}
};
class BLECharacteristic;
inline std::vector<BLECharacteristic*> fakeCharacteristics;
struct FakeNotification { const BLECharacteristic* chr; uint16_t handle; std::vector<uint8_t> data; };
inline std::vector<FakeNotification> fakeNotifications;
class BLECharacteristic {
public:
 using read_cb=void(*)(uint16_t,BLECharacteristic*,ble_gatts_evt_read_t*);
 using write_cb=void(*)(uint16_t,BLECharacteristic*,uint8_t*,uint16_t);
 using authorize_cb=void(*)(uint16_t,BLECharacteristic*,ble_gatts_evt_write_t*);
 template<class T> explicit BLECharacteristic(T) {}
 void setProperties(unsigned) {} void setPermission(unsigned,unsigned) {} void setFixedLen(unsigned) {} void setMaxLen(unsigned) {}
 void setReadAuthorizeCallback(read_cb cb,bool) { reader=cb; }
 void setWriteCallback(write_cb cb,bool) { writer=cb; }
 void setWriteAuthorizeCallback(authorize_cb cb,bool) { authorizer=cb; }
 void setReportRefDescriptor(unsigned,unsigned) {}
 err_t begin() { parent=fakeCurrentService; id=fakeCharacteristics.size(); fakeCharacteristics.push_back(this); return 0; }
 ble_gatts_char_handles_t handles() { return {id}; }
 BLEService& parentService() { return *parent; }
 void write(const void* data,size_t n) { value.assign((const uint8_t*)data,(const uint8_t*)data+n); }
 bool notifyEnabled(uint16_t handle) { return subscribed[handle]; }
 bool notify(uint16_t handle,const void* data,size_t n) {
  if(!notifyReady || !subscribed[handle]) return false;
  fakeNotifications.push_back({this,handle,{(const uint8_t*)data,(const uint8_t*)data+n}}); return true;
 }
 void clientRead(uint16_t handle,uint16_t offset=0) { ble_gatts_evt_read_t r{offset}; reader(handle,this,&r); }
 void clientWrite(uint16_t handle,uint8_t data) { writer(handle,this,&data,1); }
 void authorizedWrite(uint16_t handle,std::vector<uint8_t> data) {
  ble_gatts_evt_write_t r; r.len=data.size(); std::memcpy(r.data,data.data(),data.size()); authorizer(handle,this,&r);
 }
 BLEService* parent=nullptr; read_cb reader=nullptr; write_cb writer=nullptr; authorize_cb authorizer=nullptr;
 bool subscribed[BLE_MAX_CONNECTION]={}; bool notifyReady=true; std::vector<uint8_t> value;
 uint16_t id=0;
};
inline unsigned fakeHvnSlots=1000;
inline uint32_t sd_ble_gatts_hvx(uint16_t handle, const ble_gatts_hvx_params_t* packet) {
 auto* chr=fakeCharacteristics.at(packet->handle);
 if(!fakeHvnSlots || !chr->notify(handle,packet->p_data,*packet->p_len)) return NRF_ERROR_RESOURCES;
 --fakeHvnSlots; return NRF_SUCCESS;
}
struct BLEConnection {
 bool up=false, parameterReady=true;
 uint16_t interval=36, latency=0, requestedInterval=0; unsigned requests=0;
 bool connected() { return up; } bool secured() { return up; } bool bonded() { return up; }
 uint16_t getConnectionInterval() { return interval; } uint16_t getSlaveLatency() { return latency; }
 bool requestConnectionParameter(uint16_t value,uint16_t,uint16_t) { ++requests; requestedInterval=value; return parameterReady; }
};
struct FakeBluefruit {
 BLEConnection peers[BLE_MAX_CONNECTION];
 BLEConnection* Connection(uint16_t handle) { return handle<BLE_MAX_CONNECTION ? &peers[handle] : nullptr; }
 uint8_t getConnectedHandles(uint16_t* handles,uint8_t max) {
  uint8_t count=0; for(unsigned i=0;i<BLE_MAX_CONNECTION && count<max;++i) if(peers[i].up) handles[count++]=i; return count;
 }
};
inline FakeBluefruit Bluefruit;
