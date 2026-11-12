#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_player_option_codec.h"
#include <stdio.h>
using namespace CrossplayPlayerOption;
#define ORACLE(name,klass) extern "C" __declspec(dllexport) uint32_t __cdecl name(uint32_t edition) {CrossplayNativeCodec::PlayerOptionDetail::Spec spec{};return CrossplayNativeCodec::PlayerOptionDetail::Layout(static_cast<Crossplay::Edition>(edition),klass,spec) && CrossplayNativeCodec::PlayerOptionDetail::Methods(static_cast<Crossplay::Edition>(edition),spec)?1u:0u;}
ORACLE(WePlayColorMethodsOracle,Color)
ORACLE(WePlayTeamMethodsOracle,Team)
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayColorSetterMethodsOracle(uint32_t edition) {
    return CrossplayNativeCodec::PlayerOptionDetail::ColorSetterMethods(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
static std::vector<unsigned char> Payload(uint32_t klass,uint32_t slot,uint32_t value) {
    std::vector<unsigned char> wire(klass==Color?21:18,0);
    CrossplayChat::Put(wire.data(),klass);CrossplayChat::Put(wire.data()+4,7u|((klass==Color?0x32u:0x1eu)<<16));
    CrossplayChat::Put(wire.data()+8,42);wire[12]=1;CrossplayChat::Put(wire.data()+13,slot);
    if(klass==Color)CrossplayChat::Put(wire.data()+17,value);else wire[17]=static_cast<unsigned char>(value);
    return wire;
}
int main() {
    unsigned checks=0;auto check=[&](bool okay){++checks;if(!okay){fprintf(stderr,"FAIL player option check %u\n",checks);exit(1);}};
    for(uint32_t klass:{Color,Team})for(uint32_t slot=0;slot<=3;++slot)for(uint32_t value=0;value<=9;++value) {
        const auto wire=Payload(klass,slot,value);Value parsed{1,1,1};
        const bool valid=slot>=1 && slot<=2 && (klass==Color?(value>=2 && value<=7):(value>=1 && value<=4));
        check(Inspect(wire,42,parsed)==valid);
        check(valid?(parsed.klass==klass && parsed.slot==slot && parsed.value==value):(!parsed.klass && !parsed.slot && !parsed.value));
    }
    Crossplay::Peer host{},guest{};
    host.edition=Crossplay::Edition::UbisoftHistory;guest.edition=Crossplay::Edition::Gold;
    host.local_id[0]=1;host.remote_id[0]=2;guest.local_id=host.remote_id;guest.remote_id=host.local_id;
    for(uint32_t klass:{Color,Team}) {
        const auto wire=Payload(klass,1,3);Value parsed;
        for(size_t size=0;size<26;++size){auto bad=wire;bad.resize(size);check(Inspect(bad,42,parsed)==(size==wire.size()));}
        for(size_t at=0;at<wire.size();++at) {
            if(at>=13 && at<=17)continue;
            auto bad=wire;bad[at]^=0x80;check(!Inspect(bad,42,parsed));
        }
        check(!Inspect(wire,UINT32_MAX,parsed));check(!Inspect(wire,43,parsed));
        std::vector<unsigned char> frame,decoded;
        check(Encode(host,42,1,wire,frame));check(Decode(guest,42,123,0,frame,decoded));
        check(Inspect(decoded,123,parsed) && parsed.klass==klass && parsed.slot==1 && parsed.value==3);
        for(size_t size=0;size<Header+24;++size) {
            auto bad=frame;bad.resize(size);decoded={9};
            const bool accepted=Decode(guest,42,123,0,bad,decoded);
            check(accepted==(size==frame.size()));check(accepted || decoded.empty());
        }
        for(size_t at=0;at<Header;++at){auto bad=frame;bad[at]^=0x80;decoded={9};check(!Decode(guest,42,123,0,bad,decoded) && decoded.empty());}
        for(uint32_t previous:{1u,2u,UINT32_MAX})check(!Decode(guest,42,123,previous,frame,decoded));
        check(!Decode(guest,43,123,0,frame,decoded));check(!Decode(guest,42,UINT32_MAX,0,frame,decoded));
        auto wrong=guest;wrong.edition=Crossplay::Edition::UbisoftHistory;check(!Decode(wrong,42,123,0,frame,decoded));
        check(!Encode(host,42,0,wire,frame) && frame.empty());
        wrong=host;wrong.edition=Crossplay::Edition::Gold;check(!Encode(wrong,42,1,wire,frame) && frame.empty());
        wrong=host;wrong.edition=Crossplay::Edition::SteamHistory;check(Encode(wrong,42,1,wire,frame));
        // Foreign slot and out-of-range values cannot pass a valid envelope.
        for(uint32_t bad_slot:{0u,3u,UINT32_MAX}) {
            auto bad=frame;CrossplayChat::Put(bad.data()+Header+13,bad_slot);check(!Decode(guest,42,123,0,bad,decoded));
        }
    }
    using namespace CrossplayNativeCodec;
    for(unsigned edition=1;edition<=3;++edition)for(uint32_t klass:{Color,Team}) {
        PlayerOptionDetail::Spec spec{};check(PlayerOptionDetail::Layout(static_cast<Crossplay::Edition>(edition),klass,spec));
        check(spec.width==(klass==Color?4u:1u));
        check(!PlayerOptionDetail::Layout(static_cast<Crossplay::Edition>(edition),0x0ea630cc,spec) && !spec.klass);
    }
    check(!WePlayColorMethodsOracle(0) && !WePlayTeamMethodsOracle(4));
    check(!WePlayColorSetterMethodsOracle(0) && !WePlayColorSetterMethodsOracle(1) && !WePlayColorSetterMethodsOracle(4));
    CrossplayNative::ParticipantEvent empty;HistoryPlayerOptionReceiver receiver;
    check(receiver.Observe(empty)==ReadyObservation::Invalid);
    check(!receiver.Deliver(Crossplay::Edition::UbisoftHistory,empty,{42,43},Payload(Color,1,5)));
    check(!receiver.Deliver(Crossplay::Edition::UbisoftHistory,empty,{42,43},Payload(Color,1,5)));
    printf("PASS %u bounded host Color/Team payload, frame and exclusion checks. No game/world executed.\n",checks);return 0;
}
