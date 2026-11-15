#pragma once
#include "crossplay_resume_file_codec.h"
#include "crossplay_saved_session_config.h"
#include "crossplay_ready_state_codec.h"

namespace CrossplayNativeCodec::SavedSessionConfigDetail {
inline bool Fields(const Detail::Profile& p,const unsigned char* map_field,const unsigned char* save_field,
                   const unsigned char* container,CrossplaySessionConfig::Connections ids,
                   const std::string& map,const std::string& save) {
    if(!CrossplaySessionConfig::Distinct(ids) || !CrossplaySessionConfig::RelativeMap(map)
        || !CrossplayResumeStart::SlotName(save) || !Detail::Readable(map_field,16)
        || !Detail::Readable(save_field,16) || !Detail::Readable(container,p.gold?16:12))return false;
    std::string actual_map,actual_save;
    if(!SessionConfigDetail::NativeString(map_field,actual_map) || actual_map!=map
        || !SessionConfigDetail::NativeString(save_field,actual_save) || actual_save!=save)return false;
    const auto* vector=reinterpret_cast<const uintptr_t*>(container+(p.gold?4:0));
    constexpr size_t bytes=CrossplaySessionConfig::Slots*0x18;
    if(vector[1]<vector[0] || vector[1]-vector[0]!=bytes || vector[2]<vector[1]
        || vector[2]-vector[0]>2*bytes || !Detail::Readable(reinterpret_cast<void*>(vector[0]),bytes))return false;
    for(size_t i=0;i<CrossplaySessionConfig::Slots;++i) {
        const auto* row=reinterpret_cast<const unsigned char*>(vector[0]+i*0x18);
        if(!CrossplaySessionConfig::SlotValid(i,row[4],row[0xc],CrossplayChat::Word(row+8),
            CrossplayChat::Word(row+0x10),CrossplayChat::Word(row),ids,true))return false;
    }
    return true;
}
inline bool Source(const Detail::Profile& p,const void* event,CrossplaySessionConfig::Connections ids,
                   const std::string& map,const std::string& save) {
    const auto* data=static_cast<const unsigned char*>(event);
    return Detail::Readable(data,p.gold?0x40:0x3c) && *reinterpret_cast<const uintptr_t*>(data)==p.event_vt
        && CrossplayChat::Word(data+4)==CrossplaySessionConfig::Type && CrossplayChat::Word(data+8)==ids.host && data[12]==1
        && Fields(p,data+0x10,data+0x20,data+0x30,ids,map,save);
}
inline bool CaptureGold(const CrossplayNative::ParticipantEvent& binding,DWORD owner,uintptr_t session,
                        CrossplaySessionConfig::Connections ids,const std::string& map,const std::string& save,
                        std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;wire.clear();
    if(!ResumeFileDetail::Owner(binding,owner,session,ids,true)
        || !SessionConfigDetail::Methods(Crossplay::Edition::Gold))return false;
    auto* cache=SessionConfigDetail::GoldConfiguration();if(!cache)return false;
    SessionConfigDetail::ConfigLock lock(cache);if(!lock.acquired)return false;
    const auto p=SessionConfigDetail::Pin(Crossplay::Edition::Gold);
    if(!Fields(p,cache+0x24,cache+0x34,cache+0x44,ids,map,save))return false;
    SessionConfigDetail::OwnedGoldConfig event;
    void* result=SessionConfigDetail::GoldConstruct(0x98d0a0,cache+0x24,event.bytes,ids.host,cache+0x34,cache+0x44);
    event.initialized=*reinterpret_cast<const uintptr_t*>(event.bytes)==p.event_vt;
    if(!event.initialized || result!=event.bytes || !Source(p,event.bytes,ids,map,save)
        || *reinterpret_cast<const uintptr_t*>(event.bytes+0x10)==*reinterpret_cast<const uintptr_t*>(cache+0x24)
        || *reinterpret_cast<const uintptr_t*>(event.bytes+0x20)==*reinterpret_cast<const uintptr_t*>(cache+0x34)
        || *reinterpret_cast<const uintptr_t*>(event.bytes+0x34)==*reinterpret_cast<const uintptr_t*>(cache+0x48)
        || SessionConfigDetail::GoldConfiguration()!=cache || !ResumeFileDetail::Owner(binding,owner,session,ids,true))return false;
    std::vector<unsigned char> snapshot;
    return Detail::Encode(p,event.bytes,snapshot) && CrossplaySavedSessionConfig::BindSource(snapshot,ids,map,save,wire);
}
inline bool Enqueue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                    uintptr_t session,CrossplaySessionConfig::Connections ids,const std::string& map,const std::string& save,
                    const std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;CrossplaySessionConfig::Layout layout;
    if(edition==Crossplay::Edition::Gold || !ResumeFileDetail::Owner(binding,owner,session,ids,false)
        || !SessionConfigDetail::Methods(edition) || !CrossplaySavedSessionConfig::Inspect(wire,ids,map,save,layout))return false;
    const auto p=SessionConfigDetail::Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};
    void* decoded=reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,"WePlayEvent",0);
    if(!decoded || !Detail::Readable(decoded,0x3c) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(p,decoded,ids,map,save)
        && Detail::Encode(p,decoded,repeated) && repeated==wire && ResumeFileDetail::Owner(binding,owner,session,ids,false);
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
inline bool PendingFileMetadata(const unsigned char* pending) {
    // Original constructors copy three native strings and a scalar. This is
    // file metadata, not the saved setup event or its vector of player slots.
    if(!Detail::Readable(pending,0x34))return false;
    std::string value;
    return SessionConfigDetail::NativeString(pending,value)
        && SessionConfigDetail::NativeString(pending+0x10,value)
        && SessionConfigDetail::NativeString(pending+0x20,value);
}
}
