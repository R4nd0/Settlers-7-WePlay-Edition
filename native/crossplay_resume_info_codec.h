#pragma once
#include "crossplay_resume_file_codec.h"
#include "crossplay_resume_info.h"

namespace CrossplayNativeCodec::ResumeInfoDetail {
inline Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=Detail::Pin(edition);p.event_vt=edition==Crossplay::Edition::Gold?0xf4e0dc:
        edition==Crossplay::Edition::UbisoftHistory?0x114dd28:0x114c254;return p;
}
inline bool Methods(Crossplay::Edition edition) {
    const auto index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=Pin(edition);
    if(!Detail::Methods(p) || !Detail::Tables(p,edition) || !Detail::Readable(reinterpret_cast<void*>(p.event_vt),24))return false;
    uint64_t hash=14695981039346656037ull;uintptr_t visitor=0;
    for(unsigned i=0;i<6;++i){
        const auto* bytes=reinterpret_cast<const unsigned char*>(p.event_vt+4*i);uintptr_t address=0;memcpy(&address,bytes,4);
        if(address<0x400000 || address>=0x1500000 || !Detail::Readable(reinterpret_cast<void*>(address),8))return false;
        for(unsigned j=0;j<4;++j)hash=(hash^bytes[j])*1099511628211ull;
        const auto* code=reinterpret_cast<const unsigned char*>(address);
        for(unsigned j=0;j<8;++j)hash=(hash^code[j])*1099511628211ull;
        if(i==3)visitor=address;
    }
    const unsigned sizes[]={168,60,60};
    const uint64_t expected[]={0x822909ca6794b515ull,0xae1ffec7dae21154ull,0x3e64104b6e10e5b4ull};
    if(!Detail::Readable(reinterpret_cast<void*>(visitor),sizes[index]))return false;
    const auto* code=reinterpret_cast<const unsigned char*>(visitor);
    for(unsigned i=0;i<sizes[index];++i)hash=(hash^code[i])*1099511628211ull;
    return hash==expected[index];
}
inline bool Source(const Detail::Profile& p,const void* event,uint32_t source) {
    const auto* data=static_cast<const unsigned char*>(event);std::string text;
    return source!=UINT32_MAX && Detail::Readable(data,0x44) && *reinterpret_cast<const uintptr_t*>(data)==p.event_vt
        && CrossplayChat::Word(data+4)==CrossplayResumeInfo::Type && CrossplayChat::Word(data+8)==source && data[12]==1
        && SessionConfigDetail::NativeString(data+0x20,text);
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                   uintptr_t session,CrossplaySessionConfig::Connections ids,const void* event,std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;wire.clear();
    if(!ResumeFileDetail::Owner(binding,owner,session,ids,true) || !Methods(edition))return false;
    const auto p=Pin(edition);
    if(Source(p,event,ids.host) && Detail::Encode(p,event,wire) && CrossplayResumeInfo::Inspect(wire,ids.host))return true;
    wire.clear();return false;
}
template<class FileContext>
inline bool EnqueueBound(Crossplay::Edition edition,uint32_t host,const std::vector<unsigned char>& wire,FileContext context) {
    LoadProgressDetail::FpScope preserve;
    if(edition==Crossplay::Edition::Gold || !context()
        || !Methods(edition) || !CrossplayResumeInfo::Inspect(wire,host))return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};
    void* decoded=reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,"WePlayEvent",0);
    if(!decoded || !Detail::Readable(decoded,0x44) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(p,decoded,host) && Detail::Encode(p,decoded,repeated)
        && repeated==wire && context();
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
inline bool Enqueue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                    uintptr_t session,CrossplaySessionConfig::Connections ids,const std::vector<unsigned char>& wire) {
    return EnqueueBound(edition,ids.host,wire,[&]{return ResumeFileDetail::Owner(binding,owner,session,ids,false);});
}
}
