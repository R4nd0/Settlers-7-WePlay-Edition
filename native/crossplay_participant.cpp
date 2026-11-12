#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <limits>
#include "crossplay_participant.h"

#if !defined(_M_IX86)
#error Native participant calls require the verified x86 ABI.
#endif

namespace CrossplayNative {
namespace {
struct NativeString { const char* data; uint32_t capacity, length, extra; };
struct PlayerData {
    uint32_t status, connection, reserved8, slot;
    NativeString name;
    uint32_t principal;
    unsigned char host, reserved25, reserved26, reserved27;
    uint32_t station, reserved2c, reserved30;
};
struct CastleVector { const void* begin; const void* end; const void* capacity; };
struct CheckedCastleVector { uint32_t proxy; CastleVector value; };
struct GoldCastle { NativeString type,name;CheckedCastleVector parts,parents,slots; };
struct HistoryCastle { uint32_t vtable;NativeString type,name;CastleVector parts,parents,slots; };
static_assert(sizeof(GoldCastle)==0x50 && sizeof(HistoryCastle)==0x48,"Native castle source layouts");
static_assert(sizeof(PlayerData)==0x34 && sizeof(NativeString)==16,"Native player ABI");
struct Profile {
    Crossplay::Edition edition;
    const char* file_hash;
    uint32_t file_size, added, confirmed, vtable, destroy, castle_vtable, clone;
    size_t object_size, extra_offset, local_offset, castle_size;
    uint32_t main_pointer, main_vtable, transport_vtable, send;
    uint32_t session_vtable, roster_offset;
};
constexpr Profile Profiles[] = {
    {Crossplay::Edition::Gold,
     "76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab",
     14141768,0xa50370,0xaa6fe0,0xf56238,0xa47620,0,0xa475b0,0x9c,0x94,0x98,0x50,
     0x10f83dc,0xf55600,0xf566cc,0xa4e430,0xf569bc,0x1c},
    {Crossplay::Edition::UbisoftHistory,
     "4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f",
     17333680,0x656f50,0x690c20,0x113f2f4,0x657060,0x11372d0,0x656fe0,0x94,0x8c,0x90,0x48,
     0x13a5210,0x113f2c0,0x11404f0,0xedb300,0x11403fc,0x18},
    {Crossplay::Edition::SteamHistory,
     "8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410",
     17459856,0x656b30,0x690800,0x113d814,0x656c40,0x11357f8,0x656bc0,0x94,0x8c,0x90,0x48,
     0x13a230c,0x113d7ec,0x113ea18,0xeda9f0,0x113e924,0x18}
};

bool Readable(const void* pointer,size_t size) {
    uintptr_t at=reinterpret_cast<uintptr_t>(pointer);
    if (!at || !size || size>std::numeric_limits<uintptr_t>::max()-at) return false;
    const uintptr_t end=at+size;
    while(at<end) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<const void*>(at),&region,sizeof(region))
            || region.State!=MEM_COMMIT || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        const DWORD access=region.Protect&0xff;
        if(access!=PAGE_READONLY && access!=PAGE_READWRITE && access!=PAGE_WRITECOPY
            && access!=PAGE_EXECUTE_READ && access!=PAGE_EXECUTE_READWRITE
            && access!=PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t begin=reinterpret_cast<uintptr_t>(region.BaseAddress);
        if(region.RegionSize>std::numeric_limits<uintptr_t>::max()-begin) return false;
        const uintptr_t next=begin+region.RegionSize;
        if(next<=at) return false;
        at=next;
    }
    return true;
}
bool SameBytes(uint32_t address,const unsigned char* bytes,size_t size) {
    const auto* pointer=reinterpret_cast<const void*>(address);
    return Readable(pointer,size) && !memcmp(pointer,bytes,size);
}
bool Methods(const Profile& profile) {
    if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000) return false;
    const unsigned char prologue[]={0x55,0x8b,0xec,0x6a,0xff};
    if(!SameBytes(profile.added,prologue,sizeof(prologue))
        || !SameBytes(profile.confirmed,prologue,sizeof(prologue))
        || !Readable(reinterpret_cast<void*>(profile.vtable),24)) return false;
    return *reinterpret_cast<const uint32_t*>(profile.vtable)==profile.destroy
        && *reinterpret_cast<const uint32_t*>(profile.vtable+20)==profile.clone
        && SameBytes(profile.clone,prologue,sizeof(prologue));
}
bool FileMatches(const Profile& profile) {
    wchar_t path[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,32768);
    if(!length || length>=32768) return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    bool okay=GetFileInformationByHandle(file,&info) && !info.nFileSizeHigh
        && info.nFileSizeLow==profile.file_size
        && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD object_size=0,returned=0;
    if(okay) okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(okay) okay=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&returned,0)>=0
        && returned==sizeof(object_size) && object_size && object_size<=65536;
    std::vector<unsigned char> object(object_size);
    if(okay) okay=BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0)>=0;
    std::array<unsigned char,65536> buffer{};
    uint64_t consumed=0;
    while(okay) {
        DWORD count=0;
        if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)) { okay=false;break; }
        if(!count) break;
        consumed+=count;
        if(consumed>profile.file_size || BCryptHashData(hash,buffer.data(),count,0)<0) okay=false;
    }
    std::array<unsigned char,32> digest{};
    if(okay) okay=consumed==profile.file_size && BCryptFinishHash(hash,digest.data(),32,0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);
    constexpr char hex[]="0123456789abcdef";
    if(okay) for(size_t i=0;i<digest.size();++i) {
        if(hex[digest[i]>>4]!=profile.file_hash[2*i] || hex[digest[i]&15]!=profile.file_hash[2*i+1]) return false;
    }
    return okay;
}

// Gold's constructors are NOT thiscall. ESI is the borrowed SPlayerData,
// EDX is the subtype for the default-castle form, and the callee pops its
// remaining arguments. Our ordinary C++ wrapper retains caller nonvolatiles.
void* GoldAdded(uintptr_t address,void* output,const PlayerData* source,
                uint32_t subtype,uint32_t connection,uint32_t local) {
    void* result=nullptr;
    __asm {
        mov esi,source
        mov edx,subtype
        push local
        push connection
        push output
        call address
        mov result,eax
    }
    return result;
}
void* GoldConfirmed(uintptr_t address,void* output,const PlayerData* source,
                    uint32_t connection,uint32_t local,const void* castle,uint32_t extra) {
    void* result=nullptr;
    __asm {
        mov esi,source
        push extra
        push castle
        push local
        push connection
        push output
        call address
        mov result,eax
    }
    return result;
}
void* HistoryConstruct(uintptr_t address,void* output,const PlayerData* source,
    uint32_t subtype,uint32_t connection,uint32_t local,const void* castle,uint32_t extra,bool confirmed) {
    if(confirmed) {
        using Constructor=void* (__thiscall*)(void*,uint32_t,uint32_t,const PlayerData*,uint32_t,const void*,uint32_t);
        return reinterpret_cast<Constructor>(address)(output,subtype,connection,source,local,castle,extra);
    }
    using Constructor=void* (__thiscall*)(void*,uint32_t,uint32_t,const PlayerData*,uint32_t);
    return reinterpret_cast<Constructor>(address)(output,subtype,connection,source,local);
}
bool Eligible(Change change,const Participant& member,const Profile& profile) {
    if(change!=Change::Added && change!=Change::Confirmed
        && change!=Change::Leaving && change!=Change::Removed) return false;
    if(member.connection==UINT32_MAX || member.station==UINT32_MAX
        || (member.slot==UINT32_MAX ? change!=Change::Added : member.slot<1 || member.slot>24)
        || member.name.size()>64) return false;
    // Original Gold callbacks legitimately carry PID zero (the native
    // preparation update then uses its assigned slot). This is not an account
    // authentication check; PID must stay independent of CID and StationID.
    if(profile.edition==Crossplay::Edition::Gold && (!member.connection
        || (!member.station && (change==Change::Added || change==Change::Confirmed)))) return false;
    for(unsigned char c:member.name) if(c<0x20 || c>0x7e) return false;
    return true;
}
void LocalSend(uintptr_t address,void* transport,const void* event) {
    using Send=void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t);
    // Mode zero takes the native clone-and-queue branch in all three images.
    reinterpret_cast<Send>(address)(transport,event,0,0,0,0);
}
void* CurrentTransport(const Profile& profile) {
    const unsigned char gold_prefix[]={0x55,0x8b,0xec,0x8b,0x45,0x0c,0x83,0xe8,0x00,0x56};
    const unsigned char history_prefix[]={0x55,0x8b,0xec,0x83,0xec,0x18,0x89,0x4d,0xfc,0x8b};
    const auto* prefix=profile.edition==Crossplay::Edition::Gold?gold_prefix:history_prefix;
    if(!SameBytes(profile.send,prefix,sizeof(gold_prefix))
        || !Readable(reinterpret_cast<void*>(profile.transport_vtable),24)
        || *reinterpret_cast<const uint32_t*>(profile.transport_vtable+20)!=profile.send
        || !Readable(reinterpret_cast<void*>(profile.main_pointer),4)) return nullptr;
    const auto* main=*reinterpret_cast<const unsigned char* const*>(profile.main_pointer);
    if(!Readable(main,0x14) || *reinterpret_cast<const uint32_t*>(main)!=profile.main_vtable) return nullptr;
    void* transport=*reinterpret_cast<void* const*>(main+0x10);
    const bool gold=profile.edition==Crossplay::Edition::Gold;
    if(!Readable(transport,gold?0x5c:0x40)
        || *static_cast<const uint32_t*>(transport)!=profile.transport_vtable) return nullptr;
    const auto* bytes=static_cast<const unsigned char*>(transport);
    const void* lock=*reinterpret_cast<void* const*>(bytes+(gold?0x18:0x14));
    if(!Readable(lock,sizeof(CRITICAL_SECTION))) return nullptr;
    return transport;
}
bool SnapshotLobby(const Profile& profile,LobbySnapshot& result) {
    result={};
    if(!CurrentTransport(profile)) return false;
    const auto* main=*reinterpret_cast<const unsigned char* const*>(profile.main_pointer);
    if(!Readable(main,0x18)) return false;
    const auto* session=*reinterpret_cast<const unsigned char* const*>(main+0x14);
    if(!Readable(session,profile.roster_offset+8)
        || *reinterpret_cast<const uint32_t*>(session)!=profile.session_vtable
        || *reinterpret_cast<const uint32_t*>(session+0x10)!=1 || session[0x14]>1) return false;
    LobbySnapshot snapshot; snapshot.instance=reinterpret_cast<uintptr_t>(session);
    snapshot.local_slot=*reinterpret_cast<const uint32_t*>(session+0xc);snapshot.host=session[0x14]!=0;
    if(snapshot.local_slot<1 || snapshot.local_slot>24) return false;
    const auto* head=*reinterpret_cast<const unsigned char* const*>(session+profile.roster_offset);
    const auto count=*reinterpret_cast<const uint32_t*>(session+profile.roster_offset+4);
    if(!count || count>24 || !Readable(head,8)) return false;
    const auto* node=*reinterpret_cast<const unsigned char* const*>(head);
    const auto* previous=head;uint32_t local_count=0;
    for(uint32_t i=0;i<count;++i) {
        if(node==head || !Readable(node,8+sizeof(PlayerData))
            || *reinterpret_cast<const unsigned char* const*>(node+4)!=previous) return false;
        const auto* data=reinterpret_cast<const PlayerData*>(node+8);
        if(data->status<1 || data->status>6 || data->host>1 || data->name.length>64
            || (data->name.length && (data->name.capacity<=data->name.length
                || !Readable(data->name.data,data->name.length+1) || data->name.data[data->name.length]))) return false;
        Participant member{data->connection,data->slot,data->principal,data->station,
            data->name.length?std::string(data->name.data,data->name.length):std::string(),
            data->host!=0,data->slot==snapshot.local_slot};
        if(!Eligible(data->status==1?Change::Added:data->status==2?Change::Confirmed:Change::Leaving,member,profile)) return false;
        for(const auto& prior:snapshot.members)
            if(prior.player.connection==member.connection || (member.slot!=UINT32_MAX && prior.player.slot==member.slot)) return false;
        if(member.local) {
            if(member.host!=snapshot.host || data->status!=2) return false;
            ++local_count;
        }
        snapshot.members.push_back({std::move(member),data->status});previous=node;
        node=*reinterpret_cast<const unsigned char* const*>(node);
    }
    if(node!=head || *reinterpret_cast<const unsigned char* const*>(head+4)!=previous || local_count!=1) return false;
    result=std::move(snapshot);return true;
}
NativeString BorrowedText(const std::string& value) {
    return {value.c_str(),static_cast<uint32_t>(value.size()+1),static_cast<uint32_t>(value.size()),0};
}
struct CastleSource {
    GoldCastle gold{};HistoryCastle history{};
    std::vector<NativeString> parts;std::vector<uint32_t> parents,slots;
    CastleSource(const Profile& profile,const castle_wire::Design& design) {
        for(const auto& part:design.parts) {
            parts.push_back(BorrowedText(part.asset));parents.push_back(part.parent);slots.push_back(part.slot);
        }
        static const std::string empty;
        gold.type=history.type=BorrowedText(design.type);gold.name=history.name=BorrowedText(empty);
        history.vtable=profile.castle_vtable;
        if(!parts.empty()) {
            gold.parts.value=history.parts={parts.data(),parts.data()+parts.size(),parts.data()+parts.size()};
            gold.parents.value=history.parents={parents.data(),parents.data()+parents.size(),parents.data()+parents.size()};
            gold.slots.value=history.slots={slots.data(),slots.data()+slots.size(),slots.data()+slots.size()};
        }
    }
    const void* Get(const Profile& profile) const {
        return profile.edition==Crossplay::Edition::Gold?static_cast<const void*>(&gold):static_cast<const void*>(&history);
    }
};
bool CastleText(const NativeString& source,std::string& result) {
    result.clear();
    if(source.length>castle_wire::MaxAsset || source.capacity<=source.length
        || !Readable(source.data,source.length+1) || source.data[source.length]) return false;
    result.assign(source.data,source.length);
    return result.empty() || castle_wire::Asset(result);
}
bool VectorCount(const CastleVector& vector,size_t stride,size_t& result) {
    const auto begin=reinterpret_cast<uintptr_t>(vector.begin),end=reinterpret_cast<uintptr_t>(vector.end);
    const auto capacity=reinterpret_cast<uintptr_t>(vector.capacity);
    if(end<begin || capacity<end || (end-begin)%stride || (end-begin)/stride>castle_wire::MaxParts) return false;
    result=(end-begin)/stride;
    return !result || Readable(vector.begin,end-begin);
}
bool SnapshotCastle(const Profile& profile,const void* source,castle_wire::Design& result) {
    result={};
    if(!Readable(source,profile.castle_size)) return false;
    NativeString type{};CastleVector parts{},parents{},slots{};
    if(profile.edition==Crossplay::Edition::Gold) {
        const auto& value=*static_cast<const GoldCastle*>(source);
        type=value.type;parts=value.parts.value;parents=value.parents.value;slots=value.slots.value;
    } else {
        const auto& value=*static_cast<const HistoryCastle*>(source);
        if(value.vtable!=profile.castle_vtable) return false;
        type=value.type;parts=value.parts;parents=value.parents;slots=value.slots;
    }
    castle_wire::Design design;
    // Original default castles use a null CString, capacity zero and length zero.
    if(type.length ? !CastleText(type,design.type) : type.data && (!Readable(type.data,1) || *type.data)) return false;
    size_t count=0,parent_count=0,slot_count=0;
    if(!VectorCount(parts,16,count) || !VectorCount(parents,4,parent_count)
        || !VectorCount(slots,4,slot_count) || count!=parent_count || count!=slot_count) return false;
    for(size_t i=0;i<count;++i) {
        castle_wire::Part part;
        if(!CastleText(static_cast<const NativeString*>(parts.begin)[i],part.asset)) return false;
        part.parent=static_cast<const uint32_t*>(parents.begin)[i];part.slot=static_cast<const uint32_t*>(slots.begin)[i];
        design.parts.push_back(std::move(part));
    }
    std::vector<unsigned char> encoded;
    if(!castle_wire::Encode(design,encoded)) return false;
    result=std::move(design);return true;
}
}

struct ParticipantEvent::Impl {
    const Profile* profile=nullptr;
    DWORD owner=0;
    uint32_t live=0;
    ParticipantFailure failure=ParticipantFailure::None;
    alignas(8) std::array<unsigned char,0xa0> storage{};
};
ParticipantEvent::ParticipantEvent():impl(new Impl) {}
ParticipantEvent::~ParticipantEvent() {
    if(!Reset()) RaiseFailFastException(nullptr,nullptr,0);
}
bool ParticipantEvent::Bind(Crossplay::Edition edition) {
    if(impl->owner && impl->owner!=GetCurrentThreadId()) return false;
    impl->failure=ParticipantFailure::State;
    if(impl->profile || impl->live) return false;
    for(const auto& profile:Profiles) if(profile.edition==edition) {
        impl->failure=ParticipantFailure::Executable;
        if(!FileMatches(profile)) return false;
        impl->failure=ParticipantFailure::NativeMethods;
        if(!Methods(profile)) return false;
        impl->owner=GetCurrentThreadId();impl->profile=&profile;impl->failure=ParticipantFailure::None;return true;
    }
    return false;
}
bool ParticipantEvent::Construct(Change change,const Participant& member,const void* castle,uint32_t extra,bool ready) {
    if(impl->owner && impl->owner!=GetCurrentThreadId()) return false;
    const auto* profile=impl->profile;
    impl->failure=ParticipantFailure::State;
    if(!profile || impl->live) return false;
    impl->failure=ParticipantFailure::NativeMethods;
    if(!Methods(*profile)) return false;
    impl->failure=ParticipantFailure::Participant;
    if(!Eligible(change,member,*profile) || (ready && (change!=Change::Confirmed || profile->edition==Crossplay::Edition::Gold))) return false;
    impl->failure=ParticipantFailure::Castle;
    if(change==Change::Confirmed) {
        if(!Readable(castle,profile->castle_size)) return false;
        // A nonzero native castle vtable is independently pinned for each HE.
        if(profile->edition!=Crossplay::Edition::Gold && (!profile->castle_vtable
            || *static_cast<const uint32_t*>(castle)!=profile->castle_vtable)) return false;
    } else if(castle || extra) return false;
    const uint32_t status=change==Change::Added?1u:change==Change::Confirmed?2u:6u;
    PlayerData data{status,member.connection,0,member.slot,
        {member.name.c_str(),static_cast<uint32_t>(member.name.size()+1),static_cast<uint32_t>(member.name.size()),0},
        member.principal,static_cast<unsigned char>(member.host),static_cast<unsigned char>(ready),0,0,member.station,0,0};
    impl->storage.fill(0);
    void* output=impl->storage.data();void* result=nullptr;
    const uint32_t subtype=static_cast<uint32_t>(change),local=member.local?1u:0u;
    if(profile->edition==Crossplay::Edition::Gold) {
        result=change==Change::Confirmed
            ?GoldConfirmed(profile->confirmed,output,&data,member.connection,local,castle,extra)
            :GoldAdded(profile->added,output,&data,subtype,member.connection,local);
    } else {
        result=HistoryConstruct(change==Change::Confirmed?profile->confirmed:profile->added,
            output,&data,subtype,member.connection,local,castle,extra,change==Change::Confirmed);
    }
    impl->live=true;
    const auto* bytes=impl->storage.data();uint32_t header=0;
    memcpy(&header,bytes+4,4);
    const auto* copied=reinterpret_cast<const PlayerData*>(bytes+0x10);
    if(result!=output || *reinterpret_cast<const uint32_t*>(bytes)!=profile->vtable
        || header!=(7u|(subtype<<16)) || *reinterpret_cast<const uint32_t*>(bytes+8)!=member.connection
        || bytes[profile->local_offset]!=local || copied->status!=status
        || copied->connection!=member.connection || copied->slot!=member.slot
        || copied->principal!=member.principal || copied->station!=member.station
        || copied->host!=static_cast<unsigned char>(member.host)
        || copied->reserved25!=static_cast<unsigned char>(ready)
        || copied->name.length!=member.name.size()
        || (member.name.empty()
            ? (copied->name.data && (!Readable(copied->name.data,1) || *copied->name.data))
            : (copied->name.capacity<member.name.size()+1 || copied->name.data==member.name.c_str()
                || !Readable(copied->name.data,member.name.size()+1)
                || memcmp(copied->name.data,member.name.c_str(),member.name.size()+1)))
        || (change==Change::Confirmed && *reinterpret_cast<const uint32_t*>(bytes+profile->extra_offset)!=extra)) {
        RaiseFailFastException(nullptr,nullptr,0);return false;
    }
    impl->failure=ParticipantFailure::None;
    return true;
}
bool ParticipantEvent::ConstructConfirmed(const Participant& member,const castle_wire::Design& castle,uint32_t extra,bool ready) {
    if(impl->owner && impl->owner!=GetCurrentThreadId()) return false;
    impl->failure=ParticipantFailure::State;
    if(!impl->profile || impl->live) return false;
    impl->failure=ParticipantFailure::Castle;
    std::vector<unsigned char> encoded;
    if(!castle_wire::Encode(castle,encoded)) return false;
    const CastleSource source(*impl->profile,castle);
    return Construct(Change::Confirmed,member,source.Get(*impl->profile),extra,ready);
}
bool ParticipantEvent::CaptureCastle(const void* source,castle_wire::Design& result) const {
    result={};
    return impl->owner==GetCurrentThreadId() && impl->profile && Methods(*impl->profile)
        && SnapshotCastle(*impl->profile,source,result);
}
const void* ParticipantEvent::Get() const {
    return impl->owner==GetCurrentThreadId() && impl->live?impl->storage.data():nullptr;
}
bool ParticipantEvent::CaptureLobby(LobbySnapshot& result) const {
    result={};
    return impl->owner==GetCurrentThreadId() && impl->profile && Methods(*impl->profile)
        && SnapshotLobby(*impl->profile,result);
}
bool ParticipantEvent::EnqueueLocal() {
    if(impl->owner && impl->owner!=GetCurrentThreadId()) return false;
    impl->failure=ParticipantFailure::State;
    if(!impl->profile || !impl->live || (impl->live&2u)) return false;
    impl->failure=ParticipantFailure::NativeMethods;
    if(!Methods(*impl->profile)
        || *reinterpret_cast<const uint32_t*>(impl->storage.data())!=impl->profile->vtable) return false;
    impl->failure=ParticipantFailure::Transport;
    void* transport=CurrentTransport(*impl->profile);
    if(!transport) return false;
    // The original method clones with its own allocator and transfers the
    // clone to its queue. Our original event remains owned until Reset.
    LocalSend(impl->profile->send,transport,impl->storage.data());
    impl->live|=2u;impl->failure=ParticipantFailure::None;return true;
}
bool ParticipantEvent::Reset() {
    if(impl->owner && impl->owner!=GetCurrentThreadId()) return false;
    if(!impl->live) return true;
    if(!Methods(*impl->profile)
        || *reinterpret_cast<const uint32_t*>(impl->storage.data())!=impl->profile->vtable) return false;
    using Destroy=void* (__thiscall*)(void*,uint32_t);
    reinterpret_cast<Destroy>(impl->profile->destroy)(impl->storage.data(),0);
    impl->live=false;impl->storage.fill(0);return true;
}
ParticipantFailure ParticipantEvent::Failure() const {
    return impl->owner && impl->owner!=GetCurrentThreadId()?ParticipantFailure::Thread:impl->failure;
}
}
