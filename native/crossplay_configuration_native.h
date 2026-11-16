#pragma once
#include "crossplay_configuration_receipt.h"
#include "crossplay_ready_state_codec.h"

namespace CrossplayConfigurationNative {
inline void Word(std::vector<unsigned char>& bytes,uint32_t value){
    const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);
}
inline void Text(std::vector<unsigned char>& bytes,const std::string& value){
    bytes.insert(bytes.end(),{'S','t','r','_'});Word(bytes,value.empty()?0u:static_cast<uint32_t>(value.size()+1));
    if(!value.empty()){bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}
}
inline bool Snapshot(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                     CrossplaySessionConfig::Connections ids,CrossplayConfigurationReceipt::Interval interval,
                     std::vector<unsigned char>& wire){
    using namespace CrossplayNativeCodec;wire.clear();
    CrossplayNative::LobbySnapshot lobby;
    if(interval.owner!=GetCurrentThreadId() || !interval.session || !interval.cache
        || (edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)
        || !SessionConfigDetail::Methods(edition) || !SessionConfigDetail::Roster(binding,ids,false)
        || !binding.CaptureLobby(lobby) || lobby.instance!=interval.session)return false;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);
    const auto pin=SessionConfigDetail::Pin(edition);
    if(reinterpret_cast<uintptr_t>(cache)!=interval.cache || !cache
        || !SessionConfigDetail::Fields(pin,cache+0x24,cache+0x34,cache+0x44,ids))return false;
    const auto* begin=*reinterpret_cast<const unsigned char* const*>(cache+0x44);
    std::array<unsigned char,8*0x18> slots{};memcpy(slots.data(),begin,slots.size());
    std::string map,save;
    if(!SessionConfigDetail::NativeString(cache+0x24,map) || !SessionConfigDetail::NativeString(cache+0x34,save))return false;
    std::vector<unsigned char> snapshot;
    Word(snapshot,CrossplaySessionConfig::Class);Word(snapshot,CrossplaySessionConfig::Type);Word(snapshot,ids.host);snapshot.push_back(1);
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
    if(!CrossplaySessionConfig::BindSource(snapshot,ids,bound)
        || ReadyStateDetail::HistoryConfiguration(edition)!=cache
        || *reinterpret_cast<const unsigned char* const*>(cache+0x44)!=begin
        || !Detail::Readable(begin,slots.size()) || memcmp(begin,slots.data(),slots.size())
        || !SessionConfigDetail::NativeString(cache+0x24,after_map) || after_map!=map
        || !SessionConfigDetail::NativeString(cache+0x34,after_save) || after_save!=save
        || !binding.CaptureLobby(lobby) || lobby.instance!=interval.session
        || !SessionConfigDetail::Roster(binding,ids,false))return false;
    wire=std::move(bound);return true;
}
}
