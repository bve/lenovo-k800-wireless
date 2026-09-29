#include <assert.h>
#include <stdio.h>
#include "HidService.hpp"
#include "LpcUpdateService.hpp"
#include "ConnectionTuning.hpp"
// The service tests never program hardware; algorithm/SWD are covered separately.
bridge::UpdateError LpcProgrammer::program(const uint8_t*,uint32_t,Progress,void*) { assert(false); return bridge::UpdateError::None; }
bool SwdPort::attach() { return false; } void SwdPort::release(bool) {}
bool SwdPort::readMemory(uint32_t,uint32_t&) { return false; }
bool SwdPort::writeMemory(uint32_t,uint32_t) { return false; }
bool SwdPort::writeRegister(uint8_t,uint32_t) { return false; }
bool SwdPort::waitHalted(uint32_t) { return false; }
int main() {
 HidService hid; assert(hid.begin()==0);
 auto* protocol=fakeCharacteristics[0]; auto* keys=fakeCharacteristics[1];
 auto* consumer=fakeCharacteristics[2]; auto* leds=fakeCharacteristics[3]; auto* boot=fakeCharacteristics[5];
 for(uint16_t h=0;h<3;++h) {
  Bluefruit.peers[h].up=true; static_cast<BLEService&>(hid).svc_connect_hdl(h);
  keys->subscribed[h]=consumer->subscribed[h]=boot->subscribed[h]=true;
 }
 const auto a=hid.endpoint(0), b=hid.endpoint(1);
 leds->clientWrite(0,1); leds->clientWrite(1,2); protocol->clientWrite(1,0);
 assert(hid.leds(a)==1 && hid.leds(b)==2 && !hid.bootMode(a) && hid.bootMode(b));
 protocol->clientRead(0); assert(fakeRead==std::vector<uint8_t>{1});
 protocol->clientRead(1); assert(fakeRead==std::vector<uint8_t>{0});
 hid.select(a); uint8_t report[8]={2,0,4};
 assert(hid.keyboard(a,report)); assert(fakeNotifications.back().handle==0 && fakeNotifications.back().chr==keys);
 keys->clientRead(0); assert(fakeRead[0]==2 && fakeRead[2]==4);
 // A saturated SoftDevice queue does not update the last accepted report.
 fakeHvnSlots=0; report[2]=5; assert(!hid.keyboard(a,report));
 keys->clientRead(0); assert(fakeRead[2]==4);
 // Release must progress even if only one notification fits per radio event.
 fakeHvnSlots=1; assert(!hid.release(a));
 fakeHvnSlots=1; assert(!hid.release(a));
 fakeHvnSlots=1; assert(hid.release(a));
 const auto releasedCount=fakeNotifications.size(); assert(hid.release(a));
 assert(fakeNotifications.size()==releasedCount);
 fakeHvnSlots=1000;
 keys->clientRead(1); for(auto x:fakeRead) assert(!x); // shared characteristic never leaks another host's keys
 boot->clientRead(2); for(auto x:fakeRead) assert(!x);
 assert(hid.release(a)); hid.select(b); assert(hid.keyboard(b,report));
 assert(fakeNotifications.back().handle==1 && fakeNotifications.back().chr==boot);
 keys->clientRead(0); for(auto x:fakeRead) assert(!x);
 leds->clientRead(0); assert(fakeRead[0]==1); leds->clientRead(1); assert(fakeRead[0]==2);
 keys->clientRead(0,9); assert(fakeStatus==BLE_GATT_STATUS_ATTERR_INVALID_OFFSET);
 static_cast<BLEService&>(hid).svc_disconnect_hdl(1); static_cast<BLEService&>(hid).svc_connect_hdl(1);
 assert(!hid.live(b) && !hid.bootMode(hid.endpoint(1)) && hid.leds(hid.endpoint(1))==0);
 assert(!hid.keyboard(b,report));
 bridge::HostProfiles profiles;
 bridge::PeerIdentity first{0,{1,2,3,4,5,6}}, second{1,{6,5,4,3,2,1}};
 assert(profiles.connect(first,a)==0 && profiles.connect(second,hid.endpoint(1))==1);
 ConnectionTuning tuning; tuning.poll(100,profiles,hid); tuning.poll(1099,profiles,hid);
 assert(Bluefruit.peers[0].requests==0);
 tuning.poll(1100,profiles,hid);
 assert(Bluefruit.peers[0].requestedInterval==6 && Bluefruit.peers[1].requestedInterval==24);
 tuning.poll(10000,profiles,hid); assert(Bluefruit.peers[0].requests==1); // Respect central's decision.
 profiles.select(1); Bluefruit.peers[1].parameterReady=false;
 tuning.poll(10001,profiles,hid);
 for(unsigned t=11001;t<=15001;t+=1000) tuning.poll(t,profiles,hid);
 assert(Bluefruit.peers[0].requestedInterval==24 && Bluefruit.peers[1].requestedInterval==6);
 assert(Bluefruit.peers[1].requests==4); // Three bounded retries, not a radio negotiation loop.
 const auto oldPeer = profiles.endpoint(1);
 static_cast<BLEService&>(hid).svc_disconnect_hdl(1);
 // Reusing a handle for the same bonded host still starts a new tuning attempt.
 profiles.disconnect(oldPeer); profiles.connect(second,hid.endpoint(1));
 tuning.poll(0xfffffff0u,profiles,hid); tuning.poll(984,profiles,hid);
 assert(Bluefruit.peers[1].requests==5);
 LpcUpdateService lpc; const auto base=fakeCharacteristics.size(); assert(!lpc.begin());
 auto* command=fakeCharacteristics[base]; auto* data=fakeCharacteristics[base+1]; auto* status=fakeCharacteristics[base+2];
 std::vector<uint8_t> begin={1,0,4,0,0,0,0,0,0}; // valid 1024-byte receive session
 command->authorizedWrite(0,begin); assert(fakeStatus==0 && status->value[1]==1);
 data->authorizedWrite(0,{0,0,0,0,1,2,3,4}); assert(fakeStatus==0 && bridge::read32(status->value.data()+4)==4);
 const auto before=status->value;
 command->authorizedWrite(1,{3}); assert(fakeStatus!=0 && status->value==before);
 data->authorizedWrite(2,{4,0,0,0,5}); assert(fakeStatus!=0 && status->value==before);
 static_cast<BLEService&>(lpc).svc_disconnect_hdl(1); assert(status->value==before);
 static_cast<BLEService&>(lpc).svc_disconnect_hdl(0); assert(status->value[1]==0);
 command->authorizedWrite(1,begin); assert(fakeStatus==0 && status->value[1]==1);
 std::vector<uint8_t> image(1024,0xff);
 for(unsigned i=0;i<8;++i) bridge::write32(image.data()+4*i,0);
 bridge::write32(image.data(),0x10001000); bridge::write32(image.data()+4,0x301);
 bridge::write32(image.data()+28,0u-0x10001000-0x301);
 bridge::write32(begin.data()+5,bridge::crc32(image.data(),image.size()));
 command->authorizedWrite(1,begin);
 for(unsigned offset=0;offset<image.size();offset+=16) {
  std::vector<uint8_t> packet(20); bridge::write32(packet.data(),offset);
  memcpy(packet.data()+4,image.data()+offset,16); data->authorizedWrite(1,packet);
  assert(fakeStatus==0);
 }
 command->authorizedWrite(1,{2}); assert(fakeStatus==0 && lpc.pending());
 const auto committed=status->value;
 command->authorizedWrite(0,{3}); assert(fakeStatus!=0 && status->value==committed);
 static_cast<BLEService&>(lpc).svc_disconnect_hdl(2); assert(lpc.pending() && status->value==committed);
 static_cast<BLEService&>(lpc).svc_disconnect_hdl(1); assert(lpc.pending());
 assert(!lpc.reserveExclusive()); // committed image survives owner disconnect and excludes DFU/sleep
 puts("PASS: per-connection HID reads/notifications/LEDs/protocol; LPC update ownership");
}
