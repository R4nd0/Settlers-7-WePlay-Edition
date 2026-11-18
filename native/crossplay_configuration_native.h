#pragma once
#include "crossplay_configuration_receipt.h"
#include "crossplay_ready_state_codec.h"
#include <optional>

namespace CrossplayConfigurationNative {
inline unsigned char* Cache(Crossplay::Edition edition){
    return edition==Crossplay::Edition::Gold?CrossplayNativeCodec::SessionConfigDetail::GoldConfiguration():
        CrossplayNativeCodec::ReadyStateDetail::HistoryConfiguration(edition);
}
inline void Word(std::vector<unsigned char>& bytes,uint32_t value){
    const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);
}
inline void Text(std::vector<unsigned char>& bytes,const std::string& value){
    bytes.insert(bytes.end(),{'S','t','r','_'});Word(bytes,value.empty()?0u:static_cast<uint32_t>(value.size()+1));
    if(!value.empty()){bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}
}
template<class Owned,class Fields,class Canonicalize>
inline bool SnapshotChecked(Crossplay::Edition edition,uint32_t host,
        CrossplayConfigurationReceipt::Interval interval,Owned owned,Fields fields,
        Canonicalize canonicalize,std::vector<unsigned char>& wire){
    using namespace CrossplayNativeCodec;wire.clear();
    if(interval.owner!=GetCurrentThreadId() || !interval.session || !interval.cache
        || (edition!=Crossplay::Edition::Gold && edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)
        || !SessionConfigDetail::Methods(edition) || !owned())return false;
    auto* cache=Cache(edition);
    const auto pin=SessionConfigDetail::Pin(edition);
    if(reinterpret_cast<uintptr_t>(cache)!=interval.cache || !cache)return false;
    std::optional<SessionConfigDetail::ConfigLock> gold_lock;
    if(edition==Crossplay::Edition::Gold){gold_lock.emplace(cache);if(!gold_lock->acquired)return false;}
    if(!fields(pin,cache+0x24,cache+0x34,cache+0x44))return false;
    const size_t vector_offset=edition==Crossplay::Edition::Gold?0x48:0x44;
    const auto* begin=*reinterpret_cast<const unsigned char* const*>(cache+vector_offset);
    std::array<unsigned char,8*0x18> slots{};memcpy(slots.data(),begin,slots.size());
    std::string map,save;
    if(!SessionConfigDetail::NativeString(cache+0x24,map) || !SessionConfigDetail::NativeString(cache+0x34,save))return false;
    std::vector<unsigned char> snapshot;
    Word(snapshot,CrossplaySessionConfig::Class);Word(snapshot,CrossplaySessionConfig::Type);Word(snapshot,host);snapshot.push_back(1);
    Text(snapshot,map);Text(snapshot,save);Word(snapshot,8);
    for(unsigned i=0;i<8;++i){
        const auto* row=slots.data()+i*0x18;
        const uint32_t state=CrossplayChat::Word(row);if(state>255)return false;
        snapshot.push_back(row[4]);Word(snapshot,CrossplayChat::Word(row+8));
        snapshot.push_back(row[12]);Word(snapshot,CrossplayChat::Word(row+16));snapshot.push_back(static_cast<unsigned char>(state));
    }
    // The original History cache can retain zero station placeholders after
    // consuming a configuration with receiver-local routing CIDs. Bind only
    // those typed routing fields as the Gold source snapshot already does.
    // Keep the raw cache below for the stability check; never write it back.
    std::string after_map,after_save;std::vector<unsigned char> bound;
    if(!canonicalize(snapshot,bound)
        || Cache(edition)!=cache
        || *reinterpret_cast<const unsigned char* const*>(cache+vector_offset)!=begin
        || !Detail::Readable(begin,slots.size()) || memcmp(begin,slots.data(),slots.size())
        || !SessionConfigDetail::NativeString(cache+0x24,after_map) || after_map!=map
        || !SessionConfigDetail::NativeString(cache+0x34,after_save) || after_save!=save
        || !owned())return false;
    wire=std::move(bound);return true;
}
inline bool Snapshot(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                     CrossplaySessionConfig::Connections ids,CrossplayConfigurationReceipt::Interval interval,
                     std::vector<unsigned char>& wire){
    using namespace CrossplayNativeCodec;
    const auto owned=[&]{
        CrossplayNative::LobbySnapshot lobby;
        return SessionConfigDetail::Roster(binding,ids,false)
            && binding.CaptureLobby(lobby) && lobby.instance==interval.session;
    };
    return SnapshotChecked(edition,ids.host,interval,owned,[&](const auto& p,const auto* map,const auto* save,const auto* rows){
        return SessionConfigDetail::Fields(p,map,save,rows,ids);
    },[&](const auto& snapshot,auto& output){return CrossplaySessionConfig::BindSource(snapshot,ids,output);},wire);
}
inline bool SnapshotRoom(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
        const CrossplayNative::RoomLobbyAdmission& admission,const CrossplaySessionConfig::RoomConnections& ids,
        CrossplayConfigurationReceipt::Interval interval,std::vector<unsigned char>& wire){
    using namespace CrossplayNativeCodec;
    const auto owned=[&]{
        CrossplayNative::LobbySnapshot lobby;
        CrossplaySessionConfig::RoomConnections fresh;
        return binding.CaptureLobby(lobby) && lobby.instance==interval.session
            && admission.CaptureConfigurationRoutes(ids.capacity,fresh) && fresh.host==ids.host
            && fresh.stations==ids.stations;
    };
    return SnapshotChecked(edition,ids.host,interval,owned,[&](const auto& p,const auto* map,const auto* save,const auto* rows){
        return SessionConfigDetail::FieldsChecked(p,map,save,rows,[&](size_t i,unsigned ready,unsigned privacy,uint32_t slot,uint32_t station,uint32_t state){
            return CrossplaySessionConfig::RoomSlotValid(i,ready,privacy,slot,station,state,ids,true);
        });
    },[&](const auto& snapshot,auto& output){return CrossplaySessionConfig::ProjectRoom(snapshot,ids,ids,output,true);},wire);
}
}
