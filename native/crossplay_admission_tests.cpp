#include "crossplay_admission.cpp"
#include <cstdio>
#include <cstdlib>
using namespace CrossplayNative;
namespace {
unsigned checks=0;
void Check(bool okay,const char* reason) {
    ++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}
}
Crossplay::Peer Reverse(const Crossplay::Peer& peer) {
    auto result=peer;result.local_id=peer.remote_id;result.remote_id=peer.local_id;return result;
}
}
int main() {
    Crossplay::Peer peer{};peer.local_id.fill(0x11);peer.remote_id.fill(0x22);
    const auto reverse=Reverse(peer);
    Hello hello{0,13,0,"Crossplay",{"woodland",{{"Keep",UINT32_MAX,0},{"Tower",0,3}}}};
    for(bool host:{false,true}) {
        for(auto type:{Message::Hello,Message::Assign,Message::Applied,Message::Commit,Message::Committed}) {
            auto frame=Frame(type,host,peer);Message observed{};
            Check(frame.size()==FrameHeader && Header(frame,!host,reverse,observed) && observed==type,"reciprocal channel ownership and leader");
            Check(!Header(frame,host,reverse,observed),"two hosts/two guests excluded");
            Check(!Header(frame,!host,peer,observed),"reversed identities excluded");
            for(size_t at=0;at<FrameHeader;++at) {
                auto changed=frame;changed[at]^=0x80;
                Check(!Header(changed,!host,reverse,observed),"changed epoch/identity/protocol header excluded");
            }
        }
        auto frame=Frame(Message::Hello,host,peer);Check(AppendHello(frame,hello),"native lobby hello encoded");
        Hello decoded;
        Check(ReadHello(frame,Crossplay::Edition::UbisoftHistory,decoded) && decoded.connection==0
            && decoded.name==hello.name && decoded.extra==13 && decoded.principal==0 && decoded.castle.parts.size()==2,"History zero CID/principal and canonical castle decoded");
        Check(!ReadHello(frame,Crossplay::Edition::Gold,decoded),"zero Gold source connection excluded");
        for(size_t end=0;end<frame.size();++end) {
            const std::vector<unsigned char> truncated(frame.begin(),frame.begin()+end);
            Check(!ReadHello(truncated,Crossplay::Edition::UbisoftHistory,decoded),"truncated hello excluded");
        }
        auto changed=frame;changed.push_back(0);Check(!ReadHello(changed,Crossplay::Edition::UbisoftHistory,decoded),"trailing hello data excluded");
        changed=frame;changed[FrameHeader+12]=65;Check(!ReadHello(changed,Crossplay::Edition::UbisoftHistory,decoded),"unbounded display name excluded");
        changed=frame;changed[7]=1;Message old{};
        Check(!Header(changed,!host,reverse,old),"old admission version cannot silently lose principal");
        for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}) {
            for(uint32_t principal:{0u,1u,2u,2028987018u,0x80000001u,UINT32_MAX-1}) {
                auto offered=hello;offered.connection=7;offered.principal=principal;
                auto encoded=Frame(Message::Hello,host,peer);
                Check(AppendHello(encoded,offered) && ReadHello(encoded,edition,decoded)
                    && decoded.principal==principal && decoded.connection==7,"source principal stays independent of CID/role/edition");
            }
            changed=frame;for(size_t at=FrameHeader+8;at<FrameHeader+12;++at)changed[at]=0xff;
            Check(!ReadHello(changed,edition,decoded),"sentinel principal excluded before native construction");
        }
        auto invalid=hello;invalid.principal=UINT32_MAX;
        auto invalid_frame=Frame(Message::Hello,host,peer);
        Check(!AppendHello(invalid_frame,invalid),"sentinel principal cannot be offered");
    }
    for(auto edition:{Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}) {
        Check(Mixed(Crossplay::Edition::Gold,edition) && Mixed(edition,Crossplay::Edition::Gold),"both History launch profiles supported");
        Check(!Mixed(edition,edition),"same-edition bridge session excluded");
    }
    for(const auto& name:{std::string(),std::string(65,'x'),std::string("bad\0name",8),std::string("bad\nname")}) {
        auto frame=Frame(Message::Hello,true,peer);auto invalid=hello;invalid.name=name;
        Check(!AppendHello(frame,invalid),"invalid native name cannot be offered");
    }
    Crossplay::Channel channel;LobbyAdmission admission;
    Check(!admission.Begin(channel,Crossplay::Edition::Gold,true,nullptr,0)
        && admission.GetState()==AdmissionState::Failed,"unauthenticated channel cannot begin native admission");
    Check(!admission.Poll(),"failed admission does not pump native callbacks");
    for(bool host:{false,true}) {
        const Participant own{0,host?1u:2u,0,0,"Local",host,true};
        const Participant remote{1,host?2u:1u,2,1,"Remote",!host,false};
        LobbySnapshot snapshot{123,own.slot,host,{{own,2},{remote,2}}};
        Check(BoundRoster(snapshot,own,remote,host),"committed native roster retains channel ownership");
        for(unsigned change=0;change<12;++change) {
            auto changed=snapshot;
            switch(change) {
            case 0: changed.members.pop_back();break;
            case 1: changed.members.push_back({remote,2});break;
            case 2: changed.local_slot=remote.slot;break;
            case 3: changed.host=!host;break;
            case 4: changed.members[0].status=3;break;
            case 5: changed.members[1].status=6;break;
            case 6: changed.members[1].player.connection=2;break;
            case 7: changed.members[1].player.name="Replacement";break;
            case 8: changed.members[1].player.slot=own.slot;break;
            case 9: changed.members[1].player.principal=3;break;
            case 10: changed.members[1].player.station=2;break;
            default: changed.members[1].player.host=host;break;
            }
            Check(!BoundRoster(changed,own,remote,host),"departed/replaced/reassigned roster invalidates committed binding");
        }
    }
    for(bool host:{false,true}) {
        const uint32_t own_slot=host?1u:2u,remote_slot=host?2u:1u;
        for(uint32_t local:{0u,1u,2u,2028987018u,UINT32_MAX}) {
            for(uint32_t foreign:{0u,1u,2u,2028987018u,UINT32_MAX}) {
                const Participant own{11,own_slot,local,11,"Local",host,true};
                const Participant remote{12,remote_slot,foreign,12,"Remote",!host,false};
                const uint32_t a=local?local:own_slot,b=foreign?foreign:remote_slot;
                Check(DistinctPlayers(own,remote)==(a!=UINT32_MAX && b!=UINT32_MAX && a!=b),
                    "native preparation fallback collisions excluded independently of routing slots");
            }
        }
    }
    std::printf("PASS %u admission framing/ownership checks; no game was launched.\n",checks);
}
