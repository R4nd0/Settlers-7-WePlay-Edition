#pragma once
#include "crossplay_ready_state_codec.h"

// Experimental two-human/open-slot lobby only. MapSetup.Load reconstructs the
// preparation rows. Retain owned native events across that call, then reapply
// their admitted identities through the original setters and queue consumers.
// This is not content agreement or permission to start a world.
namespace CrossplayNative {
namespace HistoryMapReloadDetail {
struct Method { uintptr_t address;size_t size;uint64_t hash; };
inline std::array<Method,10> Methods(Crossplay::Edition edition) {
    const bool steam=edition==Crossplay::Edition::SteamHistory;
    return {{{steam?0x545a90u:0x546440u,13,0x01d1c2021a67c88eull},
        {steam?0x545aa0u:0x546450u,29,steam?0xe972815518a0b3d5ull:0x57a850529cd07c8dull},
        {steam?0x545ad0u:0x546480u,13,0xe540581fdd5511b2ull},
        {steam?0x585db0u:0x5862f0u,13,0x5e451817ca589186ull},
        {steam?0x585da0u:0x5862e0u,13,0x0bed4880da5fee7aull},
        {steam?0x585d80u:0x5862c0u,13,0x2e5fa17e197b46bfull},
        {steam?0x545ac0u:0x546470u,13,0x518d812d862b8696ull},
        {steam?0x529a30u:0x52a510u,95,steam?0x683657b533daf25eull:0xfd40267e44bcb0e0ull},
        {steam?0x6572b0u:0x6576d0u,104,steam?0xe4813dc6e3a8d24bull:0x17d2d709d35cf21bull},
        {steam?0xef5f60u:0xef6870u,180,steam?0x252605f4530c8512ull:0x189401d218ee0ea7ull}}};
}
inline bool Guard(Crossplay::Edition edition) {
    if(edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)return false;
    for(const auto& pin:Methods(edition)) {
        const auto* code=reinterpret_cast<const unsigned char*>(pin.address);
        if(!CrossplayNativeCodec::Detail::Readable(code,pin.size))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<pin.size;++i)hash=(hash^code[i])*1099511628211ull;
        if(hash!=pin.hash)return false;
    }
    return true;
}
}
class HistoryMapReload {
    enum class Stage { New,Captured,Queued,Observed,Failed };
    Stage stage=Stage::New;
    Crossplay::Edition edition{};
    CrossplaySessionConfig::Connections ids{};
    uintptr_t session=0,manager=0,configuration=0,rows_address=0,cache=0;
    unsigned char manager_mode=0;
    std::string map;
    Participant players[2];
    ParticipantEvent events[2];
    castle_wire::Design castles[2];
    uint32_t extras[2]{},ready[2]{};
    ULONGLONG started=0;
    bool Exclude() {stage=Stage::Failed;return false;}
    bool ProjectRoomMap() {
        using CrossplayNativeCodec::Detail::Readable;
        const bool steam=edition==Crossplay::Edition::SteamHistory;
        const auto* backend=*reinterpret_cast<const unsigned char* const*>(steam?0x1376f04:0x1379e14);
        if(!Readable(backend,0xa50))return false;
        const auto* handler=*reinterpret_cast<const unsigned char* const*>(backend+0xa4c);
        if(!Readable(handler,0x28) || *reinterpret_cast<const uintptr_t*>(handler)!=(steam?0x113e3acu:0x113fe90u))return false;
        const auto* implementation=*reinterpret_cast<const unsigned char* const*>(handler+0x1c);
        if(!Readable(implementation,0x18))return false;
        auto* descriptor=*reinterpret_cast<unsigned char* const*>(implementation+0x10);
        const uintptr_t type=steam?0x113e678:0x1140150,property_type=steam?0x1084d6c:0x1086d3c;
        if(!Readable(descriptor,0x1ec) || *reinterpret_cast<const uintptr_t*>(descriptor)!=type
            || *reinterpret_cast<const uintptr_t*>(descriptor+0x1c8)!=property_type
            || !HistoryRoleDetail::Writable(descriptor+0x1c8,0x24) || map.size()>255
            || *reinterpret_cast<const uintptr_t*>(type+0x18)!=HistoryMapReloadDetail::Methods(edition)[9].address)return false;
        auto* dirty=*reinterpret_cast<unsigned char* const*>(descriptor+0x1d0);
        if(!HistoryRoleDetail::Writable(dirty,1))return false;
        struct SmallString {unsigned char storage[16];uint32_t length,capacity;};
        static_assert(sizeof(SmallString)==24,"Original room-map by-value ABI");
        SmallString owned{};
        const auto methods=HistoryMapReloadDetail::Methods(edition);
        reinterpret_cast<void* (__thiscall*)(void*,const char*)>(methods[8].address)(&owned,map.c_str());
        // This original by-value setter owns and destroys its parameter. The
        // trivial local view must not destroy the transferred allocation again.
        reinterpret_cast<void (__thiscall*)(void*,SmallString)>(methods[9].address)(descriptor,owned);
        const auto* result=reinterpret_cast<const SmallString*>(descriptor+0x1d4);
        if(result->length!=map.size() || result->capacity<15 || result->capacity<result->length)return false;
        const auto* data=result->capacity<16?reinterpret_cast<const char*>(result->storage):*reinterpret_cast<const char* const*>(result->storage);
        return Readable(data,map.size()+1) && !memcmp(data,map.c_str(),map.size()+1);
    }
    unsigned char* Rows(const ParticipantEvent& binding) const {
        using CrossplayNativeCodec::Detail::Readable;
        HistoryPreparationDetail::Layout pin{};LobbySnapshot snapshot;
        if(!HistoryPreparationDetail::LayoutFor(edition,pin)
            || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,false)
            || !binding.CaptureLobby(snapshot) || snapshot.instance!=session)return nullptr;
        if(stage!=Stage::New)for(const auto& member:snapshot.members) {
            const auto& current=member.player;const auto& saved=players[current.slot-1];
            if(current.principal!=saved.principal || current.name!=saved.name
                || current.connection!=saved.connection || current.station!=saved.station)return nullptr;
        }
        if(!Readable(reinterpret_cast<void*>(pin.backend_pointer),4))return nullptr;
        const auto* backend=*reinterpret_cast<const unsigned char* const*>(pin.backend_pointer);
        if(!Readable(backend,0xa50))return nullptr;
        const auto* backend_session=*reinterpret_cast<const unsigned char* const*>(backend+0xa4c);
        if(!Readable(backend_session,0x28) || *reinterpret_cast<const uintptr_t*>(backend_session)!=pin.session_type
            || CrossplayChat::Word(backend_session+0x20)!=ids.guest
            || CrossplayChat::Word(backend_session+0x24)!=ids.host)return nullptr;
        const auto* native_session=reinterpret_cast<const unsigned char*>(session);
        if(!Readable(native_session,0x2c) || CrossplayChat::Word(native_session+0x28)!=2
            || !Readable(reinterpret_cast<void*>(pin.manager_pointer),4))return nullptr;
        const auto* current=*reinterpret_cast<const unsigned char* const*>(pin.manager_pointer);
        if(reinterpret_cast<uintptr_t>(current)!=manager || !Readable(current,0x190)
            || *reinterpret_cast<const uintptr_t*>(current)!=pin.manager_type || current[4]!=manager_mode)return nullptr;
        auto* config=*reinterpret_cast<unsigned char* const*>(current+((manager_mode-1)&1)*0xc4+0x98);
        if(reinterpret_cast<uintptr_t>(config)!=configuration || !Readable(config,8))return nullptr;
        auto* rows=*reinterpret_cast<unsigned char* const*>(config+4);
        if(!Readable(rows,0x8dc0) || *reinterpret_cast<unsigned char* const*>(config)!=rows || rows[0x8db8]!=4)return nullptr;
        auto* native_cache=CrossplayNativeCodec::ReadyStateDetail::HistoryConfiguration(edition);
        std::string current_map;
        if(reinterpret_cast<uintptr_t>(native_cache)!=cache || !native_cache
            || !CrossplayNativeCodec::SessionConfigDetail::NativeString(native_cache+0x24,current_map) || current_map!=map)return nullptr;
        const auto* slots=*reinterpret_cast<const unsigned char* const*>(native_cache+0x44);
        if(!Readable(slots,8*0x18) || *reinterpret_cast<const unsigned char* const*>(native_cache+0x48)!=slots+8*0x18)return nullptr;
        for(unsigned i=0;i<4;++i) {
            const auto* slot=slots+i*0x18;
            if(CrossplayChat::Word(slot)!=(i<2?3u:1u)
                || CrossplayChat::Word(slot+8)!=(i<2?i+1:UINT32_MAX))return nullptr;
        }
        for(unsigned i=1;i<=4;++i)
            if(!HistoryRoleDetail::Writable(rows+i*0x5a8,0x5a8))return nullptr;
        return rows;
    }
public:
    bool Active() const {return stage!=Stage::New;}
    bool Complete() const {return stage==Stage::Observed;}
    bool Failed() const {return stage==Stage::Failed;}
    bool Begin(Crossplay::Edition value,ParticipantEvent& binding,LobbyAdmission& admission) {
        using namespace CrossplayNativeCodec;
        if(stage!=Stage::New || !HistoryMapReloadDetail::Guard(value)
            || !HistoryGuestPresentationReady(value,binding,admission))return false;
        edition=value;ids={admission.PeerLocalConnection(),admission.OwnConnection()};
        LobbySnapshot snapshot;unsigned char* rows=nullptr;
        if(!ReadyStateDetail::Preparation(edition,binding,ids,snapshot,rows))return Exclude();
        HistoryPreparationDetail::Layout pin{};HistoryPreparationDetail::LayoutFor(edition,pin);
        const auto* current=*reinterpret_cast<const unsigned char* const*>(pin.manager_pointer);
        manager=reinterpret_cast<uintptr_t>(current);manager_mode=current[4];session=snapshot.instance;
        configuration=*reinterpret_cast<const uintptr_t*>(current+((manager_mode-1)&1)*0xc4+0x98);
        cache=reinterpret_cast<uintptr_t>(ReadyStateDetail::HistoryConfiguration(edition));
        if(!cache || !SessionConfigDetail::NativeString(reinterpret_cast<const unsigned char*>(cache)+0x24,map)
            || !CrossplaySessionConfig::RelativeMap(map) || Rows(binding)!=rows)return Exclude();
        for(const auto& member:snapshot.members) {
            const unsigned i=member.player.slot-1;const auto* row=rows+(i+1)*0x5a8;
            players[i]=member.player;extras[i]=CrossplayChat::Word(row+0x30);ready[i]=row[0x71];
            if(!binding.CaptureCastle(row+0x55c,castles[i]) || !events[i].Bind(edition)
                || !events[i].ConstructConfirmed(players[i],castles[i],extras[i],ready[i]!=0))return Exclude();
        }
        if(!ProjectRoomMap())return Exclude();
        started=GetTickCount64();stage=Stage::Captured;return true;
    }
    bool Commit(ParticipantEvent& binding,LobbyAdmission& admission) {
        if(stage==Stage::New || stage==Stage::Failed)return false;
        if(stage!=Stage::Observed && GetTickCount64()-started>10000)return Exclude();
        if(admission.GetState()!=AdmissionState::RosterBound || !admission.Poll()
            || !HistoryMapReloadDetail::Guard(edition))return Exclude();
        auto* rows=Rows(binding);if(!rows)return Exclude();
        if(stage==Stage::Captured) {
            const auto methods=HistoryMapReloadDetail::Methods(edition);
            using Set=void (__thiscall*)(void*,uint32_t);
            for(unsigned i=0;i<4;++i) {
                auto* row=rows+(i+1)*0x5a8;
                reinterpret_cast<Set>(methods[2].address)(row,1);
                reinterpret_cast<Set>(methods[3].address)(row,i<2?3:1);
                reinterpret_cast<Set>(methods[4].address)(row,i<2?i+1:UINT32_MAX);
                reinterpret_cast<Set>(methods[5].address)(row,i<2?ready[i]:0);
                if(i>=2)continue;
                reinterpret_cast<Set>(methods[0].address)(row,players[i].principal?players[i].principal:i+1);
                struct String {const char* data;uint32_t capacity,length,extra;};
                const String name{players[i].name.c_str(),static_cast<uint32_t>(players[i].name.size()+1),static_cast<uint32_t>(players[i].name.size()),0};
                reinterpret_cast<void (__thiscall*)(void*,const String*)>(methods[1].address)(row,&name);
                reinterpret_cast<Set>(methods[6].address)(row,extras[i]);
                const auto* event=static_cast<const unsigned char*>(events[i].Get());
                if(!event)return Exclude();
                reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t)>(methods[7].address)(row,event+0x44,1);
            }
            if(!events[0].EnqueueLocal() || !events[1].EnqueueLocal())return Exclude();
            rows_address=reinterpret_cast<uintptr_t>(rows);stage=Stage::Queued;return false;
        }
        if(reinterpret_cast<uintptr_t>(rows)!=rows_address
            || !HistoryGuestPresentationReady(edition,binding,admission))return Exclude();
        const auto* native_cache=reinterpret_cast<const unsigned char*>(cache);
        const auto* slots=*reinterpret_cast<const unsigned char* const*>(native_cache+0x44);
        for(unsigned i=0;i<2;++i) {
            const auto* row=rows+(i+1)*0x5a8;castle_wire::Design current;
            std::vector<unsigned char> expected,actual;
            if(row[0x71]!=ready[i] || !binding.CaptureCastle(row+0x55c,current)
                || !castle_wire::Encode(castles[i],expected) || !castle_wire::Encode(current,actual) || actual!=expected)return Exclude();
            if(slots[i*0x18+4]!=ready[i])return false;
        }
        stage=Stage::Observed;return true;
    }
};
}
