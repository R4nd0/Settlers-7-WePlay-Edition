#pragma once
#include "crossplay_profile_name_reference.h"
#include "crossplay_gold_closed_preparation.h"
#include "crossplay_history_host_guest_identity.h"
// Two-human fresh reverse-role preparation only. Names come from the actual
// host-selected native catalogue entries and must travel over the authenticated
// current setup interval. Original setters resolve existing receiver-local IDs;
// this never creates a catalogue entry, native result or world permission.
namespace CrossplayHumanProfileSelection {
using Names=std::array<std::string,2>;
inline constexpr unsigned char Magic[8]={'S','7','H','P','R','O','F',1};
inline bool Is(const std::vector<unsigned char>& frame){return frame.size()>=8 && !memcmp(frame.data(),Magic,8);}
inline bool Frame(const Crossplay::Peer& peer,uint32_t revision,const Names& names,std::vector<unsigned char>& frame){
    frame.clear();if(revision<1 || revision>2)return false;
    for(const auto& name:names)if(!CrossplayProfileNameReference::Key(name))return false;
    frame.assign(Magic,Magic+8);frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(44);CrossplayChat::Put(frame.data()+40,revision);
    for(unsigned i=0;i<2;++i){const size_t at=frame.size();frame.resize(at+8);CrossplayChat::Put(frame.data()+at,i+1);
        CrossplayChat::Put(frame.data()+at+4,static_cast<uint32_t>(names[i].size()));frame.insert(frame.end(),names[i].begin(),names[i].end());}
    return true;
}
inline bool Receive(const Crossplay::Peer& peer,uint32_t revision,const std::vector<unsigned char>& frame,Names& names){
    names={};if(revision<1 || revision>2 || frame.size()<62 || frame.size()>316 || !Is(frame)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=revision)return false;
    Names actual;size_t at=44;
    for(unsigned i=0;i<2;++i){
        if(frame.size()-at<8 || CrossplayChat::Word(frame.data()+at)!=i+1)return false;
        const auto count=CrossplayChat::Word(frame.data()+at+4);at+=8;
        if(!count || count>128 || count>frame.size()-at)return false;
        actual[i].assign(reinterpret_cast<const char*>(frame.data()+at),count);at+=count;
        if(!CrossplayProfileNameReference::Key(actual[i]))return false;
    }
    if(at!=frame.size())return false;names=std::move(actual);return true;
}
inline bool Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,bool host,Names& names){
    names={};using namespace CrossplayNativeCodec;
    if(!SessionConfigDetail::Roster(binding,ids,host))return false;
    CrossplayAIPreparationSource::Snapshot before,after;unsigned stage=0;
    if(!CrossplayAIPreparationSource::Capture(edition,before,stage) || !before.same)return false;
    Names actual;
    for(unsigned i=0;i<2;++i){const auto& row=before.writer[i+1];
        if(row.slot!=i+1 || row.controller!=3 || row.kind!=1 || !CrossplayProfileNameReference::Name(edition,row.profile,actual[i]))return false;}
    if(!CrossplayAIPreparationSource::Capture(edition,after,stage) || !after.same || !SessionConfigDetail::Roster(binding,ids,host))return false;
    for(unsigned i=0;i<2;++i)if(before.writer[i+1].profile!=after.writer[i+1].profile)return false;
    names=std::move(actual);return true;
}
inline bool Preserved(const unsigned char* before,const unsigned char* after,size_t size){
    if(!before || !after || size!=25*0x5c4)return false;
    for(size_t i=0;i<size;++i){
        if((i>=0x5c4+0x18 && i<0x5c4+0x1c) || (i>=2*0x5c4+0x18 && i<2*0x5c4+0x1c))continue;
        if(before[i]!=after[i])return false;
    }
    return true;
}
inline bool Methods(){
    const unsigned char expected[]={0x89,0x48,0x18,0xc3};
    return CrossplayNative::GoldGuestIdentityDetail::Methods()
        && CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(0x5b1f90),4) && !memcmp(reinterpret_cast<const void*>(0x5b1f90),expected,4);
}
inline bool HistoryPreserved(const unsigned char* before,const unsigned char* after,size_t size){
    if(!before || !after || size!=0x8dc0)return false;
    for(size_t i=0;i<size;++i){
        if((i>=0x5a8+0x18 && i<0x5a8+0x1c) || (i>=2*0x5a8+0x18 && i<2*0x5a8+0x1c))continue;
        if(before[i]!=after[i])return false;
    }
    return true;
}
inline bool RestoreHistorySelection(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
    const CrossplayNative::HistoryClaimDetail::Interval& selected,const Names& source,ULONGLONG selected_at){
    struct ErrorScope{DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;using namespace CrossplayNative;
    constexpr auto edition=Crossplay::Edition::SteamHistory;HistoryClaimDetail::Pin pin;
    const auto now=GetTickCount64();HistoryClaimDetail::Interval before,after;LobbySnapshot lobby;
    if(now<selected_at || now-selected_at>10000 || !HistoryHostGuestIdentityDetail::Methods()
        || !HistoryClaimDetail::Layout(edition,pin) || !binding.CaptureLobby(lobby)
        || !HistoryHostGuestRowDetail::Capture(binding,admission,before) || !HistoryClaimDetail::Same(selected,before))return false;
    auto* rows=reinterpret_cast<unsigned char*>(before.rows);std::array<uint32_t,2> expected{};
    for(unsigned i=0;i<2;++i){const auto* row=rows+(i+1)*0x5a8;const Participant* member=nullptr;std::string name;
        for(const auto& observed:lobby.members)if(observed.player.slot==i+1)member=&observed.player;
        if(!member || CrossplayChat::Word(row+0x5c)!=i+1 || CrossplayChat::Word(row+0x60)!=3 || CrossplayChat::Word(row+0x64)!=1
            || !CrossplayNativeCodec::SessionConfigDetail::NativeString(row+0x1c,name) || name!=member->name
            || !HistoryRoleDetail::Writable(row+0x18,4) || !CrossplayProfileNameReference::Resolve(edition,source[i],expected[i]))return false;
        const uint32_t current=CrossplayChat::Word(row+0x18);
        if(current!=expected[i] && (CrossplayProfileNameReference::Valid(edition,current)
            || (current!=member->principal && current!=(member->principal?member->principal:member->slot))))return false;
    }
    const std::vector<unsigned char> original(rows,rows+0x8dc0);
    for(unsigned i=0;i<2;++i)reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(pin.set_id)(rows+(i+1)*0x5a8,expected[i]);
    Names actual;
    return HistoryHostGuestIdentityDetail::Methods() && HistoryPreserved(original.data(),rows,original.size())
        && HistoryHostGuestRowDetail::Capture(binding,admission,after) && HistoryClaimDetail::Same(selected,after)
        && Capture(edition,binding,{admission.OwnConnection(),admission.PeerLocalConnection()},true,actual) && actual==source;
}
inline bool ApplyGold(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,const Names& source,
    const CrossplayConfigurationReceipt::Interval& interval,const CrossplayConfigurationReceipt::Receipt& receipt){
    struct ErrorScope{DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    using namespace CrossplayNative;LobbySnapshot lobby;GoldGuestRowDetail::Interval before,after;uintptr_t session=0,cache=0;
    if(!Methods() || !CrossplayClosedConfigReceipt::Context(Crossplay::Edition::Gold,binding,ids,3,interval,receipt,session,cache)
        || !binding.CaptureLobby(lobby) || !GoldGuestRowDetail::Guest(lobby,ids.guest,ids.host)
        || !GoldGuestRowDetail::Capture(lobby,before) || before.session!=session)return false;
    std::array<uint32_t,2> resolved{};auto* rows=reinterpret_cast<unsigned char*>(before.rows);
    for(unsigned i=0;i<2;++i){
        const auto* row=rows+(i+1)*0x5c4;std::string name;const Participant* member=nullptr;
        for(const auto& m:lobby.members)if(m.player.slot==i+1)member=&m.player;
        if(!member || CrossplayChat::Word(row+0x5c)!=i+1 || CrossplayChat::Word(row+0x60)!=3 || CrossplayChat::Word(row+0x64)!=1
            || !CrossplayNativeCodec::SessionConfigDetail::NativeString(row+0x1c,name) || name!=member->name
            || !HistoryRoleDetail::Writable(row+0x18,4) || !CrossplayProfileNameReference::Resolve(Crossplay::Edition::Gold,source[i],resolved[i]))return false;
    }
    const std::vector<unsigned char> original(rows,rows+25*0x5c4);
    for(unsigned i=0;i<2;++i)CrossplayGoldClosedPreparation::SetWith(0x5b1f90,rows+(i+1)*0x5c4,resolved[i]);
    Names actual;
    return Methods() && Preserved(original.data(),rows,original.size())
        && binding.CaptureLobby(lobby) && GoldGuestRowDetail::Guest(lobby,ids.guest,ids.host) && GoldGuestRowDetail::Capture(lobby,after)
        && GoldGuestRowDetail::Same(before,after) && Capture(Crossplay::Edition::Gold,binding,ids,false,actual) && actual==source
        && CrossplayClosedConfigReceipt::Context(Crossplay::Edition::Gold,binding,ids,3,interval,receipt,session,cache);
}
}
