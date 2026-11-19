#pragma once
#include "crossplay_build_command_codec.h"
#include "crossplay_priority_command.h"

// Original task-priority command only. Keep the full native task handles and
// native serializer/factory/reader guards; transport never edits the FIFO ledger.
namespace CrossplayNativeCodec::PriorityCommandDetail {
inline uintptr_t Table(Crossplay::Edition edition){
if(edition==Crossplay::Edition::Gold)return 0xf395a4;
if(edition==Crossplay::Edition::UbisoftHistory)return 0x1151c50;
if(edition==Crossplay::Edition::SteamHistory)return 0x1150098;
return 0;}
inline bool Guard(Crossplay::Edition edition){
SharedEngineMode::ErrorScope error;
if(!BuildCommandDetail::Methods(edition))return false;
struct Region{uintptr_t at;unsigned size;uint64_t hash;};
if(edition==Crossplay::Edition::Gold){
static constexpr Region rows[]={
{0xf395a4,24,0x8659938fb8e6c892ull},
{0x5f0dc0,39,0x89483cef82370584ull},
{0xa2ad10,6,0xc180face6c87d32dull},
{0x4244d0,8,0xd49a243d1287aeb0ull},
{0xa2c9b0,234,0x12012d6ae1cadd00ull},
{0xa2ad20,6,0x31b5d0af35a4361full},
{0x5f0d70,63,0xe03a3a50d533c110ull},
};
for(const auto& row:rows)if(SharedEngineMode::Hash(row.at,row.size)!=row.hash)return false;return true;}
if(edition==Crossplay::Edition::UbisoftHistory){
static constexpr Region rows[]={
{0x1151c50,24,0xd07891d8904596a6ull},
{0x869270,41,0xb4eb7f4dc6d738c5ull},
{0x998d20,6,0xc180face6c87d32dull},
{0x4eeec0,8,0xd49a243d1287aeb0ull},
{0x999e00,227,0x585c8d7b5eff44aull},
{0x4f17d0,6,0x31b5d0af35a4361full},
{0x869230,59,0x9af2ac605014cc48ull},
};
for(const auto& row:rows)if(SharedEngineMode::Hash(row.at,row.size)!=row.hash)return false;return true;}
if(edition==Crossplay::Edition::SteamHistory){
static constexpr Region rows[]={
{0x1150098,24,0x1be67f555af91b46ull},
{0x869040,41,0x27006df106b826bbull},
{0x998af0,6,0xc180face6c87d32dull},
{0x4eebe0,8,0xd49a243d1287aeb0ull},
{0x999bd0,227,0x3689407b325cc2feull},
{0x4f1450,6,0x31b5d0af35a4361full},
{0x869000,59,0x83e8c0d8a49d927bull},
};
for(const auto& row:rows)if(SharedEngineMode::Hash(row.at,row.size)!=row.hash)return false;return true;}
return false;}
inline Detail::Profile Pin(Crossplay::Edition edition){return BuildCommandDetail::Pin(edition);}
inline bool Methods(Crossplay::Edition edition){const auto p=Pin(edition);return Guard(edition) && Detail::Methods(p) && Detail::Tables(p,edition);}
inline bool Source(Crossplay::Edition edition,const void* event,uint32_t source,uint32_t player){
 const auto p=Pin(edition);const auto* b=static_cast<const unsigned char*>(event);
 if(!p.event_vt || !Table(edition) || !CrossplayPriorityCommand::PlayerSlot(player) || source==UINT32_MAX || !Detail::Readable(b,0x1c)
  || CrossplayChat::Word(b)!=p.event_vt || CrossplayChat::Word(b+4)!=CrossplayPriorityCommand::Type || CrossplayChat::Word(b+8)!=source || b[12]>1 || CrossplayChat::Word(b+20)!=player || b[24]>1)return false;
 const auto* nested=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(b+16));
 if(!Detail::Readable(nested,0x14) || CrossplayChat::Word(nested)!=Table(edition) || CrossplayChat::Word(nested+4)!=CrossplayPriorityCommand::PriorityType || nested[8]!=player)return false;
 const auto id=CrossplayChat::Word(nested+12),relative=CrossplayChat::Word(nested+16);
 return id && id!=UINT32_MAX && relative!=UINT32_MAX && id!=relative;
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                   uintptr_t session,uint32_t own,uint32_t other,uint32_t player,const void* event,std::vector<unsigned char>& wire) {
    SharedEngineMode::ErrorScope error;LoadProgressDetail::FpScope fp;wire.clear();
    if((player!=1 && player!=2) || !LoadProgressDetail::World(binding,owner,session,own,other)
        || !Methods(edition) || !Source(edition,event,own,player))return false;
    if(Detail::Encode(Pin(edition),event,wire) && CrossplayPriorityCommand::Inspect(wire,own,player))return true;
    wire.clear();return false;
}
template<class WorldCheck>
inline bool EnqueueBound(Crossplay::Edition edition,uint32_t other,uint32_t player,
                         const std::vector<unsigned char>& wire,WorldCheck current_world) {
    SharedEngineMode::ErrorScope error;LoadProgressDetail::FpScope fp;
    if(!current_world() || !CrossplayPriorityCommand::Inspect(wire,other,player) || !Methods(edition))return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=p.gold?Detail::GoldRead(p.read,&reader,name):reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Detail::Readable(decoded,0x1c) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(edition,decoded,other,player) && Detail::Encode(p,decoded,repeated)
        && repeated==wire && current_world();
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);return okay;
}
inline bool Enqueue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                    uintptr_t session,uint32_t own,uint32_t other,uint32_t player,const std::vector<unsigned char>& wire) {
    return (player==1 || player==2) && EnqueueBound(edition,other,player,wire,[&]{
        return LoadProgressDetail::World(binding,owner,session,own,other);
    });
}
inline bool EnqueueRoom(Crossplay::Edition edition,const CrossplayNative::RoomLobbyAdmission& admission,
                        unsigned char source_slot,const std::vector<unsigned char>& wire) {
    CrossplayNative::RoomRoute source;
    if(!admission.Resolve(source_slot,true,source) || source.local || source.receiver!=edition)return false;
    return EnqueueBound(edition,source.receiver_cid,source.player_slot,wire,[&]{
        CrossplayNative::RoomRoute fresh;
        return admission.Resolve(source_slot,true,fresh) && fresh.receiver_cid==source.receiver_cid
            && fresh.player_slot==source.player_slot;
    });
}
inline bool EncodeRoom(Crossplay::Edition edition,const CrossplayNative::RoomRoute& own,
                       const CrossplayNative::RoomRoute& target,uint32_t sequence,const void* event,
                       std::vector<unsigned char>& frame) {
    SharedEngineMode::ErrorScope error;LoadProgressDetail::FpScope fp;frame.clear();
    std::vector<unsigned char> wire;
    return own.local && !target.local && own.receiver==edition && target.receiver==edition
        && Methods(edition) && Source(edition,event,own.receiver_cid,own.player_slot)
        && Detail::Encode(Pin(edition),event,wire)
        && CrossplayPriorityCommand::Encode(target.peer,sequence,wire,own.receiver_cid,own.player_slot,frame);
}
}
