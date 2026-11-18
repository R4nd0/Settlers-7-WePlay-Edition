#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
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
bool LobbyAdmission::CapturePeerSetup(Participant& peer,CrossplayCastleSetup::Value& castle,uint32_t& extra) {
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    peer={};castle={};extra=0;
    if(!impl->Owned() || impl->state!=AdmissionState::RosterBound || !impl->received_hello || !Poll())return false;
    CrossplayCastleSetup::Value value{impl->remote_hello.castle,impl->remote_hello.castle_type};
    std::vector<unsigned char> checked;
    if(!CrossplayCastleSetup::Encode(value,checked))return false;
    peer=impl->remote;castle=std::move(value);extra=impl->remote_hello.extra;return true;
}

namespace {
using RoomSlots=std::array<unsigned char,CrossplayGroupRoster::Members>;
constexpr unsigned char RoomHelloMagic[8]={'S','7','G','J','O','I','N',1};
constexpr unsigned char RoomAppliedMagic[8]={'S','7','G','B','O','U','N',1};

bool ValidRoomSlots(const Crossplay::RoomSnapshot& room, const RoomSlots& slots) {
    if(room.member_count<2 || room.member_count>slots.size() || !room.host_slot
        || room.host_slot>room.member_count)return false;
    unsigned occupied=0;
    for(size_t i=0;i<slots.size();++i) {
        if(i>=room.member_count) {
            if(slots[i])return false;
            continue;
        }
        if(!slots[i] || slots[i]>slots.size() || (occupied&(1u<<slots[i])))return false;
        occupied|=1u<<slots[i];
    }
    return true;
}

std::vector<unsigned char> RoomHelloHeader(const Crossplay::RoomSnapshot& room,
    const RoomSlots& slots,unsigned char source_slot) {
    std::vector<unsigned char> frame(RoomHelloMagic,RoomHelloMagic+8);
    Word(frame,room.generation);
    frame.insert(frame.end(),room.session.begin(),room.session.end());
    const auto& sender=room.members[source_slot-1].id;
    frame.insert(frame.end(),sender.begin(),sender.end());
    frame.insert(frame.end(),slots.begin(),slots.end());
    frame.resize(FrameHeader);
    return frame;
}

bool ReadRoomHello(const Crossplay::RoomSnapshot& room,const RoomSlots& slots,
    const Crossplay::RoomEvent& event,Hello& hello) {
    hello={};
    if(!event.source_slot || event.source_slot>room.member_count
        || event.bytes.size()<FrameHeader || event.bytes.size()>MaximumFrame)return false;
    const auto header=RoomHelloHeader(room,slots,event.source_slot);
    return std::equal(header.begin(),header.end(),event.bytes.begin())
        && ReadHello(event.bytes,room.members[event.source_slot-1].edition,hello);
}

bool Digest(const std::vector<unsigned char>& bytes,Crossplay::Digest& output) {
    output={};
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(bytes.empty() || BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    const bool okay=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()),output.data(),static_cast<ULONG>(output.size()))>=0;
    BCryptCloseAlgorithmProvider(algorithm,0);
    return okay;
}

bool RoomReadbackPending(const LobbySnapshot& snapshot,
    const std::array<Participant,CrossplayGroupRoster::Members>& players,size_t count) {
    if(snapshot.members.size()>count)return false;
    for(const auto& observed:snapshot.members) {
        const auto end=players.begin()+count;
        const auto assigned=std::find_if(players.begin(),end,[&](const auto& player) {
            return player.connection==observed.player.connection;
        });
        if(assigned==end || (observed.status!=1 && observed.status!=2))return false;
        if(observed.status==2 && !observed.player.local && !SameMember(observed.player,*assigned))return false;
    }
    return true;
}
}

struct RoomLobbyAdmission::Impl {
    DWORD owner=GetCurrentThreadId();
    Crossplay::Channel* channel=nullptr;
    Crossplay::RoomSnapshot room{};
    ParticipantEvent native;
    AdmissionState state=AdmissionState::New;
    ULONGLONG started=0;
    uintptr_t instance=0;
    unsigned char local=0;
    RoomSlots slots{};
    Participant own;
    std::array<Hello,CrossplayGroupRoster::Members> offers{};
    std::array<Participant,CrossplayGroupRoster::Members> players{};
    std::array<std::vector<unsigned char>,CrossplayGroupRoster::Members> receipts{};
    std::array<CrossplayGroupRoster::Binding,CrossplayGroupRoster::Members> bindings{};
    CrossplayGroupRoster::Value roster;
    std::vector<unsigned char> receipt;
    unsigned offered=0,applied=0;

    bool Owned() const {return owner==GetCurrentThreadId();}
    unsigned AllMembers() const {return (1u<<room.member_count)-1;}
    bool Connected() const {
        if(!channel || channel->GetState()!=Crossplay::State::Connected)return false;
        const auto* current=channel->GetRoom();
        return current && current->session==room.session && current->generation==room.generation
            && current->host_slot==room.host_slot && current->member_count==room.member_count;
    }
    bool Fail() {
        state=AdmissionState::Failed;
        if(channel)channel->Close();
        return false;
    }
    bool Matches(const LobbySnapshot& snapshot) const {
        return snapshot.instance==instance
            && CrossplayGroupRoster::MatchesNative(roster,room.members[local-1].id,bindings,snapshot);
    }
    bool Prepare();
    bool Apply();
    bool Record(const Crossplay::RoomEvent& event);
};

bool RoomLobbyAdmission::Impl::Prepare() {
    std::array<uint32_t,CrossplayGroupRoster::Members> cids;
    if(!PlanRoomConnections(room,local,own.connection,cids))return false;
    roster={room.session,room.generation,room.content,{}};
    std::vector<unsigned char> transcript;
    for(size_t i=0;i<room.member_count;++i) {
        const auto& source=offers[i];
        const bool is_local=i+1==local;
        const bool host=i+1==room.host_slot;
        auto& player=players[i];
        player={cids[i],slots[i],source.principal,is_local?own.station:cids[i],source.name,host,is_local};
        for(size_t j=0;j<i;++j)if(!DistinctPlayers(player,players[j]))return false;
        Crossplay::Digest castle_hash{};
        std::vector<unsigned char> castle;
        if(!CrossplayCastleSetup::Encode({source.castle,source.castle_type},castle) || !Digest(castle,castle_hash))return false;
        roster.members.push_back({room.members[i].id,room.members[i].edition,player.slot,source.name,castle_hash});
        auto hello=RoomHelloHeader(room,slots,static_cast<unsigned char>(i+1));
        if(!AppendHello(hello,source))return false;
        Word(transcript,static_cast<uint32_t>(hello.size()));
        transcript.insert(transcript.end(),hello.begin(),hello.end());
    }
    std::swap(roster.members[0],roster.members[room.host_slot-1]);
    std::sort(roster.members.begin()+1,roster.members.end(),[](const auto& a,const auto& b){return a.slot<b.slot;});
    if(!CrossplayGroupRoster::MatchesRoom(roster,room))return false;
    for(size_t i=0;i<roster.members.size();++i) {
        const auto slot=roster.members[i].slot;
        const auto player=std::find_if(players.begin(),players.begin()+room.member_count,
            [slot](const auto& candidate){return candidate.slot==slot;});
        if(player==players.begin()+room.member_count)return false;
        bindings[i]={roster.members[i].peer,player->connection,player->principal,player->station};
    }
    std::vector<unsigned char> encoded;
    Crossplay::Digest digest{};
    if(!CrossplayGroupRoster::Encode(roster,encoded) || !Digest(transcript,digest))return false;
    receipt.assign(RoomAppliedMagic,RoomAppliedMagic+8);
    receipt.insert(receipt.end(),digest.begin(),digest.end());
    receipt.insert(receipt.end(),encoded.begin(),encoded.end());
    for(size_t i=0;i<room.member_count;++i)
        if(!receipts[i].empty() && receipts[i]!=receipt)return false;
    return true;
}

bool RoomLobbyAdmission::Impl::Apply() {
    // Verify the original owner and sole local participant again before
    // queuing the externally observed members into this lobby.
    LobbySnapshot current;
    if(!native.CaptureLobby(current) || current.instance!=instance || !current.host
        || current.members.size()!=1 || current.members[0].status!=2
        || !SameMember(current.members[0].player,own))return false;
    const auto confirm=[this](size_t i) {
        const auto& source=offers[i];
        return native.ConstructConfirmedSetup(players[i],{source.castle,source.castle_type},source.extra)
            && native.EnqueueLocal() && native.Reset();
    };
    if(!SameMember(players[local-1],own) && !confirm(local-1))return false;
    for(size_t i=0;i<room.member_count;++i) {
        const auto& player=players[i];
        if(player.local)continue;
        if(!native.Construct(Change::Added,player) || !native.EnqueueLocal() || !native.Reset() || !confirm(i))return false;
    }
    state=AdmissionState::Applying;
    return true;
}

bool RoomLobbyAdmission::Impl::Record(const Crossplay::RoomEvent& event) {
    if(!event.source_slot || event.source_slot>room.member_count || event.source_slot==local
        || event.bytes.size()<8)return false;
    const size_t i=event.source_slot-1;
    const auto bit=1u<<i;
    if(!memcmp(event.bytes.data(),RoomAppliedMagic,8)) {
        if(!(offered&bit) || (applied&bit) || event.bytes.size()>40+CrossplayGroupRoster::Maximum
            || (!receipt.empty() && receipt!=event.bytes))return false;
        receipts[i]=event.bytes;
        applied|=bit;
        return true;
    }
    if((offered&bit) || !ReadRoomHello(room,slots,event,offers[i]))return false;
    offered|=bit;
    return true;
}

RoomLobbyAdmission::RoomLobbyAdmission():impl(new Impl) {}
RoomLobbyAdmission::~RoomLobbyAdmission()=default;

bool RoomLobbyAdmission::Begin(Crossplay::Channel& channel,unsigned char local,
    const RoomSlots& slots,const void* castle,uint32_t extra) {
    auto& s=*impl;
    if(!s.Owned() || s.state!=AdmissionState::New)return false;
    const auto* room=channel.GetRoom();
    if(!room || channel.GetState()!=Crossplay::State::Connected
        || !ValidRoomSlots(*room,slots) || !local || local!=room->local_slot)return s.Fail();
    s.channel=&channel;
    s.room=*room;
    s.slots=slots;
    s.local=local;
    LobbySnapshot lobby;
    CrossplayCastleSetup::Value setup;
    if(!s.native.Bind(room->members[local-1].edition) || !s.native.CaptureLobby(lobby)
        || !lobby.host || lobby.members.size()!=1 || lobby.members[0].status!=2
        || !lobby.members[0].player.local || !lobby.members[0].player.host
        || !s.native.CaptureCastleSetup(castle,setup))return s.Fail();
    s.own=lobby.members[0].player;
    s.instance=lobby.instance;
    auto& hello=s.offers[local-1];
    hello={s.own.connection,extra,s.own.principal,s.own.name,std::move(setup.design),std::move(setup.serialized_type)};
    auto frame=RoomHelloHeader(*room,slots,local);
    if(!AppendHello(frame,hello) || !channel.BroadcastRoom(frame))return s.Fail();
    s.offered=1u<<(local-1);
    s.started=GetTickCount64();
    s.state=AdmissionState::AwaitingPeer;
    return true;
}

bool RoomLobbyAdmission::Poll() {
    auto& s=*impl;
    if(!s.Owned() || s.state==AdmissionState::New || s.state==AdmissionState::Failed)return false;
    if(!s.channel->Poll() || !s.Connected())return s.Fail();
    LobbySnapshot snapshot;
    if(!s.native.CaptureLobby(snapshot) || snapshot.instance!=s.instance)return s.Fail();
    if(s.state==AdmissionState::RosterBound)return s.Matches(snapshot) || s.Fail();
    if(GetTickCount64()-s.started>120000)return s.Fail();
    if(s.state==AdmissionState::Applying) {
        if(!RoomReadbackPending(snapshot,s.players,s.room.member_count))return s.Fail();
        if(s.Matches(snapshot)) {
            if(!s.channel->BroadcastRoom(s.receipt))return s.Fail();
            s.applied|=1u<<(s.local-1);
            s.state=AdmissionState::AwaitingCommit;
        }
    } else if(s.state==AdmissionState::AwaitingCommit && !s.Matches(snapshot))return s.Fail();
    Crossplay::RoomEvent event;
    for(unsigned i=0;i<16 && s.state!=AdmissionState::RosterBound;++i) {
        if(s.state==AdmissionState::AwaitingCommit && s.applied==s.AllMembers()) {
            s.state=AdmissionState::RosterBound;
            break;
        }
        if(!s.channel->ReceiveRoom(event))break;
        if(!s.Record(event))return s.Fail();
    }
    if(s.state==AdmissionState::AwaitingPeer && s.offered==s.AllMembers()
        && (!s.Prepare() || !s.Apply()))return s.Fail();
    return true;
}

AdmissionState RoomLobbyAdmission::GetState() const {return impl->Owned()?impl->state:AdmissionState::Failed;}

bool RoomLobbyAdmission::Resolve(unsigned char source_slot,bool started,RoomRoute& route) const {
    route={};
    const auto& s=*impl;
    if(!s.Owned() || s.state!=AdmissionState::RosterBound || !s.Connected()
        || !source_slot || source_slot>s.room.member_count)return false;
    LobbySnapshot snapshot;
    if(!(started?s.native.CaptureStartedSession(snapshot):s.native.CaptureLobby(snapshot))
        || !s.Matches(snapshot))return false;
    const size_t i=source_slot-1;
    const auto& sender=s.room.members[i];
    route={s.offers[i].connection,s.players[i].connection,s.players[i].slot,s.players[i].host,
        source_slot==s.local,s.room.members[s.local-1].edition,
        {sender.edition,s.room.members[s.local-1].id,sender.id,sender.native_identity,
         sender.in_local_namespace,{}}};
    return true;
}

bool RoomLobbyAdmission::CaptureConfigurationRoutes(unsigned char capacity,CrossplaySessionConfig::RoomConnections& routes) const {
    routes={};
    const auto& s=*impl;
    if(!s.Owned() || s.state!=AdmissionState::RosterBound || !s.Connected())return false;
    LobbySnapshot snapshot;
    if(!s.native.CaptureLobby(snapshot) || !s.Matches(snapshot))return false;
    CrossplaySessionConfig::RoomConnections candidate;
    candidate.capacity=capacity;
    candidate.host=s.players[s.room.host_slot-1].connection;
    for(size_t i=0;i<s.room.member_count;++i)
        candidate.stations[s.players[i].slot-1]=s.players[i].connection;
    if(!CrossplaySessionConfig::ValidRoomConnections(candidate))return false;
    routes=candidate;
    return true;
}
}
