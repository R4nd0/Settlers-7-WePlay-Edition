#pragma once
#include "crossplay_production_launch.h"
#include "crossplay_gold_host_choices.h"
#include "crossplay_saved_session_config_codec.h"
#include "crossplay_resume_source.h"
#include "crossplay_saved_ai.h"

// Host source for a NEW saved-session ownership interval. Selection is read
// from the original cache and the actual isolated profile, not a test manifest.
// This captures no native Ready/start permission and enqueues nothing.
namespace CrossplaySelectedSavedSource {
struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}};
struct Value {
    DWORD owner=0;uintptr_t session=0;
    CrossplaySessionConfig::Connections ids{};
    CrossplayGoldChoices::Cache cache;
    std::string map_template;
    CrossplayResumeSource::Selected selected;
    std::vector<unsigned char> configuration;
    unsigned ai_mask=0;
    std::array<CrossplaySavedAI::Row,2> ai{};
};
inline bool Path(const std::wstring& profile,const std::string& token,std::wstring& output){
    output.clear();
    if(!CrossplayProduction::AbsoluteProfile(profile) || !CrossplayResumeStart::SlotName(token))return false;
    output=profile+L"\\Savegames\\"+std::wstring(token.begin(),token.end())+L".s7s";return true;
}
// UI handoff only, before the original ChangeSavegame. It validates the real
// owned file and restricted metadata, but supplies no lobby/file/start receipt.
inline bool SelectFile(const CrossplayProduction::Launch& launch,const std::string& argument,
                       CrossplayResumeSource::ArchiveIdentity& identity,std::string& token){
    ErrorScope error;identity={};token.clear();
    if(!launch.mixed || !launch.host || (launch.channel.edition!=Crossplay::Edition::Gold && !launch.IsRoom())
        || argument.empty() || argument.size()>32767 || argument.find('\0')!=std::string::npos)return false;
    std::wstring expected;std::string source_token;
    if(CrossplayResumeStart::SlotName(argument)){
        // Original Gold reports a native slot token before ChangeSavegame.
        // Resolve it against the verified profile, never a process CWD.
        source_token=argument;if(!Path(launch.profile,source_token,expected))return false;
    }else{
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argument.data(),static_cast<int>(argument.size()),nullptr,0);
        if(length<=0 || length>32767)return false;
        std::wstring selected(static_cast<size_t>(length),L'\0');
        if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argument.data(),static_cast<int>(argument.size()),&selected[0],length)!=length)return false;
        std::replace(selected.begin(),selected.end(),L'/',L'\\');
        const auto at=selected.find_last_of(L'\\');if(at==std::wstring::npos)return false;
        const auto name=selected.substr(at+1);if(name.size()<6 || name.substr(name.size()-4)!=L".s7s")return false;
        const auto stem=name.substr(0,name.size()-4);
        for(auto ch:stem){if(ch>127)return false;source_token.push_back(static_cast<char>(ch));}
        if(!Path(launch.profile,source_token,expected))return false;
        std::replace(expected.begin(),expected.end(),L'/',L'\\');
        if(_wcsicmp(selected.c_str(),expected.c_str()))return false;
    }
    std::vector<unsigned char> bytes;
    if(!CrossplayResumeSource::ReadArchive(expected,bytes) || !CrossplayResumeSource::ArchiveHash(bytes.data(),bytes.size(),identity.sha256))return false;
    try{
        std::istringstream input(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()),std::ios::in|std::ios::binary);
        const auto members=neo_save::Reader(input).MembersWithWorld();const auto logic=neo_save::LogicTurn(members[1]);
        bool found=false;const auto serialized=neo_save::Cache{members[0]}.Neo(found);
        if(!found || logic==UINT32_MAX)return false;
        neo_logistics::SavedState state;
        if(!neo_logistics::Decode(neo_save::Pickle{serialized}.Payload(),state) || !state.enabled
            || state.logic_turn!=logic || state.turn!=logic+1)return false;
        identity.bytes=bytes.size();token=std::move(source_token);return true;
    }catch(const std::exception&){identity={};return false;}
}
inline bool Capture(const CrossplayProduction::Launch& launch,const CrossplayBootstrap::Context& bootstrap,
                    const CrossplayNative::ParticipantEvent& binding,DWORD owner,uintptr_t session,
                    CrossplaySessionConfig::Connections ids,Value& output){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;output={};
    if(!launch.mixed || !launch.host || launch.channel.edition!=Crossplay::Edition::Gold
        || !bootstrap.saved || !CrossplayBootstrap::Valid(bootstrap) || owner!=GetCurrentThreadId()
        || !CrossplayResumeTransfer::Nonzero(launch.channel.content)
        || !CrossplayNativeCodec::ResumeFileDetail::Owner(binding,owner,session,ids,true)
        || !CrossplayGoldChoices::Methods())return false;
    Value next;std::wstring path;std::vector<unsigned char> repeated;
    if(CrossplayGoldChoices::Snapshot(next.cache)!=CrossplayGoldChoices::Observation::Captured
        || next.cache.map!=bootstrap.map || next.cache.flags!=bootstrap.flags
        || !Path(launch.profile,next.cache.save,path)
        || !CrossplayGoldChoices::ReadTemplate(next.map_template) || next.map_template!=bootstrap.map_template
        || !CrossplayNativeCodec::SavedSessionConfigDetail::CaptureGold(binding,owner,session,ids,
            next.cache.map,next.cache.save,next.configuration)
        || !CrossplayResumeSource::CaptureSelected(path,launch.channel.content,next.cache.map,next.selected)
        || !next.selected.players_observed
        || !CrossplaySavedAI::FromArchive(next.selected.players,next.ai_mask,next.ai))return false;
    CrossplayGoldChoices::Cache after;std::string after_template;
    if(CrossplayGoldChoices::Snapshot(after)!=CrossplayGoldChoices::Observation::Captured
        || !CrossplayGoldChoices::Same(next.cache,after)
        || !CrossplayGoldChoices::ReadTemplate(after_template) || after_template!=next.map_template
        || !CrossplayNativeCodec::SavedSessionConfigDetail::CaptureGold(binding,owner,session,ids,
            next.cache.map,next.cache.save,repeated) || repeated!=next.configuration
        || !CrossplayNativeCodec::ResumeFileDetail::Owner(binding,owner,session,ids,true))return false;
    next.owner=owner;next.session=session;next.ids=ids;output=std::move(next);return true;
}
inline bool Current(const CrossplayNative::ParticipantEvent& binding,const Value& value){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    if(!value.owner || value.owner!=GetCurrentThreadId() || !value.session || !value.selected.lease
        || value.selected.lease->value==INVALID_HANDLE_VALUE || !CrossplayResumeSource::Valid(value.selected.archive)
        || !CrossplayResumeTransfer::Valid(value.selected.checkpoint)
        || !CrossplayNativeCodec::ResumeFileDetail::Owner(binding,value.owner,value.session,value.ids,true))return false;
    CrossplayGoldChoices::Cache cache;std::string map_template;
    return CrossplayGoldChoices::Snapshot(cache)==CrossplayGoldChoices::Observation::Captured
        && CrossplayGoldChoices::Same(value.cache,cache)
        && CrossplayGoldChoices::ReadTemplate(map_template) && map_template==value.map_template;
}
}
