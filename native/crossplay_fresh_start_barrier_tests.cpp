#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_fresh_start_barrier.h"
#include <stdio.h>
#include <stdlib.h>

namespace {
void Check(bool okay,const char* message){if(!okay){fprintf(stderr,"FAIL %s\n",message);exit(1);}}
struct Pair {
    CrossplayFreshStart::Barrier host,guest;
    Crossplay::Peer hp{},gp{};
    CrossplayGameStart::Agreement agreement{};
    Crossplay::Digest content{};
    std::vector<unsigned char> hr,gr,start,queued,wire;
    Pair() {
        hp.local_id[0]=1;hp.remote_id[0]=2;gp.local_id=hp.remote_id;gp.remote_id=hp.local_id;
        agreement.revision=7;agreement.setup[0]=3;content[0]=4;
        Check(host.Bind(hp,true,agreement,content,100) && guest.Bind(gp,false,agreement,content,100),"bind actual setup/content");
    }
    void Ready() {
        Check(host.Ready(101,hr) && guest.Ready(101,gr),"emit readiness receipts");
        Check(host.Sent(102) && guest.Sent(102),"only accepted sends advance readiness");
        Check(host.ReceiveReady(103,gr) && guest.ReceiveReady(103,hr),"mutual setup and content receipts");
    }
    void Start() {
        wire.assign(CrossplayGameStart::WireSize,0);
        CrossplayChat::Put(wire.data(),CrossplayGameStart::Class);CrossplayChat::Put(wire.data()+4,CrossplayGameStart::Type);
        CrossplayChat::Put(wire.data()+8,42);wire[12]=1;memcpy(wire.data()+13,"Str_",4);
        Check(host.Start(104,wire,42,start) && host.Sent(105),"captured original start sent once");
    }
};
}
int main() {
    using namespace CrossplayFreshStart;
    {
        Barrier empty;Check(!empty.CanBufferWorldEvents(100) && empty.GetPhase()==Phase::New,"unbound events cannot open a start interval");
        Pair p;Check(!p.host.CanBufferWorldEvents(100),"setup binding alone cannot admit gameplay events");
        p.Ready();Check(!p.host.CanBufferWorldEvents(103) && !p.guest.CanBufferWorldEvents(103),"mutual setup agreement alone cannot admit gameplay events");
        p.Start();Check(p.host.CanBufferWorldEvents(106) && p.host.GetPhase()==Phase::AwaitQueued,"early guest events may be buffered while host awaits the real queue receipt");
        Check(!p.guest.CanBufferWorldEvents(106),"guest cannot buffer before accepting the original start");
        std::vector<unsigned char> wire;
        Check(p.guest.GuestAttempt(107,p.start,42,0,wire) && p.guest.CanBufferWorldEvents(108),"guest native attempt owns a bounded buffer interval");
        Check(p.guest.GuestQueued(109,true,p.queued) && p.guest.Sent(110) && p.guest.CanBufferWorldEvents(111),"completed guest queue keeps its bounded interval");
        Check(p.host.GetPhase()==Phase::AwaitQueued,"buffer admission never fabricates a native queue receipt");
        Check(p.host.HostAttempt(112,p.queued) && p.host.CanBufferWorldEvents(113),"host attempt may buffer without advancing its phase");
        Check(p.host.HostQueued(114,true) && p.host.CanBufferWorldEvents(115),"completed original host queue remains eligible within its lifetime");
        Pair expired;expired.Ready();expired.Start();Check(!expired.host.CanBufferWorldEvents(10101) && expired.host.GetPhase()==Phase::Failed,"early gameplay buffering expires with the original rendezvous");
        Pair backward;backward.Ready();backward.Start();Check(!backward.host.CanBufferWorldEvents(104),"backward buffer timestamps invalidate the interval");
    }
    {
        Pair p;p.Ready();p.Start();std::vector<unsigned char> wire;
        Check(p.guest.GuestAttempt(106,p.start,42,0,wire),"guest accepts captured original event");
        Check(CrossplayGameStart::Inspect(wire,0),"only local CID projected");
        auto expected=p.wire;CrossplayChat::Put(expected.data()+8,0);Check(wire==expected,"native type async and path preserved");
        Check(p.guest.GuestQueued(107,true,p.queued) && p.guest.Sent(108),"original guest send returned; transport receipt sent");
        Check(p.host.HostAttempt(109,p.queued) && p.host.HostQueued(110,true),"host original send follows exact guest queue receipt");
        Check(p.host.GetPhase()==Phase::Complete && p.guest.GetPhase()==Phase::Complete,"completed local enqueue rendezvous");
        Check(!p.host.HostAttempt(111,p.queued),"no repeated host queue attempt");
        Check(!p.guest.GuestAttempt(111,p.start,42,0,wire) && wire.empty(),"no repeated guest queue attempt");
    }
    // Mutate every byte, including magic, identities, revision, setup, content,
    // and role. Neither receipt accepts an unaffiliated agreement.
    for(size_t i=0;i<ReceiptSize;++i) {
        Pair p;Check(p.host.Ready(101,p.hr) && p.host.Sent(102),"host ready for mutation");
        Check(p.guest.Ready(101,p.gr),"guest emits mutation source");p.gr[i]^=1;
        Check(!p.host.ReceiveReady(103,p.gr),"every readiness byte bound");
        Pair q;q.Ready();q.Start();std::vector<unsigned char> w;
        Check(q.guest.GuestAttempt(106,q.start,42,0,w) && q.guest.GuestQueued(107,true,q.queued),"queue mutation source");
        q.queued[i]^=1;Check(!q.host.HostAttempt(108,q.queued),"every queue receipt byte bound");
    }
    for(auto time:{99ull,10101ull}) {
        Pair p;Check(!p.host.Ready(time,p.hr) && p.hr.empty(),"backward/expired lifetime excludes start");
    }
    for(size_t i=0;i<CrossplayGameStart::FrameSize;++i) {
        Pair p;p.Ready();p.Start();p.start[i]^=1;std::vector<unsigned char> w;
        Check(!p.guest.GuestAttempt(106,p.start,42,0,w) && w.empty(),"every captured-start byte bound before native queue");
        Check(!p.guest.GuestAttempt(107,p.start,42,0,w),"invalid first attempt cannot be retried");
    }
    {
        Pair p;CrossplayFreshStart::Barrier no_content,no_setup,no_peer;
        auto c=p.content;c.fill(0);auto a=p.agreement;a.revision=0;auto peer=p.hp;peer.remote_id=peer.local_id;
        Check(!no_content.Bind(p.hp,true,p.agreement,c,100),"zero content contract excluded independently");
        Check(!no_setup.Bind(p.hp,true,a,p.content,100),"missing setup revision excluded independently");
        Check(!no_peer.Bind(peer,true,p.agreement,p.content,100),"aliased peer excluded independently");
        p.Ready();p.Start();std::vector<unsigned char> w;
        Check(!p.host.HostAttempt(106,p.gr),"readiness cannot substitute for native queue receipt");
        Check(!p.guest.GuestAttempt(10101,p.start,42,0,w) && w.empty(),"expired start cannot call native sender");
    }
    {
        Pair p;Check(!p.host.Sent(101),"no unsolicited sent notification");
        Pair q;q.Ready();q.Start();std::vector<unsigned char> w;
        Check(!q.guest.GuestQueued(106,true,q.queued),"no queue receipt before attempt");
        Pair r;r.Ready();r.Start();Check(r.guest.GuestAttempt(106,r.start,42,0,w),"one guest attempt");
        Check(!r.guest.GuestQueued(107,false,r.queued) && r.queued.empty(),"native send failure cannot acknowledge");
        Pair s;s.Ready();s.Start();s.start.back()=1;
        Check(!s.guest.GuestAttempt(106,s.start,42,0,w) && w.empty(),"resume or malformed native event excluded");
        Pair t;t.Ready();Check(!t.host.ReceiveReady(104,t.gr),"duplicate ready receipt excluded");
        CrossplayFreshStart::Barrier empty;Crossplay::Peer peer{};CrossplayGameStart::Agreement a{};Crossplay::Digest c{};
        Check(!empty.Bind(peer,true,a,c,100),"missing content/setup/identities excluded");
    }
    puts("PASS original fresh-event rendezvous; exhaustive receipt mutations, one-shot queue order, failure/replay/deadline exclusion. No native game or world executed.");
    return 0;
}
