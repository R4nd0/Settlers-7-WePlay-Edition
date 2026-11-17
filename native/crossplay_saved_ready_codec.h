#pragma once
#include "crossplay_saved_ready.h"
#include "crossplay_saved_session_config_codec.h"

namespace CrossplayNativeCodec::SavedReadyDetail {
inline bool GoldSlot(const CrossplayNative::ParticipantEvent& original,DWORD owner,uintptr_t session,
                     CrossplaySessionConfig::Connections ids,const std::string& map,const std::string& token,
                     unsigned char*& cache,unsigned char*& guest){
    cache=guest=nullptr;
    if(!ResumeFileDetail::Owner(original,owner,session,ids,true) || !ReadyDetail::Methods(Crossplay::Edition::Gold)
        || !ReadyDetail::GoldStateMethods())return false;
    const auto* object=reinterpret_cast<const unsigned char*>(session);
    if(!Detail::Readable(object,0x34) || CrossplayChat::Word(object)!=0xf569bc || CrossplayChat::Word(object+0x30)!=2)return false;
    cache=SessionConfigDetail::GoldConfiguration();if(!cache)return false;
    const auto p=SessionConfigDetail::Pin(Crossplay::Edition::Gold);
    if(!SavedSessionConfigDetail::Fields(p,cache+0x24,cache+0x34,cache+0x44,ids,map,token))return false;
    guest=*reinterpret_cast<unsigned char**>(cache+0x48)+0x18;return true;
}
inline bool Enqueue(const CrossplayNative::ParticipantEvent& original,DWORD owner,uintptr_t session,
                    CrossplaySessionConfig::Connections ids,const std::string& map,const std::string& token,
                    const std::vector<unsigned char>& wire,PendingReadyToggle& pending){
    LoadProgressDetail::FpScope fp;pending={};
    if(!CrossplayReady::Validate(wire,ids.guest) || !ResumeFileDetail::Owner(original,owner,session,ids,true))return false;
    auto* cache=SessionConfigDetail::GoldConfiguration();if(!cache)return false;
    SessionConfigDetail::ConfigLock lock(cache);if(!lock.acquired)return false;
    unsigned char* current=nullptr;unsigned char* guest=nullptr;
    if(!GoldSlot(original,owner,session,ids,map,token,current,guest) || current!=cache)return false;
    const auto before=guest[4],after=static_cast<unsigned char>(!before);
    std::vector<unsigned char> state=wire;state.resize(CrossplayReady::NativeStateSize);
    CrossplayChat::Put(state.data()+13,2);state[17]=after;
    if(!ReadyDetail::EnqueueState(Crossplay::Edition::Gold,state,ids.guest))return false;
    pending={reinterpret_cast<uintptr_t>(cache),session,ids,before,after};return true;
}
inline ReadyObservation Observe(const CrossplayNative::ParticipantEvent& original,DWORD owner,
                                const std::string& map,const std::string& token,const PendingReadyToggle& pending,
                                uint32_t revision,CrossplayReadyState::Snapshot& state){
    LoadProgressDetail::FpScope fp;state={};if(!pending.configuration || !revision)return ReadyObservation::Invalid;
    if(!ResumeFileDetail::Owner(original,owner,pending.session,pending.ids,true))return ReadyObservation::Invalid;
    auto* cache=SessionConfigDetail::GoldConfiguration();if(reinterpret_cast<uintptr_t>(cache)!=pending.configuration)return ReadyObservation::Invalid;
    SessionConfigDetail::ConfigLock lock(cache);if(!lock.acquired)return ReadyObservation::Pending;
    unsigned char* current=nullptr;unsigned char* guest=nullptr;
    if(!GoldSlot(original,owner,pending.session,pending.ids,map,token,current,guest) || current!=cache)return ReadyObservation::Invalid;
    if(guest[4]==pending.before)return ReadyObservation::Pending;
    if(guest[4]!=pending.after)return ReadyObservation::Invalid;
    CrossplayReadyState::Snapshot observed{revision,(guest-0x18)[4],guest[4]};
    if(!CrossplayReadyState::Valid(observed))return ReadyObservation::Invalid;
    state=observed;return ReadyObservation::Applied;
}
}
