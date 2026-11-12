#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_game_start_codec.h"
#include <cstdio>
#include <cstdlib>
#include <array>

extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGameStartMethodsOracle(uint32_t value) {
    return CrossplayNativeCodec::GameStartDetail::Methods(static_cast<Crossplay::Edition>(value))?1u:0u;
}
namespace {
unsigned checks=0;
void Check(bool value,const char* why) {++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
std::vector<unsigned char> Start(uint32_t host) {
    std::vector<unsigned char> wire(CrossplayGameStart::WireSize,0);
    CrossplayChat::Put(wire.data(),CrossplayGameStart::Class);CrossplayChat::Put(wire.data()+4,CrossplayGameStart::Type);
    CrossplayChat::Put(wire.data()+8,host);wire[12]=1;memcpy(wire.data()+13,"Str_",4);return wire;
}
}
int main() {
    using namespace CrossplayGameStart;
    Crossplay::Peer peer;peer.local_id.fill(17);peer.remote_id.fill(34);
    auto reverse=peer;std::swap(reverse.local_id,reverse.remote_id);
    Agreement agreed;agreed.revision=3;agreed.setup.fill(0x56);
    const auto source=Start(42);std::vector<unsigned char> frame,wire;
    const std::vector<unsigned char> native_fixture={0xe9,0x0a,0xf0,0x8b,7,0,7,0,42,0,0,0,1,'S','t','r','_',0,0,0,0};
    Check(source==native_fixture,"independent original constructor/writer fixture from all three images");
    Check(Inspect(source,42),"fixed original fresh-start class, type, CID, async and empty path");
    Check(!Inspect(source,0) && !Inspect(Start(UINT32_MAX),UINT32_MAX),"foreign or missing host excluded");
    Check(Encode(peer,agreed,source,42,frame) && Decode(reverse,agreed,frame,42,0,wire),"reciprocal setup-bound frame supports local CID zero");
    Check(wire==Start(0),"only admitted host CID changes");
    Check(Encode(reverse,agreed,wire,0,frame) && Decode(peer,agreed,frame,0,42,wire) && wire==source,"roundtrip preserves every original event byte");
    for(size_t i=0;i<source.size();++i) {
        auto changed=source;changed[i]^=0x80;
        Check(!Inspect(changed,42),"every changed event field rejected");
        Check(!Inspect(std::vector<unsigned char>(source.begin(),source.begin()+i),42),"every truncated event rejected");
    }
    auto changed=source;changed.push_back(0);Check(!Inspect(changed,42),"trailing event rejected");
    changed=source;CrossplayChat::Put(changed.data()+17,1);changed.push_back(0);Check(!Inspect(changed,42),"saved-game path is outside fresh start");
    Check(Encode(peer,agreed,source,42,frame),"restore valid frame");
    for(size_t i=0;i<frame.size();++i) {
        changed=frame;changed[i]^=0x80;wire={1,2,3};
        Check(!Decode(reverse,agreed,changed,42,0,wire) && wire.empty(),"ownership, revision, setup and event mutation clears output");
        Check(!Decode(reverse,agreed,std::vector<unsigned char>(frame.begin(),frame.begin()+i),42,0,wire),"every truncated frame excluded");
    }
    changed=frame;changed.push_back(0);Check(!Decode(reverse,agreed,changed,42,0,wire),"trailing frame excluded");
    auto stale=agreed;++stale.revision;Check(!Decode(reverse,stale,frame,42,0,wire),"stale setup revision excluded");
    stale=agreed;stale.setup[31]^=1;Check(!Decode(reverse,stale,frame,42,0,wire),"different actual setup digest excluded");
    for(const Agreement invalid:{Agreement{},Agreement{3,{}}}) {
        Check(!Encode(peer,invalid,source,42,wire) && wire.empty(),"missing agreement excluded before encode");
        Check(!Decode(reverse,invalid,frame,42,0,wire) && wire.empty(),"missing agreement excluded before decode");
    }
    CrossplayNative::ParticipantEvent unbound;
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}) {
        wire={1};Check(!CrossplayNativeCodec::EncodeFreshGameStart(edition,unbound,{42,0},"Maps/Test",nullptr,wire) && wire.empty(),"unbound process cannot encode or call native code");
        CrossplayNativeCodec::FreshGameStartReceiver receiver;
        Check(!receiver.Bind(edition,unbound,{42,0},reverse,42,agreed,"Maps/Test"),"unbound process cannot own a start receiver");
        Check(!receiver.Deliver(unbound,frame) && !receiver.Deliver(unbound,frame),"unbound receiver cannot deliver or retry a start");
    }
    for(uint32_t invalid:{0u,4u,UINT32_MAX})Check(!WePlayGameStartMethodsOracle(invalid),"invalid edition excluded before native read");
    {
        using Rows=std::array<unsigned char,8*0x18>;Rows rows{};
        const CrossplaySessionConfig::Connections ids{42,0};
        for(size_t i=0;i<8;++i){auto* p=rows.data()+i*0x18;CrossplayChat::Put(p,i<2?3u:i<4?2u:0u);p[4]=i<4?1u:0u;CrossplayChat::Put(p+8,i<4?static_cast<uint32_t>(i+1):UINT32_MAX);}
        auto valid=[&](const Rows& value){return CrossplayNativeCodec::GameStartDetail::FreshReadySlots(value.data(),value.size(),ids);};
        Check(valid(rows),"two ready humans and original closed neutral slots accepted");
        for(size_t i=0;i<2;++i){auto altered=rows;CrossplayChat::Put(altered.data()+i*24,4);Check(valid(altered),"admitted alternate human state preserved");}
        {auto altered=rows;CrossplayChat::Put(altered.data()+16,42);Check(valid(altered),"bound human routing CID accepted");}
        for(size_t i=0;i<4;++i)for(size_t offset:{size_t(0),size_t(4),size_t(8),size_t(16)}) {
            auto altered=rows;altered[i*24+offset]^=0x80;Check(!valid(altered),"invalid state, readiness, assignment or routing excluded");
        }
        for(size_t i=2;i<4;++i)for(size_t offset:{size_t(12),size_t(20)}){auto altered=rows;altered[i*24+offset]=1;Check(!valid(altered),"closed neutral retains neither privacy nor old participant");}
        for(size_t i=2;i<8;++i)for(uint32_t state:{0u,1u,2u,3u,4u,5u}) {
            auto altered=rows;auto* p=altered.data()+i*24;CrossplayChat::Put(p,state);p[4]=state==2?1u:0u;CrossplayChat::Put(p+8,state==2?static_cast<uint32_t>(i+1):UINT32_MAX);
            Check(valid(altered)==(state==0 || state==5 || (state==2 && i<4)),"closed neutral restricted to slots three/four; open and AI states excluded");
        }
        Check(!CrossplayNativeCodec::GameStartDetail::FreshReadySlots(nullptr,rows.size(),ids),"missing cache excluded");
        Check(!CrossplayNativeCodec::GameStartDetail::FreshReadySlots(rows.data(),rows.size()-1,ids),"short cache excluded");
        Check(!CrossplayNativeCodec::GameStartDetail::FreshReadySlots(rows.data(),rows.size(),{42,42}),"aliased identities excluded");
        Check(!CrossplayNativeCodec::GameStartDetail::FreshReadySlots(rows.data(),rows.size(),{UINT32_MAX,0}),"missing identity excluded");
    }
    std::printf("PASS %u fresh-start scope, payload, revision, setup and native-binding checks; no game started.\n",checks);
}
