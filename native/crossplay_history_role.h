#pragma once
#include "crossplay_admission.h"
#include "crossplay_event_codec.h"

// Experimental common-backend host projection. Ordinary adapters exclude it.
// This changes native host ownership, never readiness or a connection result.
namespace CrossplayNative {
namespace HistoryRoleDetail {
inline bool Methods() {
    struct Method {uintptr_t address;size_t size;uint64_t hash;};
    constexpr Method pins[]={
        {0xecbb00,23,0xcf97ecd2117c9991ull},{0xed2b30,10,0x21ea18fc8b0d8918ull},
        {0xed2af0,53,0x8cdecde58fd1a89dull},{0xed7550,10,0x404c0090e7d0c104ull},
        {0xed7290,75,0x5a127b5b3b0f0bc0ull},{0x65e870,17,0x5bf589636f672331ull},
        {0x65df30,17,0x3b7fd539cf73c485ull},{0x65e8b0,22,0x83324d295c3bb120ull}
    };
    for(const auto& method:pins) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(method.address);
        if(!CrossplayNativeCodec::Detail::Readable(bytes,method.size))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<method.size;++i)hash=(hash^bytes[i])*1099511628211ull;
        if(hash!=method.hash)return false;
    }
    return true;
}
inline bool Writable(const void* pointer,size_t size) {
    if(!CrossplayNativeCodec::Detail::Readable(pointer,size))return false;
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(pointer,&memory,sizeof(memory)))return false;
    const DWORD access=memory.Protect&0xff;
    return access==PAGE_READWRITE || access==PAGE_WRITECOPY
        || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
}
inline bool ProjectUbisoftGuestHost(ParticipantEvent& binding,LobbyAdmission& admission) {
    using CrossplayNativeCodec::Detail::Readable;
    if(admission.GetState()!=AdmissionState::RosterBound || !admission.Poll())return false;
    ParticipantEvent identity;
    if(!identity.Bind(Crossplay::Edition::UbisoftHistory))return false;
    LobbySnapshot snapshot;
    if(!binding.CaptureLobby(snapshot) || snapshot.host || snapshot.local_slot!=2 || snapshot.members.size()!=2)return false;
    const Participant* local=nullptr;const Participant* host=nullptr;
    for(const auto& member:snapshot.members) {
        if(member.status!=2)return false;
        if(member.player.local && !member.player.host && member.player.slot==2
            && member.player.connection==admission.OwnConnection())local=&member.player;
        if(!member.player.local && member.player.host && member.player.slot==1
            && member.player.connection==admission.PeerLocalConnection())host=&member.player;
    }
    if(!local || !host || local->connection==host->connection)return false;
    const unsigned char setter[]={0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0x8b,0x45,0xfc,0x8b,0x4d,0x08,0x89,0x48,0x24,0x8b,0xe5,0x5d,0xc2,0x04,0x00};
    const unsigned char getter[]={0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0xe8,0x24,0x70,0x00,0x00,0x8b,0xc8,0xe8,0xdd,0x6f,0x00,0x00,0x8b,0xe5,0x5d,0xc3};
    if(!HistoryRoleDetail::Methods() || !Readable(reinterpret_cast<void*>(0x1379e14),4) || !Readable(reinterpret_cast<void*>(0x65e8b0),sizeof(setter))
        || memcmp(reinterpret_cast<void*>(0x65e8b0),setter,sizeof(setter))
        || !Readable(reinterpret_cast<void*>(0xecbb00),sizeof(getter)) || memcmp(reinterpret_cast<void*>(0xecbb00),getter,sizeof(getter)))return false;
    const auto* backend=*reinterpret_cast<const unsigned char* const*>(0x1379e14);
    if(!Readable(backend,0xa50))return false;
    auto* session=*reinterpret_cast<unsigned char* const*>(backend+0xa4c);
    if(!Readable(session,0x28) || !HistoryRoleDetail::Writable(session+0x24,4) || *reinterpret_cast<const uintptr_t*>(session)!=0x113fe90
        || *reinterpret_cast<const uint32_t*>(session+0x20)!=local->connection
        || *reinterpret_cast<const uint32_t*>(session+0x24)!=local->connection)return false;
    if(!reinterpret_cast<bool (__thiscall*)(void*)>(0xecbb00)(reinterpret_cast<void*>(snapshot.instance)))return false;
    // The authenticated common session elected this foreign native CID as host.
    // Preserve the local CID and invoke the original host setter; no Storm
    // handle, backend principal, Connected flag or acknowledgement is created.
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(0x65e8b0)(session,host->connection);
    return *reinterpret_cast<const unsigned char* const*>(0x1379e14)==backend
        && *reinterpret_cast<unsigned char* const*>(backend+0xa4c)==session
        && *reinterpret_cast<const uint32_t*>(session+0x20)==local->connection
        && *reinterpret_cast<const uint32_t*>(session+0x24)==host->connection
        && !reinterpret_cast<bool (__thiscall*)(void*)>(0xecbb00)(reinterpret_cast<void*>(snapshot.instance));
}
}
