#pragma once
#include "crossplay_setup_checkpoint.h"
#include "crossplay_setup_signature_codec.h"
#include "crossplay_game_start_codec.h"

// Read-only, opt-in checkpoint. The local fingerprint deliberately retains
// native pointer values and unknown fields: it detects local changes without
// declaring those fields equivalent between editions. Pointed-to objects are
// not a complete dependency closure. No lock, event or cache mutation occurs.
namespace CrossplaySetupCheckpointNative {
namespace Detail {
inline void Word(std::vector<unsigned char>& bytes,uint32_t value) {
    const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);
}
inline bool Local(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                  CrossplaySessionConfig::Connections ids,bool host,CrossplaySetupCheckpoint::Snapshot& result) {
    using namespace CrossplayNativeCodec;result={};
    CrossplaySetupSignatureNative::Pin pin{};
    if(!CrossplaySetupSignatureNative::Layout(edition,pin)
        || !CrossplaySetupSignatureNative::Methods(pin)
        || !GameStartDetail::FreshReadyMap(edition,binding,ids,host,"Maps\\Skirmish\\s04_m06_Monhaim_Valley"))return false;
    const auto profile=CrossplayNativeCodec::Detail::Pin(edition);
    if(!CrossplayNativeCodec::Detail::Methods(profile) || !CrossplayNativeCodec::Detail::Tables(profile,edition)
        || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(pin.manager),4))return false;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(pin.manager);
    if(!CrossplayNativeCodec::Detail::Readable(manager,8+2*pin.stride)
        || CrossplayChat::Word(manager)!=pin.type || manager[4]>1)return false;
    const unsigned char mode=manager[4];const auto* frame=manager+((mode-1)&1)*pin.stride;
    const size_t offset=profile.gold?0xa4:0x98,extent=profile.gold?0x9079:0x8db9;
    const auto* config=*reinterpret_cast<const unsigned char* const*>(frame+offset);
    if(!CrossplayNativeCodec::Detail::Readable(config,8))return false;
    const auto* rows=*reinterpret_cast<const unsigned char* const*>(config+4);
    const auto* root=frame+0x34;
    if(*reinterpret_cast<const unsigned char* const*>(config)!=rows
        || !CrossplayNativeCodec::Detail::Readable(rows,extent) || rows[extent-1]!=4
        || !CrossplayNativeCodec::Detail::Readable(root,pin.root_size))return false;
    auto* cache=profile.gold?SessionConfigDetail::GoldConfiguration():ReadyStateDetail::HistoryConfiguration(edition);
    if(!cache || !SessionConfigDetail::Fields(profile,cache+0x24,cache+0x34,cache+0x44,ids))return false;
    const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+(profile.gold?0x48:0x44));
    CrossplayNative::LobbySnapshot lobby;
    if(!binding.CaptureLobby(lobby) || !lobby.instance || lobby.members.size()!=2)return false;
    std::vector<unsigned char> bytes(rows,rows+extent);
    bytes.insert(bytes.end(),root,root+pin.root_size);bytes.insert(bytes.end(),slots,slots+8*0x18);
    std::sort(lobby.members.begin(),lobby.members.end(),[](const auto& a,const auto& b){return a.player.slot<b.player.slot;});
    for(const auto& member:lobby.members) {
        const auto& p=member.player;
        Word(bytes,p.connection);Word(bytes,p.slot);Word(bytes,p.principal);Word(bytes,p.station);
        Word(bytes,member.status);Word(bytes,p.host?1u:0u);Word(bytes,p.local?1u:0u);
        Word(bytes,static_cast<uint32_t>(p.name.size()));bytes.insert(bytes.end(),p.name.begin(),p.name.end());
    }
    if(*reinterpret_cast<const unsigned char* const*>(pin.manager)!=manager || manager[4]!=mode
        || *reinterpret_cast<const unsigned char* const*>(frame+offset)!=config
        || *reinterpret_cast<const unsigned char* const*>(config)!=rows || *reinterpret_cast<const unsigned char* const*>(config+4)!=rows
        || (profile.gold?SessionConfigDetail::GoldConfiguration():ReadyStateDetail::HistoryConfiguration(edition))!=cache
        || *reinterpret_cast<const unsigned char* const*>(cache+(profile.gold?0x48:0x44))!=slots
        || !CrossplaySetupSignatureNative::Hash(bytes,result.local))return false;
    result.thread=GetCurrentThreadId();result.session=lobby.instance;
    result.preparation=reinterpret_cast<uintptr_t>(rows);result.cache=reinterpret_cast<uintptr_t>(cache);return true;
}
}
inline bool Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,bool host,CrossplaySetupCheckpoint::Snapshot& result) {
    result={};CrossplaySetupCheckpoint::Snapshot before{},after{};
    CrossplaySetupSignature::Signature writers{};
    if(!Detail::Local(edition,binding,ids,host,before)
        || !CrossplaySetupSignatureNative::Capture(edition,binding,ids,host,writers,nullptr)
        || !Detail::Local(edition,binding,ids,host,after))return false;
    before.writers=after.writers=writers;
    if(!CrossplaySetupCheckpoint::Equal(before,after))return false;
    result=after;return true;
}
class Checkpoint {
    CrossplaySetupCheckpoint::Checkpoint checkpoint;
    Crossplay::Edition edition{};CrossplaySessionConfig::Connections ids{};
    bool host=false,bound=false;DWORD owner=0;
    bool Current(const CrossplayNative::ParticipantEvent& binding,CrossplaySetupCheckpoint::Snapshot& snapshot) {
        if(!bound || owner!=GetCurrentThreadId() || !Capture(edition,binding,ids,host,snapshot)) {
            checkpoint.Invalidate();return false;
        }
        return true;
    }
public:
    Checkpoint()=default;
    Checkpoint(const Checkpoint&)=delete;
    Checkpoint& operator=(const Checkpoint&)=delete;
    void Invalidate(){checkpoint.Invalidate();}
    CrossplaySetupCheckpoint::Phase GetPhase() const{return checkpoint.GetPhase();}
    bool Bind(Crossplay::Edition value,const CrossplayNative::ParticipantEvent& binding,
              CrossplaySessionConfig::Connections connections,bool is_host,const Crossplay::Peer& peer,uint32_t revision) {
        if(bound){Invalidate();return false;}
        // One owned lifetime, including unsuccessful initialization.
        bound=true;owner=GetCurrentThreadId();edition=value;ids=connections;host=is_host;
        CrossplaySetupCheckpoint::Snapshot snapshot{};CrossplaySetupCheckpoint::Nonce nonce{};
        if(!Current(binding,snapshot) || BCryptGenRandom(nullptr,nonce.data(),static_cast<ULONG>(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) {
            Invalidate();return false;
        }
        return checkpoint.Bind(peer,host,revision,snapshot,nonce,GetTickCount64());
    }
    bool Offer(const CrossplayNative::ParticipantEvent& binding,std::vector<unsigned char>& output) {
        output.clear();CrossplaySetupCheckpoint::Snapshot current{};
        return Current(binding,current) && checkpoint.Offer(current,GetTickCount64(),output);
    }
    bool Sent(const CrossplayNative::ParticipantEvent& binding) {
        CrossplaySetupCheckpoint::Snapshot current{};return Current(binding,current) && checkpoint.Sent(current,GetTickCount64());
    }
    bool Receive(const CrossplayNative::ParticipantEvent& binding,const std::vector<unsigned char>& input,std::vector<unsigned char>& output) {
        output.clear();CrossplaySetupCheckpoint::Snapshot current{};
        return Current(binding,current) && checkpoint.Receive(current,GetTickCount64(),input,output);
    }
    bool Confirmed(const CrossplayNative::ParticipantEvent& binding) {
        CrossplaySetupCheckpoint::Snapshot current{};return Current(binding,current) && checkpoint.Confirmed(current,GetTickCount64());
    }
};
}
