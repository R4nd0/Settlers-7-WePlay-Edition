#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "crossplay_admission.h"
#include "crossplay_connection.h"
#include <algorithm>
#include <cstring>

namespace CrossplayNative {
namespace {
constexpr unsigned char AdmissionMagic[8]={'S','7','L','O','B','B','Y',3};
constexpr size_t FrameHeader=60,MaximumFrame=FrameHeader+4+4+4+1+64+4+CrossplayCastleSetup::Limit;
enum class Message : unsigned char { Hello=1, Assign=2, Applied=3, Commit=4, Committed=5 };
struct Hello { uint32_t connection=0,extra=0,principal=0;std::string name;castle_wire::Design castle;std::string castle_type; };
uint32_t PreparationPlayer(const Participant& player) {return player.principal?player.principal:player.slot;}
bool DistinctPlayers(const Participant& own,const Participant& remote) {
    const uint32_t local=PreparationPlayer(own),foreign=PreparationPlayer(remote);
    return local && foreign && local!=UINT32_MAX && foreign!=UINT32_MAX && local!=foreign;
}
void Word(std::vector<unsigned char>& bytes,uint32_t value) { castle_wire::Put(bytes,value); }
bool TakeWord(const std::vector<unsigned char>& bytes,size_t& at,uint32_t& value) {
    if(at>bytes.size() || bytes.size()-at<4) return false;
    value=0;for(size_t i=0;i<4;++i)value|=static_cast<uint32_t>(bytes[at++])<<(i*8);return true;
}
bool Name(const std::string& name) {
    return !name.empty() && name.size()<=64 && std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32 && c<=126;});
}
bool Mixed(Crossplay::Edition local,Crossplay::Edition peer) {
    const auto history=[](Crossplay::Edition edition){return edition==Crossplay::Edition::UbisoftHistory || edition==Crossplay::Edition::SteamHistory;};
    return (local==Crossplay::Edition::Gold && history(peer)) || (peer==Crossplay::Edition::Gold && history(local));
}
std::vector<unsigned char> Frame(Message type,bool host,const Crossplay::Peer& peer) {
    std::vector<unsigned char> bytes(AdmissionMagic,AdmissionMagic+8);
    bytes.push_back(static_cast<unsigned char>(type));bytes.push_back(host?1u:0u);bytes.push_back(0);bytes.push_back(0);
    const auto& leader=host?peer.local_id:peer.remote_id;
    bytes.insert(bytes.end(),leader.begin(),leader.end());
    bytes.insert(bytes.end(),peer.local_id.begin(),peer.local_id.end());
    bytes.insert(bytes.end(),peer.remote_id.begin(),peer.remote_id.end());return bytes;
}
bool Header(const std::vector<unsigned char>& bytes,bool host,const Crossplay::Peer& peer,Message& type) {
    if(bytes.size()<FrameHeader || bytes.size()>MaximumFrame || memcmp(bytes.data(),AdmissionMagic,8)
        || bytes[8]<1 || bytes[8]>5 || bytes[9]!=(host?0u:1u) || bytes[10] || bytes[11]) return false;
    const auto& leader=host?peer.local_id:peer.remote_id;
    if(memcmp(bytes.data()+12,leader.data(),16) || memcmp(bytes.data()+28,peer.remote_id.data(),16)
        || memcmp(bytes.data()+44,peer.local_id.data(),16)) return false;
    type=static_cast<Message>(bytes[8]);return true;
}
bool AppendHello(std::vector<unsigned char>& bytes,const Hello& hello) {
    std::vector<unsigned char> castle;
    if(hello.connection==UINT32_MAX || hello.principal==UINT32_MAX || !Name(hello.name)
        || !CrossplayCastleSetup::Encode({hello.castle,hello.castle_type},castle)) return false;
    Word(bytes,hello.connection);Word(bytes,hello.extra);Word(bytes,hello.principal);bytes.push_back(static_cast<unsigned char>(hello.name.size()));
    bytes.insert(bytes.end(),hello.name.begin(),hello.name.end());Word(bytes,static_cast<uint32_t>(castle.size()));
    bytes.insert(bytes.end(),castle.begin(),castle.end());return bytes.size()<=MaximumFrame;
}
bool ReadHello(const std::vector<unsigned char>& bytes,Crossplay::Edition edition,Hello& result) {
    result={};Hello hello;size_t at=FrameHeader;
    if(!TakeWord(bytes,at,hello.connection) || hello.connection==UINT32_MAX
        || (edition==Crossplay::Edition::Gold && !hello.connection) || !TakeWord(bytes,at,hello.extra)
        || !TakeWord(bytes,at,hello.principal) || hello.principal==UINT32_MAX || at==bytes.size()) return false;
    const auto length=bytes[at++];
    if(!length || length>64 || length>bytes.size()-at) return false;
    hello.name.assign(reinterpret_cast<const char*>(bytes.data()+at),length);at+=length;
    uint32_t castle_length=0;
    if(!Name(hello.name) || !TakeWord(bytes,at,castle_length) || castle_length>CrossplayCastleSetup::Limit || castle_length!=bytes.size()-at) return false;
    const std::vector<unsigned char> castle(bytes.begin()+at,bytes.end());
    CrossplayCastleSetup::Value setup;
    if(!CrossplayCastleSetup::Decode(castle,setup)) return false;
    hello.castle=std::move(setup.design);hello.castle_type=std::move(setup.serialized_type);
    result=std::move(hello);return true;
}
bool SameMember(const Participant& left,const Participant& right) {
    return left.connection==right.connection && left.slot==right.slot && left.principal==right.principal
        && left.station==right.station && left.name==right.name && left.host==right.host && left.local==right.local;
}
bool BoundRoster(const LobbySnapshot& snapshot,const Participant& own,const Participant& remote,bool host) {
    if(snapshot.members.size()!=2 || snapshot.local_slot!=own.slot || snapshot.host!=host) return false;
    unsigned own_count=0,remote_count=0;
    for(const auto& member:snapshot.members) {
        if(member.status!=2) return false;
        own_count+=SameMember(member.player,own)?1u:0u;
        remote_count+=SameMember(member.player,remote)?1u:0u;
    }
    return own_count==1 && remote_count==1;
}
}
struct LobbyAdmission::Impl {
    DWORD owner=GetCurrentThreadId();
    Crossplay::Channel* channel=nullptr;Crossplay::Peer peer{};Crossplay::Edition edition{};
    AdmissionState state=AdmissionState::New;
    bool host=false,received_hello=false,applied=false,peer_applied=false,commit_sent=false;
    ULONGLONG started=0;uintptr_t instance=0;
    Participant own{},remote{};Hello hello{},remote_hello{};
    ParticipantEvent native;
    bool Owned() const {return owner==GetCurrentThreadId();}
    bool Fail() {state=AdmissionState::Failed;if(channel)channel->Close();return false;}
    bool Send(Message type) {return channel->Send(Frame(type,host,peer));}
    bool Apply() {
        if(!host) {
            if(!native.ConstructConfirmedSetup(own,{hello.castle,hello.castle_type},hello.extra) || !native.EnqueueLocal() || !native.Reset()) return Fail();
        }
        if(!native.Construct(Change::Added,remote) || !native.EnqueueLocal() || !native.Reset()
            || !native.ConstructConfirmedSetup(remote,{remote_hello.castle,remote_hello.castle_type},remote_hello.extra)
            || !native.EnqueueLocal() || !native.Reset()) return Fail();
        state=AdmissionState::Applying;return true;
    }
    bool Record(const std::vector<unsigned char>& bytes) {
        Message type{};
        if(!Header(bytes,host,peer,type)) return Fail();
        if(type==Message::Hello) {
            if(state!=AdmissionState::AwaitingPeer || received_hello || !ReadHello(bytes,peer.edition,remote_hello)) return Fail();
            received_hello=true;
            // Preserve the actual source scalar used by original preparation
            // setters. This is neither a vendor login nor a routing CID. Zero
            // remains zero: original code uses the common assigned slot then.
            remote={0,host?2u:1u,remote_hello.principal,0,remote_hello.name,!host,false};
            if(!DistinctPlayers(own,remote)) return Fail();
            if(!ProjectConnection(edition,own.connection,peer.peer_in_local_namespace,remote.connection))return Fail();
            remote.station=remote.connection;
            if(host) {
                if(!Send(Message::Assign)) return Fail();
                return Apply();
            }
            return true;
        }
        if(bytes.size()!=FrameHeader || !received_hello) return Fail();
        if(type==Message::Assign) {
            if(host || state!=AdmissionState::AwaitingPeer) return Fail();
            return Apply();
        }
        if(type==Message::Applied) {
            if((state!=AdmissionState::Applying && state!=AdmissionState::AwaitingCommit) || peer_applied) return Fail();
            peer_applied=true;return true;
        }
        if(type==Message::Commit) {
            if(host || state!=AdmissionState::AwaitingCommit || !applied || !peer_applied || !Send(Message::Committed)) return Fail();
            state=AdmissionState::RosterBound;return true;
        }
        if(type==Message::Committed) {
            if(!host || state!=AdmissionState::AwaitingCommit || !commit_sent || !applied || !peer_applied) return Fail();
            state=AdmissionState::RosterBound;return true;
        }
        return Fail();
    }
};
LobbyAdmission::LobbyAdmission():impl(new Impl) {}
LobbyAdmission::~LobbyAdmission()=default;
bool LobbyAdmission::Begin(Crossplay::Channel& channel,Crossplay::Edition edition,bool host,const void* castle,uint32_t extra) {
    if(!impl->Owned() || impl->state!=AdmissionState::New) return false;
    const auto* peer=channel.GetPeer();
    if(channel.GetState()!=Crossplay::State::Connected || !peer || !Mixed(edition,peer->edition)) return impl->Fail();
    impl->channel=&channel;impl->peer=*peer;impl->edition=edition;impl->host=host;
    LobbySnapshot snapshot;CrossplayCastleSetup::Value setup;
    if(!impl->native.Bind(edition) || !impl->native.CaptureLobby(snapshot) || snapshot.members.size()!=1
        || snapshot.local_slot!=1 || !snapshot.host || snapshot.members[0].status!=2
        || !snapshot.members[0].player.local || !snapshot.members[0].player.host
        || !Name(snapshot.members[0].player.name) || !impl->native.CaptureCastleSetup(castle,setup)) return impl->Fail();
    impl->hello.castle=std::move(setup.design);impl->hello.castle_type=std::move(setup.serialized_type);
    impl->own=snapshot.members[0].player;impl->instance=snapshot.instance;
    impl->hello.name=impl->own.name;impl->hello.connection=impl->own.connection;impl->hello.extra=extra;
    impl->hello.principal=impl->own.principal;
    impl->own.host=host;impl->own.slot=host?1u:2u;
    auto bytes=Frame(Message::Hello,host,*peer);
    if(!AppendHello(bytes,impl->hello) || !channel.Send(bytes)) return impl->Fail();
    impl->started=GetTickCount64();impl->state=AdmissionState::AwaitingPeer;return true;
}
bool LobbyAdmission::Poll() {
    if(!impl->Owned()) return false;
    if(impl->state==AdmissionState::New || impl->state==AdmissionState::Failed) return false;
    if(!impl->channel->Poll() || impl->channel->GetState()!=Crossplay::State::Connected) return impl->Fail();
    const auto* peer=impl->channel->GetPeer();
    if(!peer || peer->local_id!=impl->peer.local_id || peer->remote_id!=impl->peer.remote_id || peer->edition!=impl->peer.edition) return impl->Fail();
    if(impl->state!=AdmissionState::RosterBound && GetTickCount64()-impl->started>120000) return impl->Fail();
    LobbySnapshot snapshot;
    if(!impl->native.CaptureLobby(snapshot) || snapshot.instance!=impl->instance) return impl->Fail();
    // Once the roster transaction finishes, the session's event dispatcher
    // owns subsequent channel records. Continue checking native ownership
    // without swallowing a gameplay record as an admission message.
    if(impl->state==AdmissionState::RosterBound) return BoundRoster(snapshot,impl->own,impl->remote,impl->host) || impl->Fail();
    std::vector<unsigned char> bytes;
    for(unsigned i=0;i<8 && impl->channel->Receive(bytes);++i) {
        if(!impl->Record(bytes)) return false;
        if(impl->state==AdmissionState::RosterBound) break;
    }
    if(impl->state==AdmissionState::Applying && snapshot.members.size()==2) {
        unsigned own=0,remote=0;
        for(const auto& member:snapshot.members) {
            if(member.status!=2) return true;
            own+=SameMember(member.player,impl->own)?1u:0u;
            remote+=SameMember(member.player,impl->remote)?1u:0u;
        }
        if(own!=1 || remote!=1 || snapshot.local_slot!=impl->own.slot || snapshot.host!=impl->host) return impl->Fail();
        if(!impl->Send(Message::Applied)) return impl->Fail();
        impl->applied=true;impl->state=AdmissionState::AwaitingCommit;
    }
    if(impl->host && impl->state==AdmissionState::AwaitingCommit && impl->peer_applied && !impl->commit_sent) {
        if(!impl->Send(Message::Commit)) return impl->Fail();
        impl->commit_sent=true;
    }
    return true;
}
AdmissionState LobbyAdmission::GetState() const {return impl->Owned()?impl->state:AdmissionState::Failed;}
uint32_t LobbyAdmission::OwnConnection() const {return impl->Owned()?impl->own.connection:UINT32_MAX;}
uint32_t LobbyAdmission::PeerSourceConnection() const {return impl->Owned()?impl->remote_hello.connection:UINT32_MAX;}
uint32_t LobbyAdmission::PeerLocalConnection() const {return impl->Owned()?impl->remote.connection:UINT32_MAX;}
}
