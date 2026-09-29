#include <assert.h>
#include <stdio.h>
#include <vector>
#include "bridge/HidRouter.hpp"
#include "bridge/SaveSnapshot.hpp"
using namespace bridge;

static PeerIdentity identity(uint8_t last) { return {1, {1,2,3,4,5,last}}; }
static Reports pressed(uint8_t key = 4) { Reports r; r.keyboard[0] = 2; r.keyboard[2] = key; r.consumer = 8; return r; }
struct Transport {
    struct Event { HidEndpoint peer; bool release; Reports report; };
    HidEndpoint livePeers[3] = {{0,1},{1,1},{2,1}};
    bool releaseReady = true, keysReady = true, consumerReady = true;
    std::vector<Event> events;
    bool live(HidEndpoint p) const { for (auto x : livePeers) if (x == p) return p.valid(); return false; }
    bool bootMode(HidEndpoint) const { return false; }
    bool release(HidEndpoint p) { if (releaseReady) events.push_back({p, true, {}}); return releaseReady; }
    bool keyboard(HidEndpoint p, const uint8_t* r) {
        if (keysReady) { Reports report; memcpy(report.keyboard,r,8); events.push_back({p,false,report}); }
        return keysReady;
    }
    bool consumer(HidEndpoint p, uint8_t r) {
        if (consumerReady) { Reports report; report.consumer=r; events.push_back({p,false,report}); }
        return consumerReady;
    }
};
static void profiles() {
    HostProfiles p;
    assert(p.connect(identity(10), {2,7}) == 0);
    assert(p.connect(identity(11), {0,1}) == 1);
    assert(p.connect(identity(12), {1,4}) == 2);
    assert(p.occupiedMask() == 7 && p.connectedMask() == 7);
    assert(p.target() == (HidEndpoint{2,7}));
    assert(p.connect(identity(13), {4,1}) == -1);
    assert(p.connect(identity(11), {4,1}) == -1); // duplicate identity cannot take over a live session
    p.select(1); p.disconnect({0,1});
    assert(!p.target().valid() && p.connectedMask() == 5); // no fallback to another connected PC
    uint8_t data[HostProfiles::kEncodedSize]; p.encode(data);
    HostProfiles restored; assert(restored.restore(data,sizeof(data)));
    assert(restored.selected() == 1 && restored.occupiedMask() == 7 && !restored.connectedMask());
    assert(restored.connect(identity(12), {0,8}) == 2);
    assert(restored.connect(identity(10), {1,8}) == 0);
    assert(!restored.target().valid());
    assert(restored.connect(identity(11), {2,8}) == 1);
    assert(restored.target() == (HidEndpoint{2,8}));
    for (unsigned i=0;i<sizeof(data);++i) {
        data[i] ^= 1; HostProfiles bad; assert(!bad.restore(data,sizeof(data))); data[i] ^= 1;
    }
    restored.disconnect({2,7}); assert(restored.target().valid()); // stale disconnect
}
static void taps() {
    ProfileDoubleTap d;
    assert(d.update(2,0)==-1); assert(d.update(0,20)==-1); // held at boot is not the first tap
    assert(d.update(2,40)==-1); assert(d.update(0,70)==-1);
    assert(d.update(2,150)==-1); assert(d.update(0,180)==1);
    assert(d.update(2,210)==-1); assert(d.update(0,240)==-1); // triple tap doesn't select twice
    assert(d.update(4,270)==-1); assert(d.update(0,300)==-1);
    assert(d.update(4,700)==-1); assert(d.update(0,730)==-1); // too slow
    assert(d.update(6,760)==-1); assert(d.update(0,790)==-1); // chord cancels candidate
    assert(d.update(4,800)==-1); assert(d.update(0,830)==-1);
    assert(d.update(4,850)==-1); assert(d.update(0,880)==2);
    d = ProfileDoubleTap{}; d.update(0,0xffffff00u);
    d.update(1,0xffffff10u); assert(d.update(0,0xffffff20u)==-1);
    d.update(1,0xfffffff0u); assert(d.update(0,10)==0);
    d = ProfileDoubleTap{}; d.update(0,0); d.update(1,1); d.update(0,400);
    d.update(1,420); assert(d.update(0,440)==-1); // long hold isn't a tap
}
static void routing() {
    Transport t; HidRouter r;
    assert(r.route({0,1},t)); r.update({}); r.poll(1,t);
    r.update(pressed()); r.poll(2,t);
    assert(t.events.back().peer.handle == 0);
    t.events.clear(); t.releaseReady=false;
    assert(!r.route({1,1},t)); r.update(pressed(5)); r.poll(20000,t);
    assert(t.events.empty()); // failed release never sends to old or new host
    t.releaseReady=true; assert(r.route({1,1},t));
    assert(t.events.size()==1 && t.events[0].release && t.events[0].peer.handle==0);
    r.update(pressed()); r.poll(20001,t); // held Shift+A and consumer key cannot move to host 2
    for (size_t i=1;i<t.events.size();++i) assert(equal(t.events[i].report, Reports{}));
    assert(r.blocked()); r.update({}); r.poll(20002,t);
    r.update(pressed(6)); r.poll(20003,t);
    assert(!r.blocked() && t.events.back().peer.handle==1 && t.events.back().report.consumer==8);
    // Pending taps addressed to host 2 are discarded on a switch.
    t.keysReady=false; r.update(pressed(7)); r.update({});
    assert(r.route({2,1},t)); t.keysReady=true; t.events.clear(); r.update({}); r.poll(20004,t);
    for (auto& e:t.events) assert(e.peer.handle==2 && equal(e.report,Reports{}));
    // Recycled handles are new endpoints and must never receive an old release/report.
    t.livePeers[2]={2,2}; t.events.clear();
    assert(r.route({2,2},t)); r.update(pressed()); r.poll(20005,t);
    for (auto& e:t.events) assert(!e.release && equal(e.report,Reports{}));
    assert(r.route({},t)); r.update(pressed()); t.events.clear(); r.poll(20006,t);
    assert(t.events.empty());
}
static void persistence() {
    FactoryKeyboard keyboard; HostProfiles hosts; SaveSnapshot snapshot;
    assert(!snapshot.capture(keyboard,hosts));
    hosts.connect(identity(1),{0,1}); assert(snapshot.capture(keyboard,hosts) && snapshot.profiles());
    hosts.connect(identity(2),{1,1}); snapshot.accept(keyboard,hosts); assert(hosts.dirty());
    assert(snapshot.capture(keyboard,hosts)); snapshot.accept(keyboard,hosts); assert(!hosts.dirty());
    hosts.select(2); assert(snapshot.capture(keyboard,hosts)); snapshot.accept(keyboard,hosts);
    HostProfiles restored; assert(restored.restore(snapshot.data(),snapshot.size()) && restored.selected()==2);
    // Switching host preserves held physical keys for the release barrier and cancels playback.
    uint8_t rows[kRows] = {};
    keyboard.update(rows,0);
    for (unsigned i=0;i<kRows*8;++i) if (kKeys[i].kind==1 && kKeys[i].value==4) rows[i/8] |= 1u<<(i%8);
    keyboard.update(rows,20); keyboard.prepareHostChange(30);
    assert(!equal(keyboard.report(),Reports{}));
}
int main() { profiles(); taps(); routing(); persistence(); puts("PASS: profiles, double taps, exclusive HID routing, persistence"); }
