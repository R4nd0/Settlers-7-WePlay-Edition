#pragma once
#include "crossplay_session_config_codec.h"
#include "crossplay_load_progress_codec.h"
#include "crossplay_resume_transfer.h"

// Opt-in lobby primitive. Callers separately admit the saved checkpoint and
// observe the original native file consumer before permitting a resumed start.
namespace CrossplayNativeCodec::ResumeFileDetail {
inline Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=Detail::Pin(edition);
    p.event_vt=edition==Crossplay::Edition::Gold?0xf4e1e8:
        edition==Crossplay::Edition::UbisoftHistory?0x114de08:0x114c3ac;
    return p;
}
inline bool Methods(Crossplay::Edition edition) {
    const auto index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=Pin(edition);
    if(!Detail::Methods(p) || !Detail::Tables(p,edition) || !Detail::Readable(reinterpret_cast<void*>(p.event_vt),24))return false;
    uint64_t hash=14695981039346656037ull;uintptr_t visitor=0;
    for(unsigned i=0;i<6;++i){
        const auto* bytes=reinterpret_cast<const unsigned char*>(p.event_vt+4*i);
        uintptr_t address=0;memcpy(&address,bytes,4);
        if(address<0x400000 || address>=0x1500000 || !Detail::Readable(reinterpret_cast<void*>(address),8))return false;
        for(unsigned j=0;j<4;++j)hash=(hash^bytes[j])*1099511628211ull;
        const auto* code=reinterpret_cast<const unsigned char*>(address);
        for(unsigned j=0;j<8;++j)hash=(hash^code[j])*1099511628211ull;
        if(i==3)visitor=address;
    }
    const unsigned sizes[]={315,252,252};
    if(!Detail::Readable(reinterpret_cast<void*>(visitor),sizes[index]))return false;
    const auto* bytes=reinterpret_cast<const unsigned char*>(visitor);
    for(unsigned i=0;i<sizes[index];++i)hash=(hash^bytes[i])*1099511628211ull;
    const uint64_t expected[]={0x13fffa44becc085dull,0x4c342f27249c006cull,0xf008f4c59945847dull};
    return hash==expected[index];
}
inline bool OwnershipMethods(Crossplay::Edition edition,uintptr_t send) {
    const auto index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    // Qualified direction is Gold host to either original History receiver.
    // Gold's local path has a separate observer bridge and is not admitted here.
    if(edition==Crossplay::Edition::Gold)return false;
    const auto p=Pin(edition);
    const uintptr_t destructors[]={0x99c100,0x849f30,0x849d00};
    const uintptr_t clones[]={0x99c0a0,0x849ee0,0x849cb0};
    const uintptr_t transports[]={0xa4e430,0xedb300,0xeda9f0};
    const size_t destructor_bytes[]={58,60,60},clone_bytes[]={86,69,69};
    const uint64_t expected[]={0x9d9951428faf5c28ull,0x066e50c6bc2a2961ull,0xd979657238df568bull};
    const uint64_t transport_expected[]={0,0xbefb0fce84f487c2ull,0x17584edba0862dc4ull};
    if(send!=transports[index] || !Detail::Readable(reinterpret_cast<void*>(p.event_vt),24)
        || *reinterpret_cast<const uintptr_t*>(p.event_vt)!=destructors[index]
        || *reinterpret_cast<const uintptr_t*>(p.event_vt+20)!=clones[index])return false;
    uint64_t hash=14695981039346656037ull;
    for(unsigned which=0;which<2;++which){
        const uintptr_t at=which?clones[index]:destructors[index];
        const auto length=which?clone_bytes[index]:destructor_bytes[index];
        if(!Detail::Readable(reinterpret_cast<void*>(at),length))return false;
        for(size_t i=0;i<length;++i)hash=(hash^reinterpret_cast<const unsigned char*>(at)[i])*1099511628211ull;
    }
    if(hash!=expected[index] || !Detail::Readable(reinterpret_cast<void*>(send),265))return false;
    hash=14695981039346656037ull;
    // Exact original mode-zero routing, both clone/queue alternatives, locking,
    // and return. The mode-one/two branches have separately guarded outgoing
    // observers; neither is executed by this local-only ownership transfer.
    const size_t offsets[]={0,0x57,0x103},lengths[]={0x2a,0x67,6};
    for(unsigned region=0;region<3;++region)
        for(size_t i=0;i<lengths[region];++i)hash=(hash^reinterpret_cast<const unsigned char*>(send)[offsets[region]+i])*1099511628211ull;
    return hash==transport_expected[index];
}
inline bool Owner(const CrossplayNative::ParticipantEvent& binding,DWORD owner,uintptr_t session,
                  CrossplaySessionConfig::Connections ids,bool host) {
    CrossplayNative::LobbySnapshot current;
    return owner && owner==GetCurrentThreadId() && session
        && SessionConfigDetail::Roster(binding,ids,host) && binding.CaptureLobby(current)
        && current.instance==session && current.host==host;
}
inline bool Source(const Detail::Profile& p,const void* event,uint32_t source,uint32_t total) {
    const auto* data=static_cast<const unsigned char*>(event);
    if(source==UINT32_MAX || !Detail::Readable(data,0x20) || *reinterpret_cast<const uintptr_t*>(data)!=p.event_vt
        || CrossplayChat::Word(data+4)!=CrossplayResumeTransfer::Type || CrossplayChat::Word(data+8)!=source
        || data[12]!=1 || total<80 || total>CrossplayResumeTransfer::MaximumFileBytes)return false;
    const auto length=CrossplayChat::Word(data+0x18);
    return length<=CrossplayResumeTransfer::MaximumFragmentBytes && length<=total
        && CrossplayChat::Word(data+0x1c)==total
        && (!length || Detail::Readable(*reinterpret_cast<const void* const*>(data+0x10),length));
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                   uintptr_t session,CrossplaySessionConfig::Connections ids,uint32_t total,
                   const void* event,std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;wire.clear();
    if(!Owner(binding,owner,session,ids,true) || !Methods(edition))return false;
    const auto p=Pin(edition);
    if(Source(p,event,ids.host,total) && Detail::Encode(p,event,wire)
        && CrossplayResumeTransfer::Inspect(wire,ids.host,total))return true;
    wire.clear();return false;
}
template<class FileContext>
inline bool EnqueueBound(Crossplay::Edition edition,uint32_t host,uint32_t total,
                         const std::vector<unsigned char>& wire,FileContext context) {
    LoadProgressDetail::FpScope preserve;
    if(!context() || !Methods(edition) || !CrossplayResumeTransfer::Inspect(wire,host,total))return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());
    stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=p.gold?Detail::GoldRead(p.read,&reader,name):
        reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Detail::Readable(decoded,0x20) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(p,decoded,host,total)
        && Detail::Encode(p,decoded,repeated) && repeated==wire && context();
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport && OwnershipMethods(edition,send)){
        reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
        // Unlike native strings, this event's original clone shares the raw
        // payload pointer. The verified mode-zero path queues exactly one clone.
        // Transfer that allocation to the queue before destroying our factory
        // object; otherwise both original destructors free the same buffer.
        *reinterpret_cast<void**>(static_cast<unsigned char*>(decoded)+0x10)=nullptr;
    }else okay=false;
    // Destroy our event object only. Its payload now belongs to the native queue.
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
inline bool Enqueue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                    uintptr_t session,CrossplaySessionConfig::Connections ids,uint32_t total,
                    const std::vector<unsigned char>& wire) {
    return EnqueueBound(edition,ids.host,total,wire,[&]{return Owner(binding,owner,session,ids,false);});
}
}
