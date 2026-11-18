#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_group_roster.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;void Check(bool okay){++checks;if(!okay){printf("FAIL group roster %u\n",checks);std::exit(1);}}
CrossplayGroupRoster::Value Roster(bool steam){
    using namespace CrossplayGroupRoster;Value value;value.room.fill(0xf0);value.content.fill(0x31);value.generation=7;
    const Crossplay::Edition gold_order[]={Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory};
    const Crossplay::Edition steam_order[]={Crossplay::Edition::SteamHistory,Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory};
    value.members.resize(3);
    for(unsigned i=0;i<3;++i){auto& member=value.members[i];member.peer.fill(static_cast<unsigned char>(0x11+i));member.edition=steam?steam_order[i]:gold_order[i];
        member.slot=i+1;member.name="Member"+std::to_string(i+1);member.castle.fill(static_cast<unsigned char>(0x41+i));}
    return value;
}
Crossplay::RoomSnapshot Transport(const CrossplayGroupRoster::Value& roster){
    Crossplay::RoomSnapshot snapshot;
    snapshot.session=roster.room;
    snapshot.content=roster.content;
    snapshot.generation=roster.generation;
    snapshot.host_slot=static_cast<unsigned char>(roster.members.size());
    snapshot.member_count=snapshot.host_slot;
    for(size_t i=0;i<roster.members.size();++i){
        const auto& player=roster.members[roster.members.size()-1-i];
        snapshot.members[i].id=player.peer;
        snapshot.members[i].edition=player.edition;
    }
    return snapshot;
}
}
int main(){
    using namespace CrossplayGroupRoster;
    for(bool steam:{false,true}){
        const auto roster=Roster(steam);const auto transport=Transport(roster);
        Check(Valid(roster));std::vector<unsigned char> context,repeated;Value decoded;
        Check(MatchesRoom(roster,transport));
        for(unsigned field=0;field<6;++field){
            auto altered=transport;
            if(field==0)altered.session[0]^=1;if(field==1)++altered.generation;
            if(field==2)altered.content[0]^=1;if(field==3)--altered.member_count;
            if(field==4)altered.host_slot=1;if(field==5)altered.members[0].id[0]^=1;
            Check(!MatchesRoom(roster,altered));
        }
        Check(Encode(roster,context)&&Decode(context,decoded)&&Encode(decoded,repeated)&&repeated==context);
        for(unsigned local=0;local<3;++local){
            std::array<Binding,Members> bindings;CrossplayNative::LobbySnapshot snapshot;snapshot.instance=0x1234;snapshot.host=local==0;snapshot.local_slot=local+1;
            for(unsigned i=0;i<3;++i){const auto& member=roster.members[i];const uint32_t connection=roster.members[local].edition==Crossplay::Edition::Gold?i+1:i;
                bindings[i]={member.peer,connection,101+i,201+i};CrossplayNative::Participant p;
                p.connection=connection;p.principal=101+i;p.station=201+i;p.slot=i+1;p.name=member.name;p.host=i==0;p.local=i==local;snapshot.members.push_back({p,2});}
            Check(MatchesNative(roster,roster.members[local].peer,bindings,snapshot));
            std::reverse(snapshot.members.begin(),snapshot.members.end());Check(MatchesNative(roster,roster.members[local].peer,bindings,snapshot));
            auto other=snapshot;other.members.pop_back();Check(!MatchesNative(roster,roster.members[local].peer,bindings,other));
            other=snapshot;other.members.push_back(other.members.back());Check(!MatchesNative(roster,roster.members[local].peer,bindings,other));
            for(unsigned field=0;field<8;++field){other=snapshot;auto& p=other.members[0].player;
                if(field==0)other.members[0].status=1;if(field==1)++p.connection;if(field==2)++p.principal;if(field==3)++p.station;
                if(field==4)++p.slot;if(field==5)p.name+="x";if(field==6)p.host=!p.host;if(field==7)p.local=!p.local;
                Check(!MatchesNative(roster,roster.members[local].peer,bindings,other));}
            auto wrong=bindings;wrong[1].connection=wrong[0].connection;Check(!MatchesNative(roster,roster.members[local].peer,wrong,snapshot));
            other=snapshot;other.host=!other.host;Check(!MatchesNative(roster,roster.members[local].peer,bindings,other));
            other=snapshot;other.local_slot=4;Check(!MatchesNative(roster,roster.members[local].peer,bindings,other));
        }
        for(unsigned field=0;field<11;++field){auto bad=roster;
            if(field==0)bad.room={};if(field==1)bad.generation=0;if(field==2)bad.content={};if(field==3)bad.members[1].peer=bad.members[0].peer;
            if(field==4)bad.members[1].edition=static_cast<Crossplay::Edition>(4);if(field==5)bad.members[2].slot=2;
            if(field==6)bad.members[1].name="";if(field==7)bad.members[1].name="Bad\nName";
            if(field==8)bad.members[0].castle={};if(field==9)bad.members[1].peer=bad.room;
            if(field==10)std::swap(bad.members[1],bad.members[2]);
            Check(!Valid(bad));Check(!Encode(bad,repeated)&&repeated.empty());}
        for(size_t size=0;size<context.size();++size){auto truncated=context;truncated.resize(size);Check(!Decode(truncated,decoded));}
        auto appended=context;appended.push_back(0);Check(!Decode(appended,decoded));
        for(unsigned purpose=1;purpose<=6;++purpose){
            Crossplay::Id nonce{};nonce.fill(0x71);Barrier barrier;Check(barrier.Bind(roster,transport,static_cast<Purpose>(purpose),nonce,100));
            const auto exact=barrier.Context();Check(!barrier.Complete(101));
            Check(barrier.Observe(exact,3,102));Check(barrier.Observe(exact,1,103));Check(!barrier.Complete(104));
            Check(barrier.Observe(exact,2,10100));Check(barrier.Complete(10100));Check(!barrier.Complete(10101));
            Check(!barrier.Observe(exact,2,10100));Check(!barrier.Complete(10100));
            for(unsigned change=0;change<5;++change){Barrier b;Check(b.Bind(roster,transport,static_cast<Purpose>(purpose),nonce,100));auto modified=b.Context();
                uint64_t now=101;unsigned char slot=3;
                if(change==0)modified[8]^=1;if(change==1)modified[12]^=1;if(change==2)slot=4;if(change==3)now=99;if(change==4)now=10101;
                Check(!b.Observe(modified,slot,now));Check(!b.Complete(101));}
        }
    }
    // Exhaust the ordered edition combinations for every supported human
    // count, with an acknowledgement from each distinct peer.
    unsigned combinations=1;
    for(unsigned count=1;count<=Members;++count){
        combinations*=3;
        if(count<2)continue;
        for(unsigned code=0;code<combinations;++code){
            auto roster=Roster(false);
            roster.members.resize(count,roster.members[0]);
            unsigned digits=code;
            for(unsigned i=0;i<count;++i){
                auto& m=roster.members[i];
                m.peer.fill(static_cast<unsigned char>(i+1));
                m.slot=i+1;
                m.edition=static_cast<Crossplay::Edition>(digits%3+1);
                digits/=3;
            }
            std::vector<unsigned char> bytes;Value decoded;
            Check(Encode(roster,bytes) && Decode(bytes,decoded) && decoded.members.size()==count);
            for(unsigned i=0;i<count;++i)Check(decoded.members[i].edition==roster.members[i].edition);
            Crossplay::Id nonce{};nonce.fill(0x71);
            Barrier barrier;
            Check(barrier.Bind(roster,Transport(roster),Purpose::Admission,nonce,100));
            for(unsigned char slot=1;slot<=count;++slot){
                Check(!barrier.Complete(101));
                Check(barrier.Observe(barrier.Context(),slot,101));
            }
            Check(barrier.Complete(101));
        }
    }
    auto spaced=Roster(false);
    spaced.members[0].slot=4;spaced.members[1].slot=1;spaced.members[2].slot=3;
    Check(Valid(spaced));
    printf("PASS %u room roster/native-view/all-member ACK/phase/challenge/deadline checks; authored model only.\n",checks);
}
