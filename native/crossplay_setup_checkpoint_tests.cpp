#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_setup_checkpoint_native.h"
#include <cstdio>
#include <cstdlib>
namespace {
namespace C=CrossplaySetupCheckpoint;
unsigned checks=0;
void Check(bool value){++checks;if(!value){std::printf("FAIL setup checkpoint %u\n",checks);std::exit(1);}}
C::Snapshot Snapshot(unsigned char value) {
    C::Snapshot s{};s.writers.digest.fill(3);s.writers.rules=2152;s.writers.players=41;
    s.local.fill(value);s.session=100+value;s.preparation=200+value;s.cache=300+value;s.thread=value;return s;
}
struct Pair {
    C::Checkpoint h,g;
    C::Snapshot hs=Snapshot(1),gs=Snapshot(2);
    Crossplay::Peer hp{},gp{};
    C::Nonce hn{},gn{};
    std::vector<unsigned char> offer,ack,confirm,empty;
    Pair(){hp.local_id.fill(11);hp.remote_id.fill(22);gp.local_id=hp.remote_id;gp.remote_id=hp.local_id;hn.fill(33);gn.fill(44);}
    void Bind(){Check(h.Bind(hp,true,1,hs,hn,100));Check(g.Bind(gp,false,1,gs,gn,100));}
    void Offer(){Check(h.Offer(hs,101,offer));Check(h.Sent(hs,102));}
    void Ack(){Check(g.Receive(gs,103,offer,ack));Check(g.GetPhase()==C::Phase::AckPending);Check(g.Sent(gs,104));}
    void Confirm(){Check(h.Receive(hs,105,ack,confirm));Check(h.GetPhase()==C::Phase::ConfirmPending);Check(h.Sent(hs,106));}
    void Finish(){Check(g.Receive(gs,107,confirm,empty));Check(empty.empty());Check(h.Confirmed(hs,108));Check(g.Confirmed(gs,108));}
};
}
int main(){
    {Pair p;p.Bind();p.Offer();p.Ack();p.Confirm();p.Finish();
        Check(!p.h.Confirmed(p.hs,10101) && p.h.GetPhase()==C::Phase::Failed);
        Check(!p.g.Confirmed(p.gs,10101) && p.g.GetPhase()==C::Phase::Failed);}
    {Pair p;p.Bind();p.Offer();p.Ack();p.Confirm();p.Finish();
        Check(p.h.Confirmed(p.hs,10100));Check(p.g.Confirmed(p.gs,10100));}
    // Changing any framed field cannot complete the original transcript on
    // both peers, including challenges whose validity is only known on echo.
    for(unsigned stage=0;stage<3;++stage)for(size_t at=0;at<C::FrameSize;++at) {
        Pair p;p.Bind();p.Offer();bool okay=true;
        if(stage==0)p.offer[at]^=0x80;
        okay=p.g.Receive(p.gs,103,p.offer,p.ack);
        if(okay)okay=p.g.Sent(p.gs,104);
        if(okay && stage==1)p.ack[at]^=0x80;
        if(okay)okay=p.h.Receive(p.hs,105,p.ack,p.confirm);
        if(okay)okay=p.h.Sent(p.hs,106);
        if(okay && stage==2)p.confirm[at]^=0x80;
        if(okay)okay=p.g.Receive(p.gs,107,p.confirm,p.empty);
        Check(!okay);Check(!(p.h.Confirmed(p.hs,108) && p.g.Confirmed(p.gs,108)));
    }
    for(unsigned stage=0;stage<3;++stage)for(size_t length=0;length<=C::FrameSize+1;++length) {
        if(length==C::FrameSize)continue;
        Pair p;p.Bind();p.Offer();if(stage>=1)p.Ack();if(stage>=2)p.Confirm();
        auto frame=stage==0?p.offer:stage==1?p.ack:p.confirm;frame.resize(length);
        auto& receiver=stage==1?p.h:p.g;auto& snapshot=stage==1?p.hs:p.gs;
        p.empty.assign(1,99);Check(!receiver.Receive(snapshot,107,frame,p.empty));
        Check(p.empty.empty() && receiver.GetPhase()==C::Phase::Failed);
    }
    // Local native identity and unknown bytes are process-specific. They may
    // differ across peers, but any local change retires that owned interval.
    for(unsigned field=0;field<10;++field)for(unsigned stage=0;stage<4;++stage) {
        Pair p;p.Bind();if(stage>=1)p.Offer();if(stage>=2)p.Ack();if(stage>=3)p.Confirm();
        const bool host=stage==0 || stage==2;auto changed=host?p.hs:p.gs;
        switch(field){case 0:++changed.thread;break;case 1:++changed.session;break;
            case 2:++changed.preparation;break;case 3:++changed.cache;break;
            case 4:changed.local[7]^=1;break;case 5:changed.writers.digest[3]^=1;break;
            case 6:++changed.writers.rules;break;case 7:++changed.writers.players;break;
            case 8:changed.local={};break;default:changed.writers={};break;}
        auto& receiver=host?p.h:p.g;
        const bool okay=stage==0?receiver.Offer(changed,101,p.empty):
            receiver.Receive(changed,107,stage==1?p.offer:stage==2?p.ack:p.confirm,p.empty);
        Check(!okay && p.empty.empty() && receiver.GetPhase()==C::Phase::Failed);
        Check(!receiver.Confirmed(host?p.hs:p.gs,108));
    }
    for(unsigned field=0;field<10;++field) {
        Pair p;auto s=p.hs;auto peer=p.hp;auto nonce=p.hn;uint32_t revision=1;
        switch(field){case 0:s.thread=0;break;case 1:s.session=0;break;case 2:s.preparation=0;break;
            case 3:s.cache=0;break;case 4:s.local={};break;case 5:s.writers.rules=0;break;
            case 6:nonce={};break;case 7:revision=0;break;case 8:peer.local_id={};break;default:peer.remote_id=peer.local_id;break;}
        Check(!p.h.Bind(peer,true,revision,s,nonce,100));Check(p.h.GetPhase()==C::Phase::Failed);
        Check(!p.h.Bind(p.hp,true,1,p.hs,p.hn,100));
    }
    {Pair p;p.Bind();Check(!p.h.Sent(p.hs,101));Check(p.h.GetPhase()==C::Phase::Failed);}
    {Pair p;p.Bind();Check(!p.g.Sent(p.gs,101));}
    {Pair p;p.Bind();Check(p.h.Offer(p.hs,101,p.offer));Check(!p.h.Offer(p.hs,102,p.empty));Check(p.empty.empty());}
    {Pair p;p.Bind();p.Offer();Check(!p.h.Sent(p.hs,103));}
    {Pair p;p.Bind();p.Offer();p.Ack();Check(!p.g.Receive(p.gs,105,p.offer,p.empty));}
    {Pair p;p.Bind();p.Offer();p.Ack();p.Confirm();p.Finish();Check(!p.g.Receive(p.gs,109,p.confirm,p.empty));}
    {Pair p;p.Bind();p.Offer();Check(!p.h.Receive(p.hs,103,p.offer,p.empty));}
    {Pair p;p.Bind();p.Offer();p.Ack();Check(p.h.Receive(p.hs,105,p.ack,p.empty));p.h.Invalidate();Check(!p.h.Sent(p.hs,106));}
    {Pair p;p.Bind();Check(!p.h.Offer(p.hs,99,p.empty));Check(p.empty.empty());}
    {Pair p;p.Bind();p.Offer();Check(!p.g.Receive(p.gs,10101,p.offer,p.empty));}
    {Pair p;p.Bind();p.Offer();p.Ack();Check(!p.h.Receive(p.hs,10101,p.ack,p.empty));}
    {Pair p;p.Bind();p.Offer();p.Ack();p.Confirm();Check(!p.g.Receive(p.gs,10101,p.confirm,p.empty));}
    {Pair p;p.Bind();Check(!p.h.Bind(p.hp,true,2,p.hs,p.hn,101));}
    // New intervals reject the old transcript even with the same identities,
    // revision and writer bytes: the fresh challenge must be echoed exactly.
    {Pair old;old.Bind();old.Offer();old.Ack();old.Confirm();old.Finish();
        Pair fresh;fresh.hn.fill(55);fresh.gn.fill(66);fresh.Bind();fresh.Offer();
        Check(!fresh.h.Receive(fresh.hs,105,old.ack,fresh.empty));
        fresh.Ack();Check(!fresh.g.Receive(fresh.gs,107,old.confirm,fresh.empty));}
    {Pair p;p.gn=p.hn;p.Bind();p.Offer();Check(!p.g.Receive(p.gs,103,p.offer,p.empty));}
    // The native reader must reject an unbound participant without touching
    // an assumed game's absolute memory. Production adapters do not include it.
    {CrossplayNative::ParticipantEvent binding;C::Snapshot s=Snapshot(1);
        Check(!CrossplaySetupCheckpointNative::Capture(Crossplay::Edition::Gold,binding,{1,2},true,s));
        Check(!C::Valid(s));
        CrossplaySetupCheckpointNative::Checkpoint c;std::vector<unsigned char> out{1};
        Check(!c.Offer(binding,out) && out.empty());Check(c.GetPhase()==C::Phase::Failed);}
    std::printf("PASS %u setup checkpoint checks; diagnostic only, no game started.\n",checks);
}
