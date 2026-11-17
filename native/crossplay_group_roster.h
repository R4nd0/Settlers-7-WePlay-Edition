#pragma once
#include "crossplay_participant.h"
#include "crossplay_chat.h"

// One three-edition room descriptor, independent of the pair protocol. This
// model does not authenticate a transport, add native players or grant Ready,
// NEO, start, clock or save authority. Native callers must capture their own
// live original roster and retain the existing owner/image/ABI checks.
namespace CrossplayGroupRoster {
constexpr size_t Members=3,Header=61,Maximum=Header+3*(51+64);
inline constexpr unsigned char Magic[8]={'S','7','G','R','O','S','T',1};
struct Member {Crossplay::Id peer{};Crossplay::Edition edition{};uint32_t slot=0;std::string name;Crossplay::Digest castle{};};
struct Value {Crossplay::Id room{};uint32_t generation=0;Crossplay::Digest content{};std::array<Member,Members> members{};};
template<size_t N> inline bool Nonzero(const std::array<unsigned char,N>& value){for(auto byte:value)if(byte)return true;return false;}
inline bool Text(const std::string& name){
    if(name.empty() || name.size()>64)return false;for(unsigned char c:name)if(c<32 || c>126)return false;return true;
}
inline bool Valid(const Value& value){
    if(!Nonzero(value.room) || !value.generation || !Nonzero(value.content))return false;
    unsigned editions=0;
    for(size_t i=0;i<Members;++i){
        const auto& member=value.members[i];const unsigned edition=static_cast<unsigned>(member.edition);
        if(edition<1 || edition>3 || (editions&(1u<<edition)) || member.slot!=i+1 || !Nonzero(member.peer)
            || member.peer==value.room || !Text(member.name) || !Nonzero(member.castle))return false;
        for(size_t j=0;j<i;++j)if(member.peer==value.members[j].peer)return false;
        editions|=1u<<edition;
    }
    return editions==14 && (value.members[0].edition==Crossplay::Edition::Gold || value.members[0].edition==Crossplay::Edition::SteamHistory);
}
inline bool Encode(const Value& value,std::vector<unsigned char>& bytes){
    bytes.clear();if(!Valid(value))return false;bytes.assign(Magic,Magic+8);bytes.insert(bytes.end(),value.room.begin(),value.room.end());
    bytes.resize(28);CrossplayChat::Put(bytes.data()+24,value.generation);bytes.insert(bytes.end(),value.content.begin(),value.content.end());bytes.push_back(3);
    for(const auto& member:value.members){
        bytes.insert(bytes.end(),member.peer.begin(),member.peer.end());bytes.push_back(static_cast<unsigned char>(member.edition));
        bytes.push_back(static_cast<unsigned char>(member.slot));bytes.push_back(static_cast<unsigned char>(member.name.size()));
        bytes.insert(bytes.end(),member.name.begin(),member.name.end());bytes.insert(bytes.end(),member.castle.begin(),member.castle.end());
    }
    return bytes.size()<=Maximum;
}
inline bool Decode(const std::vector<unsigned char>& bytes,Value& output){
    output={};if(bytes.size()<Header+3*52 || bytes.size()>Maximum || memcmp(bytes.data(),Magic,8) || bytes[60]!=3)return false;
    Value value;memcpy(value.room.data(),bytes.data()+8,16);value.generation=CrossplayChat::Word(bytes.data()+24);memcpy(value.content.data(),bytes.data()+28,32);
    size_t at=Header;
    for(auto& member:value.members){
        if(bytes.size()-at<51)return false;memcpy(member.peer.data(),bytes.data()+at,16);member.edition=static_cast<Crossplay::Edition>(bytes[at+16]);
        member.slot=bytes[at+17];const size_t length=bytes[at+18];at+=19;
        if(!length || length>64 || bytes.size()-at<length+32)return false;
        member.name.assign(reinterpret_cast<const char*>(bytes.data()+at),length);at+=length;memcpy(member.castle.data(),bytes.data()+at,32);at+=32;
    }
    if(at!=bytes.size() || !Valid(value))return false;output=std::move(value);return true;
}
// Per-engine values are supplied by the native admission owner; never infer
// a native connection/principal from a catalogue reference or logical slot.
struct Binding {Crossplay::Id peer{};uint32_t connection=UINT32_MAX,principal=UINT32_MAX,station=UINT32_MAX;};
inline bool MatchesNative(const Value& expected,const Crossplay::Id& self,const std::array<Binding,Members>& bindings,
                          const CrossplayNative::LobbySnapshot& actual){
    if(!Valid(expected) || !Nonzero(self) || !actual.instance || actual.members.size()!=Members)return false;
    size_t local=Members;
    for(size_t i=0;i<Members;++i){
        const auto& binding=bindings[i];const auto& member=expected.members[i];
        if(binding.peer!=member.peer || binding.connection==UINT32_MAX || binding.principal==UINT32_MAX || binding.station==UINT32_MAX)return false;
        for(size_t j=0;j<i;++j)if(binding.connection==bindings[j].connection)return false;
        if(member.peer==self)local=i;
        unsigned matches=0;
        for(const auto& observed:actual.members){const auto& p=observed.player;
            if(observed.status==2 && p.slot==member.slot && p.connection==binding.connection && p.principal==binding.principal
                && p.station==binding.station && p.name==member.name && p.host==(i==0) && p.local==(member.peer==self))++matches;}
        if(matches!=1)return false;
    }
    return local<Members && actual.local_slot==expected.members[local].slot && actual.host==(local==0);
}
// An acknowledgement barrier only: consuming three protocol acknowledgements
// does not establish that an original engine performed their claimed actions.
enum class Purpose:uint32_t {Admission=1,Setup=2,Neo=3,World=4,Save=5,Reload=6};
class Barrier {
    bool bound=false,invalid=false;Value roster;std::vector<unsigned char> context;unsigned mask=0;uint64_t started=0;
    bool Current(uint64_t now)const{return bound && !invalid && now>=started && now-started<=10000;}
public:
    bool Bind(const Value& value,Purpose purpose,const Crossplay::Id& challenge,uint64_t now){
        std::vector<unsigned char> encoded;
        const auto kind=static_cast<uint32_t>(purpose);
        if(bound || invalid || kind<1 || kind>6 || !Nonzero(challenge) || challenge==value.room || !Encode(value,encoded)){invalid=true;return false;}
        const unsigned char magic[8]={'S','7','G','A','C','K',0,1};context.assign(magic,magic+8);context.resize(12);
        CrossplayChat::Put(context.data()+8,kind);context.insert(context.end(),challenge.begin(),challenge.end());context.insert(context.end(),encoded.begin(),encoded.end());
        roster=value;started=now;bound=true;return true;
    }
    const std::vector<unsigned char>& Context()const{return context;}
    bool Observe(const std::vector<unsigned char>& exact_context,const Crossplay::Id& peer,uint64_t now){
        if(!Current(now) || exact_context!=context){invalid=true;return false;}
        for(size_t i=0;i<Members;++i)if(roster.members[i].peer==peer){
            if(mask&(1u<<i)){invalid=true;return false;}mask|=1u<<i;return true;}
        invalid=true;return false;
    }
    bool Complete(uint64_t now)const{return Current(now) && mask==7;}
};
}
