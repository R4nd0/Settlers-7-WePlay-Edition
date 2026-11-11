#pragma once
#include "crossplay_participant.h"
#include "crossplay_chat.h"
#include <windows.h>
#include <cstring>
#if !defined(_M_IX86)
#error Native event factories require the guarded x86 ABI.
#endif

// Experimental loopback lobby chat only; ordinary adapters do not include it.
namespace CrossplayNativeCodec {
namespace Detail {
struct Profile { uintptr_t stream_ctor,stream_vt,writer_vt,reader_vt,write,read,event_vt;bool gold; };
inline Profile Pin(Crossplay::Edition edition) {
    if(edition==Crossplay::Edition::Gold)return {0x41f2b0,0xf26560,0xf2821c,0xf2818c,0x434990,0x4348b0,0xf56238,true};
    if(edition==Crossplay::Edition::UbisoftHistory)return {0x4e9d40,0x112f5bc,0x1130e98,0x1130f38,0x4fc540,0x4fc420,0x113f2f4,false};
    return {0x4e9a60,0x112db4c,0x112f41c,0x112f4bc,0x4fba70,0x4fb950,0x113d814,false};
}
struct Stream { uintptr_t vt;void* bytes;uint32_t capacity,length,position;uint32_t external;uint32_t aux[4]; };
struct Visitor { uintptr_t vt;uint32_t reading;Stream* stream;uint32_t flags; };
static_assert(sizeof(Stream)==0x28 && sizeof(Visitor)==16,"Native codec views");
inline void GoldCtor(uintptr_t address,void* output) {
    __asm {
        mov eax,output
        call address
    }
}
inline void GoldWrite(uintptr_t address,void* visitor,const void* event,const char* name) {
    __asm {
        mov esi,visitor
        mov edi,event
        push 0
        push name
        call address
    }
}
inline void* GoldRead(uintptr_t address,void* visitor,const char* name) {
    void* result=nullptr;
    __asm {
        mov edi,visitor
        xor ecx,ecx
        push name
        call address
        mov result,eax
    }
    return result;
}
inline bool Readable(const void* p,size_t size) {
    MEMORY_BASIC_INFORMATION m{};const uintptr_t a=reinterpret_cast<uintptr_t>(p);
    if(!p || !size || a+size<a || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT
        || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || a+size>reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize)return false;
    const DWORD access=m.Protect&0xff;
    return access==PAGE_READONLY || access==PAGE_READWRITE || access==PAGE_WRITECOPY
        || access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
inline bool Tables(const Profile& p,Crossplay::Edition edition) {
    const uint64_t expected=edition==Crossplay::Edition::Gold?0x873f19377b669596ull:
        edition==Crossplay::Edition::UbisoftHistory?0xbe86ae411023e65eull:0xe436bd65f15e14b4ull;
    uint64_t fingerprint=14695981039346656037ull;
    const uintptr_t addresses[]={p.stream_vt,p.writer_vt,p.reader_vt};const size_t counts[]={11,39,34};
    for(size_t table=0;table<3;++table) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(addresses[table]);
        if(!Readable(bytes,counts[table]*4))return false;
        for(size_t i=0;i<counts[table];++i) {
            uintptr_t method=0;memcpy(&method,bytes+4*i,4);
            if(method<0x400000 || method>=0x1500000 || !Readable(reinterpret_cast<void*>(method),8))return false;
            for(size_t j=0;j<4;++j)fingerprint=(fingerprint^bytes[4*i+j])*1099511628211ull;
            const auto* prefix=reinterpret_cast<const unsigned char*>(method);
            for(size_t j=0;j<8;++j)fingerprint=(fingerprint^prefix[j])*1099511628211ull;
        }
    }
    // Integrity guard for all invoked native visitor/stream slots. Executable
    // identity comes separately from the SHA-256 checked participant binding.
    return fingerprint==expected;
}
inline bool Methods(const Profile& p) {
    const unsigned char gold_ctor[]={0x33,0xc9,0xc7,0x00,0x60,0x65,0xf2,0x00};
    const unsigned char history_ctor[]={0x55,0x8b,0xec,0x6a,0xff};
    const unsigned char gold_write[]={0x55,0x8b,0xec,0x51,0x85,0xff};
    const unsigned char gold_read[]={0x55,0x8b,0xec,0x51,0x8b,0x07};
    const unsigned char history_write[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x57,0x8b,0xf9};
    for(auto a:{p.stream_ctor,p.stream_vt,p.writer_vt,p.reader_vt,p.write,p.read,p.event_vt})if(!Readable(reinterpret_cast<void*>(a),16))return false;
    return !(p.gold?memcmp(reinterpret_cast<void*>(p.stream_ctor),gold_ctor,sizeof(gold_ctor)):
            memcmp(reinterpret_cast<void*>(p.stream_ctor),history_ctor,sizeof(history_ctor)))
        && !(p.gold?memcmp(reinterpret_cast<void*>(p.write),gold_write,sizeof(gold_write)):
            memcmp(reinterpret_cast<void*>(p.write),history_write,sizeof(history_write)))
        && !(p.gold?memcmp(reinterpret_cast<void*>(p.read),gold_read,sizeof(gold_read)):
            memcmp(reinterpret_cast<void*>(p.read),history_ctor,sizeof(history_ctor)));
}
struct OwnedStream {
    Stream value{};bool initialized=false;
    explicit OwnedStream(const Profile& p) {
        if(p.gold)GoldCtor(p.stream_ctor,&value);
        else reinterpret_cast<void* (__thiscall*)(void*)>(p.stream_ctor)(&value);
        initialized=value.vt==p.stream_vt;
    }
    ~OwnedStream() {
        if(initialized)reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(value.vt))(&value,0);
    }
};
inline bool Encode(const Profile& p,const void* event,std::vector<unsigned char>& result) {
    result.clear();if(!Readable(event,0x10) || *static_cast<const uintptr_t*>(event)!=p.event_vt)return false;
    OwnedStream stream(p);if(!stream.initialized)return false;
    Visitor visitor{p.writer_vt,0,&stream.value,0};const char* name="WePlayEvent";
    if(p.gold)GoldWrite(p.write,&visitor,event,name);
    else reinterpret_cast<void (__thiscall*)(void*,const void*,const char*,uint32_t)>(p.write)(&visitor,event,name,0);
    const auto& s=stream.value;
    if(s.vt!=p.stream_vt || s.external || s.length<13 || s.length>65536 || s.capacity<s.length
        || s.position!=s.length || !Readable(s.bytes,s.length))return false;
    const auto* bytes=static_cast<const unsigned char*>(s.bytes);result.assign(bytes,bytes+s.length);return true;
}
inline Profile ChatPin(Crossplay::Edition edition) {
    auto p=Pin(edition);p.event_vt=edition==Crossplay::Edition::Gold?0xf4e834:
        edition==Crossplay::Edition::UbisoftHistory?0x1140a18:0x113ef5c;return p;
}
inline bool ChatMethods(Crossplay::Edition edition) {
    struct Entry {uintptr_t methods[6];unsigned char prefixes[6][8];};
    static const Entry pins[]={
        {{0x9a1d10,0xa482b0,0x4244d0,0xa48720,0xa482c0,0x9a1ca0},
         {{0x55,0x8b,0xec,0x56,0x8b,0xf1,0xe8,0xd5},{0xb8,0xa9,0xc6,0x9b,0x8f,0xc3,0xcc,0xcc},{0x33,0xc0,0xc2,0x08,0x00,0xcc,0xcc,0xcc},
          {0x55,0x8b,0xec,0x83,0xec,0x08,0x53,0x56},{0xb8,0x01,0x00,0x00,0x00,0xc3,0xcc,0xcc},{0x55,0x8b,0xec,0x6a,0xff,0x68,0x5b,0x90}}},
        {{0x69aa70,0xee4d10,0x4eeec0,0xee4aa0,0xee4d00,0x69a9f0},
         {{0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0x8b},{0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0xb8},{0x33,0xc0,0xc2,0x08,0x00,0xcc,0xcc,0xcc},
          {0x55,0x8b,0xec,0x83,0xec,0x08,0x89,0x4d},{0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0xb8},{0x55,0x8b,0xec,0x6a,0xff,0x68,0x3f,0x3b}}},
        {{0x69a650,0xee4400,0x4eebe0,0xee4190,0xee43f0,0x69a5d0},
         {{0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0x8b},{0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0xb8},{0x33,0xc0,0xc2,0x08,0x00,0xcc,0xcc,0xcc},
          {0x55,0x8b,0xec,0x83,0xec,0x08,0x89,0x4d},{0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0xb8},{0x55,0x8b,0xec,0x6a,0xff,0x68,0x0f,0x2c}}}
    };
    const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=ChatPin(edition);const auto& pin=pins[index];if(!Methods(p) || !Tables(p,edition) || !Readable(reinterpret_cast<void*>(p.event_vt),24))return false;
    for(unsigned i=0;i<6;++i)if(*reinterpret_cast<const uintptr_t*>(p.event_vt+4*i)!=pin.methods[i]
        || !Readable(reinterpret_cast<void*>(pin.methods[i]),8) || memcmp(reinterpret_cast<void*>(pin.methods[i]),pin.prefixes[i],8))return false;
    return true;
}
inline bool EncodeChatNative(Crossplay::Edition edition,const void* event,std::vector<unsigned char>& wire) {
    return ChatMethods(edition) && Encode(ChatPin(edition),event,wire);
}
inline void* Transport(Crossplay::Edition edition,uintptr_t& send) {
    const bool gold=edition==Crossplay::Edition::Gold,steam=edition==Crossplay::Edition::SteamHistory;
    const uintptr_t pointer=gold?0x10f83dc:steam?0x13a230c:0x13a5210;
    const uintptr_t main_vt=gold?0xf55600:steam?0x113d7ec:0x113f2c0;
    const uintptr_t transport_vt=gold?0xf566cc:steam?0x113ea18:0x11404f0;
    send=gold?0xa4e430:steam?0xeda9f0:0xedb300;
    const unsigned char gp[]={0x55,0x8b,0xec,0x8b,0x45,0x0c,0x83,0xe8,0x00,0x56};
    const unsigned char hp[]={0x55,0x8b,0xec,0x83,0xec,0x18,0x89,0x4d,0xfc,0x8b};
    if(!Readable(reinterpret_cast<void*>(send),10) || memcmp(reinterpret_cast<void*>(send),gold?gp:hp,10)
        || !Readable(reinterpret_cast<void*>(pointer),4) || !Readable(reinterpret_cast<void*>(transport_vt),24)
        || *reinterpret_cast<const uintptr_t*>(transport_vt+20)!=send)return nullptr;
    const auto* main=*reinterpret_cast<const unsigned char* const*>(pointer);
    if(!Readable(main,0x18) || *reinterpret_cast<const uintptr_t*>(main)!=main_vt)return nullptr;
    auto* transport=*reinterpret_cast<unsigned char* const*>(main+0x10);
    if(!Readable(transport,gold?0x5c:0x40) || *reinterpret_cast<const uintptr_t*>(transport)!=transport_vt
        || !Readable(*reinterpret_cast<const void* const*>(transport+(gold?0x18:0x14)),sizeof(CRITICAL_SECTION)))return nullptr;
    return transport;
}
inline bool DecodeChatNative(Crossplay::Edition edition,const std::vector<unsigned char>& wire) {
    // Caller must first validate the complete chat wire and the current native
    // roster/peer interval. Never pass an arbitrary factory class to this path.
    if(wire.size()<13 || !CrossplayChat::Validate(wire,CrossplayChat::Word(wire.data()+8)) || !ChatMethods(edition))return false;
    const auto p=ChatPin(edition);OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());
    stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=p.gold?GoldRead(p.read,&reader,name):
        reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Readable(decoded,0x38) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Encode(p,decoded,repeated) && wire==repeated;
    uintptr_t send=0;void* transport=okay?Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    // Mode zero clones through the receiver's original allocator. Destroy only
    // our factory-owned original; the native dispatcher owns the clone.
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
}
// A bound ParticipantEvent supplies the independently fingerprinted process and
// native-session guard. The caller must also validate its admission interval.
inline bool Roster(const CrossplayNative::ParticipantEvent& binding,uint32_t own,uint32_t remote) {
    CrossplayNative::LobbySnapshot snapshot;
    if(own==remote || !binding.CaptureLobby(snapshot) || snapshot.members.size()!=2)return false;
    unsigned local=0,peer=0;
    for(const auto& member:snapshot.members) {
        if(member.status!=2)return false;
        local+=member.player.local && member.player.connection==own?1u:0u;
        peer+=!member.player.local && member.player.connection==remote?1u:0u;
    }
    return local==1 && peer==1;
}
inline bool EncodeChat(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                       uint32_t own,uint32_t remote,const void* event,std::vector<unsigned char>& wire) {
    wire.clear();
    if(!Roster(binding,own,remote) || !Detail::EncodeChatNative(edition,event,wire))return false;
    if(CrossplayChat::Validate(wire,own))return true;
    wire.clear();return false;
}
inline bool EnqueueChat(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                        uint32_t own,uint32_t remote,const std::vector<unsigned char>& wire) {
    return Roster(binding,own,remote) && CrossplayChat::Validate(wire,remote)
        && Detail::DecodeChatNative(edition,wire);
}
}
