#pragma once
#include "crossplay_session_config_codec.h"
#include "crossplay_load_progress_codec.h"
#include "crossplay_history_role.h"
#include "crossplay_bootstrap.h"

namespace CrossplayGoldChoices {
enum class Observation {Pending,Invalid,Captured};
inline bool Methods(){
    const struct Region {uintptr_t address;size_t size;uint64_t hash;} regions[]={
        {0x98f980,0x3e2,0x23781093dae7e6bdull},
        {0xa53980,0x193,0xdb12912829993e78ull},
        {0x415e70,0x2c,0xa65593964bf1671eull}};
    for(const auto& region:regions){
        const auto* bytes=reinterpret_cast<const unsigned char*>(region.address);
        if(!CrossplayNativeCodec::Detail::Readable(bytes,region.size))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.size;++i)hash=(hash^bytes[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    return true;
}
inline bool Flags(unsigned positions,unsigned teams,unsigned coop,uint32_t& result){
    result=0;if(positions>1 || teams>1 || coop>1)return false;
    result=positions|(teams<<1)|(coop<<2);return true;
}
struct NativeString {char* data;uint32_t capacity,length,extra;};
inline bool Storage(const NativeString& native){
    if(native.length>=65536 || native.capacity>65536)return false;
    if(!native.data)return !native.length && !native.capacity;
    if(native.capacity<=native.length || !CrossplayNative::HistoryRoleDetail::Writable(native.data,native.length+1)
        || native.data[native.length])return false;
    return true;
}
inline bool Text(const NativeString& native,std::string& result){
    result.clear();
    if(!Storage(native) || native.length>CrossplayBootstrap::MaximumTemplate)return false;
    if(!native.data)return true;
    result.assign(native.data,native.length);return CrossplayBootstrap::Template(result);
}
using Getter=void* (__stdcall*)(NativeString*);
using Destroy=void (__thiscall*)(NativeString*);
// Separately authored ABI fixtures may supply these two typed original-method
// boundaries. The production call uses only the pinned original addresses.
inline bool ReadTemplateWith(Getter getter,Destroy destroy,std::string& result){
    const DWORD error=GetLastError();struct Restore {DWORD value;~Restore(){SetLastError(value);}} restore{error};
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    NativeString native{};result.clear();
    if(!getter || !destroy || getter(&native)!=&native)return false;
    // Validate ownership shape before calling a destructor on returned storage.
    // A rejected string is never passed to a potentially unsafe native free.
    if(!Storage(native))return false;
    struct Owned {NativeString* text;Destroy destroy;~Owned(){destroy(text);}} owned{&native,destroy};
    std::string text;if(!Text(native,text))return false;
    result=std::move(text);return true;
}
inline bool ReadTemplate(std::string& result){
    return ReadTemplateWith(reinterpret_cast<Getter>(0xa53980),reinterpret_cast<Destroy>(0x415e70),result);
}
struct Cache {
    uintptr_t address=0;std::string map,save;uint32_t flags=0;
};
inline Observation Snapshot(Cache& result){
    using namespace CrossplayNativeCodec::SessionConfigDetail;result={};
    auto* cache=GoldConfiguration();if(!cache)return Observation::Pending;
    ConfigLock lock(cache);
    if(!lock.section)return Observation::Invalid;
    if(!lock.acquired)return Observation::Pending;
    Cache next;next.address=reinterpret_cast<uintptr_t>(cache);
    if(!CrossplayNativeCodec::SessionConfigDetail::NativeString(cache+0x24,next.map)
        || !CrossplayNativeCodec::SessionConfigDetail::NativeString(cache+0x34,next.save)
        || !Flags(cache[0x54],cache[0x55],cache[0x56],next.flags))return Observation::Invalid;
    if(next.map.empty())return Observation::Pending;
    if(!CrossplaySessionConfig::RelativeMap(next.map) || !CrossplayBootstrap::Printable(next.map))return Observation::Invalid;
    result=std::move(next);return Observation::Captured;
}
inline bool Same(const Cache& a,const Cache& b){
    return a.address && a.address==b.address && a.map==b.map && a.save==b.save && a.flags==b.flags;
}
inline bool Local(const CrossplayNative::LobbySnapshot& snapshot,uintptr_t session,const CrossplayNative::Participant& expected){
    if(!session || snapshot.instance!=session || !snapshot.host || snapshot.local_slot!=1 || snapshot.members.size()!=1)return false;
    const auto& member=snapshot.members[0];const auto& actual=member.player;
    return member.status==2 && actual.local && actual.host && actual.slot==1
        && actual.connection==expected.connection && actual.principal==expected.principal
        && actual.station==expected.station && actual.name==expected.name;
}
inline Observation Capture(const CrossplayNative::ParticipantEvent& binding,DWORD owner,uintptr_t session,
                           const CrossplayNative::Participant& expected,CrossplayBootstrap::Context& result){
    const DWORD error=GetLastError();struct Restore {DWORD value;~Restore(){SetLastError(value);}} restore{error};
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;result={};
    CrossplayNative::LobbySnapshot before,after;
    if(!owner || owner!=GetCurrentThreadId() || !session || !Methods()
        || !CrossplayNativeCodec::SessionConfigDetail::Methods(Crossplay::Edition::Gold)
        || !binding.CaptureLobby(before) || !Local(before,session,expected))
        return Observation::Invalid;
    Cache first,second;auto observed=Snapshot(first);if(observed!=Observation::Captured)return observed;
    std::string first_template,second_template;
    if(!ReadTemplate(first_template) || !ReadTemplate(second_template))return Observation::Invalid;
    observed=Snapshot(second);if(observed!=Observation::Captured)return observed;
    if(!binding.CaptureLobby(after) || !Local(after,session,expected))return Observation::Invalid;
    if(!Same(first,second) || first_template!=second_template)return Observation::Pending;
    CrossplayBootstrap::Context captured{first.map,!first.save.empty(),first.flags,std::move(first_template),true};
    if(!CrossplayBootstrap::Valid(captured))return Observation::Invalid;
    result=std::move(captured);return Observation::Captured;
}
// Live two-human host interval, separate from the one-human bootstrap capture.
// Native readiness remains untouched; callers must also check their admission.
inline Observation CaptureConfigured(const CrossplayNative::ParticipantEvent& binding,DWORD owner,uintptr_t session,
                                     CrossplaySessionConfig::Connections ids,CrossplayBootstrap::Context& result){
    const DWORD error=GetLastError();struct Restore {DWORD value;~Restore(){SetLastError(value);}} restore{error};
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;result={};
    CrossplayNative::LobbySnapshot before,after;
    if(!owner || owner!=GetCurrentThreadId() || !session || !Methods()
        || !CrossplayNativeCodec::SessionConfigDetail::Methods(Crossplay::Edition::Gold)
        || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,true)
        || !binding.CaptureLobby(before) || before.instance!=session)return Observation::Invalid;
    Cache first,second;auto observed=Snapshot(first);if(observed!=Observation::Captured)return observed;
    std::string first_template,second_template;
    if(!ReadTemplate(first_template) || !ReadTemplate(second_template))return Observation::Invalid;
    observed=Snapshot(second);if(observed!=Observation::Captured)return observed;
    if(!binding.CaptureLobby(after) || after.instance!=session
        || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,true))return Observation::Invalid;
    if(!Same(first,second) || first_template!=second_template)return Observation::Pending;
    CrossplayBootstrap::Context captured{first.map,!first.save.empty(),first.flags,std::move(first_template),true};
    if(!CrossplayBootstrap::Valid(captured))return Observation::Invalid;
    result=std::move(captured);return Observation::Captured;
}
}
