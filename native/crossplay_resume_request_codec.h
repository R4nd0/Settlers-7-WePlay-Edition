#pragma once
#include "crossplay_saved_session_config_codec.h"
#include "crossplay_resume_request.h"

namespace CrossplayNativeCodec::ResumeRequestDetail {
inline Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=Detail::Pin(edition);p.event_vt=edition==Crossplay::Edition::Gold?0xf39b88:
        edition==Crossplay::Edition::UbisoftHistory?0x113f32c:0x113d84c;return p;
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
    const unsigned lengths[]={168,337,337};const uint64_t expected[]={0x6b7c8139d72e4862ull,0x88d6334843c6045eull,0x3c52faffbfd07a09ull};
    if(!Detail::Readable(reinterpret_cast<void*>(visitor),lengths[index]))return false;
    const auto* code=reinterpret_cast<const unsigned char*>(visitor);
    for(unsigned i=0;i<lengths[index];++i)hash=(hash^code[i])*1099511628211ull;
    return hash==expected[index];
}
inline bool Source(const Detail::Profile& p,const void* event,uint32_t guest) {
    const auto* data=static_cast<const unsigned char*>(event);
    return guest!=UINT32_MAX && Detail::Readable(data,16) && *reinterpret_cast<const uintptr_t*>(data)==p.event_vt
        && CrossplayChat::Word(data+4)==CrossplayResumeRequest::Type && CrossplayChat::Word(data+8)==guest && data[12]==1;
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                   uintptr_t session,CrossplaySessionConfig::Connections ids,const void* event,std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;wire.clear();
    if(edition==Crossplay::Edition::Gold || !ResumeFileDetail::Owner(binding,owner,session,ids,false) || !Methods(edition))return false;
    const auto p=Pin(edition);
    if(Source(p,event,ids.guest) && Detail::Encode(p,event,wire) && CrossplayResumeRequest::Inspect(wire,ids.guest))return true;
    wire.clear();return false;
}
inline bool HostFileContext(const CrossplayNative::ParticipantEvent& binding,DWORD owner,uintptr_t session,
                            CrossplaySessionConfig::Connections ids,const std::string& map,const std::string& save) {
    if(!ResumeFileDetail::Owner(binding,owner,session,ids,true))return false;
    auto* cache=SessionConfigDetail::GoldConfiguration();const auto p=SessionConfigDetail::Pin(Crossplay::Edition::Gold);
    if(!cache || !SavedSessionConfigDetail::Fields(p,cache+0x24,cache+0x34,cache+0x44,ids,map,save))return false;
    const auto* pending=*reinterpret_cast<const unsigned char* const*>(cache+0x18);
    const auto* stream=*reinterpret_cast<const unsigned char* const*>(cache+0x1c);
    if(!SavedSessionConfigDetail::PendingFileMetadata(pending) || !Detail::Readable(stream,8))return false;
    return Detail::Readable(*reinterpret_cast<const void* const*>(stream),0x24);
}
template<class FileContext>
inline bool EnqueueGoldBound(uint32_t guest,const std::vector<unsigned char>& wire,FileContext context) {
    LoadProgressDetail::FpScope preserve;
    if(!context() || !Methods(Crossplay::Edition::Gold) || !CrossplayResumeRequest::Inspect(wire,guest))return false;
    const auto p=Pin(Crossplay::Edition::Gold);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};void* decoded=Detail::GoldRead(p.read,&reader,"WePlayEvent");
    if(!decoded || !Detail::Readable(decoded,16) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(p,decoded,guest) && Detail::Encode(p,decoded,repeated)
        && repeated==wire && context();
    uintptr_t send=0;void* transport=okay?Detail::Transport(Crossplay::Edition::Gold,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
inline bool EnqueueGold(const CrossplayNative::ParticipantEvent& binding,DWORD owner,uintptr_t session,
                        CrossplaySessionConfig::Connections ids,const std::string& map,const std::string& save,
                        const std::vector<unsigned char>& wire) {
    return EnqueueGoldBound(ids.guest,wire,[&]{return HostFileContext(binding,owner,session,ids,map,save);});
}
}
