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
    Hello hello{0,13,0,"Crossplay",{"woodland",{{"Keep",UINT32_MAX,0},{"Tower",0,3}}},"CustomCastle"};
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
            && decoded.name==hello.name && decoded.extra==13 && decoded.principal==0 && decoded.castle.parts.size()==2
            && decoded.castle_type==hello.castle_type,"History zero CID/principal and both native castle strings decoded");
        Check(!ReadHello(frame,Crossplay::Edition::Gold,decoded),"zero Gold source connection excluded");
        for(size_t end=0;end<frame.size();++end) {
            const std::vector<unsigned char> truncated(frame.begin(),frame.begin()+end);
            Check(!ReadHello(truncated,Crossplay::Edition::UbisoftHistory,decoded),"truncated hello excluded");
        }
        auto changed=frame;changed.push_back(0);Check(!ReadHello(changed,Crossplay::Edition::UbisoftHistory,decoded),"trailing hello data excluded");
        changed=frame;changed[FrameHeader+12]=65;Check(!ReadHello(changed,Crossplay::Edition::UbisoftHistory,decoded),"unbounded display name excluded");
        changed=frame;changed[7]=1;Message old{};
        Check(!Header(changed,!host,reverse,old),"old admission version cannot silently lose principal");
        changed=frame;changed[7]=2;
        Check(!Header(changed,!host,reverse,old),"version2 cannot silently drop second native Castle Type");
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
    for(const auto& text:{std::string(129,'x'),std::string("bad\0type",8),std::string("bad\ntype"),std::string(1,static_cast<char>(0x80))}) {
        auto offered=hello;offered.castle_type=text;auto frame=Frame(Message::Hello,true,peer);
        Check(!AppendHello(frame,offered),"invalid Castle Type cannot reach native construction");
    }
    for(size_t length:{0u,1u,128u}) {
        CrossplayCastleSetup::Value value{hello.castle,std::string(length,'x')},decoded;
        std::vector<unsigned char> bytes;
        Check(CrossplayCastleSetup::Encode(value,bytes) && CrossplayCastleSetup::Decode(bytes,decoded)
            && decoded.serialized_type==value.serialized_type,"bounded full-castle Type roundtrip");
        for(size_t end=0;end<bytes.size();++end) {
            auto bad=std::vector<unsigned char>(bytes.begin(),bytes.begin()+end);decoded=value;
            Check(!CrossplayCastleSetup::Decode(bad,decoded) && decoded.design.parts.empty()
                && decoded.serialized_type.empty(),"truncated full castle clears result");
        }
        auto bad=bytes;bad.push_back(0);
        Check(!CrossplayCastleSetup::Decode(bad,decoded),"trailing full castle rejected");
        bad=bytes;for(size_t i=8;i<12;++i)bad[i]=0xff;
        Check(!CrossplayCastleSetup::Decode(bad,decoded),"unbounded nested cosmetic data rejected");
        std::vector<unsigned char> legacy;
        Check(castle_wire::Encode(value.design,legacy) && !CrossplayCastleSetup::Decode(legacy,decoded),"legacy cosmetic wire is not a complete castle setup");
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
    {
        const auto editions=std::array{Crossplay::Edition::Gold,Crossplay::Edition::SteamHistory,Crossplay::Edition::UbisoftHistory};
        Crossplay::RoomSnapshot room;
        room.member_count=3;
        room.generation=19;
        room.session.fill(0x75);
        room.content.fill(0x52);
        for(unsigned i=0;i<room.member_count;++i) {
            room.members[i].edition=editions[i];
            room.members[i].id.fill(static_cast<unsigned char>(i+1));
            room.members[i].in_local_namespace[0]=static_cast<unsigned char>(42+i);
        }
        for(unsigned char host=1;host<=room.member_count;++host) {
            room.host_slot=host;
            RoomSlots slots{3,5,7};
            slots[host-1]=1;
            Check(ValidRoomSlots(room,slots),"any transport member can host with reserved AI slots");
            auto bad=slots;bad[1]=bad[0];
            Check(!ValidRoomSlots(room,bad),"duplicate map slot rejected");
            bad=slots;bad[3]=2;
            Check(!ValidRoomSlots(room,bad),"unused slot entry rejected");
            for(unsigned char self=1;self<=room.member_count;++self) {
                std::array<uint32_t,CrossplayGroupRoster::Members> routes;
                for(uint32_t local_cid:{1u,2u,9u,101u}) {
                    Check(PlanRoomConnections(room,self,local_cid,routes) && routes[self-1]==local_cid,"original local CID preserved");
                    Check(routes[0]!=routes[1] && routes[0]!=routes[2] && routes[1]!=routes[2],"one distinct receiver route per member");
                    if(self==1)Check(routes[1]==43 && routes[2]==44,"authenticated Gold alias respected");
                }
                Check(PlanRoomConnections(room,self,0,routes)==(self!=1),"History zero CID, Gold nonzero CID");
                Crossplay::RoomEvent event{self,RoomHelloHeader(room,slots,self)};
                Hello offered=hello;offered.connection=7;
                Check(AppendHello(event.bytes,offered),"reuse both native castle strings");
                Check(ReadRoomHello(room,slots,event,offered),"read authenticated group member");
                auto changed=event;changed.source_slot=self%room.member_count+1;
                Check(!ReadRoomHello(room,slots,changed,offered),"cannot attribute data to another transport slot");
                auto displaced=slots;std::swap(displaced[0],displaced[1]);
                Check(!ReadRoomHello(room,displaced,event,offered),"all peers must agree to the same map slots");
                ++room.generation;
                Check(!ReadRoomHello(room,slots,event,offered),"old room generation rejected");
                --room.generation;
            }
        }
        std::array<uint32_t,CrossplayGroupRoster::Members> routes;
        Check(!PlanRoomConnections(room,1,43,routes),"a remote alias may not reuse the original local CID");
        room.members[2].in_local_namespace=room.members[1].in_local_namespace;
        Check(!PlanRoomConnections(room,1,9,routes),"duplicate remote alias rejected");
        const std::array<Participant,CrossplayGroupRoster::Members> players{{
            {0,1,10,0,"Host",true,false},
            {2,2,20,2,"Self",false,true},
            {1,3,30,1,"Peer",false,false},
        }};
        LobbySnapshot pending{123,2,false,{{players[1],2},{players[0],1}}};
        Check(RoomReadbackPending(pending,players,3),"original added member can still be pending");
        pending.members[1].player.connection=99;
        Check(!RoomReadbackPending(pending,players,3),"foreign original route fails");
        pending.members[1]={players[0],2};
        pending.members[1].player.name="Changed";
        Check(!RoomReadbackPending(pending,players,3),"changed confirmed member fails");
        RoomLobbyAdmission group;
        RoomRoute route;
        Check(!group.Begin(channel,1,{},nullptr,0) && group.GetState()==AdmissionState::Failed,
            "group admission requires authenticated ownership");
        Check(!group.Resolve(1,false,route) && !group.Resolve(1,true,route),"unbound lobby and world routes unavailable");
    }
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
