#pragma once
#include "crossplay_gold_guest_identity.h"
#include "crossplay_ready_state.h"

// A real Gold guest action produces intent only. The returned achieved state
// must come from the authenticated original host's observed Ready snapshot.
namespace CrossplayGoldGuestReady {
inline bool Methods(){
    using namespace CrossplayNativeCodec;
    const unsigned char role[]={0x8a,0x41,0x14,0xc3};
    return ReadyDetail::Methods(Crossplay::Edition::Gold) && Detail::Readable(reinterpret_cast<void*>(0xf569bc+0x54),4)
        && *reinterpret_cast<const uintptr_t*>(0xf569bc+0x54)==0xa3a520
        && Detail::Readable(reinterpret_cast<void*>(0xa3a520),sizeof(role)) && !memcmp(reinterpret_cast<void*>(0xa3a520),role,sizeof(role));
}
inline bool Context(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                    const CrossplayNative::GoldGuestRowClaim& claim,const CrossplayNative::GoldGuestIdentity& identity,
                    CrossplaySessionConfig::Connections ids){
    if(ids.host!=admission.PeerLocalConnection() || ids.guest!=admission.OwnConnection()
        || !identity.Applied(binding,admission,claim)
        || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,false) || !Methods())return false;
    CrossplayNative::LobbySnapshot lobby;if(!binding.CaptureLobby(lobby))return false;
    const auto* session=reinterpret_cast<const unsigned char*>(lobby.instance);
    return CrossplayNativeCodec::Detail::Readable(session,0x34) && CrossplayChat::Word(session+0x30)==2;
}
inline bool CaptureIntentForSlot(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                    const CrossplayNative::GoldGuestRowClaim& claim,const CrossplayNative::GoldGuestIdentity& identity,
                    CrossplaySessionConfig::Connections ids,uint32_t original_slot,const void* event,std::vector<unsigned char>& intent,
                    unsigned char& original_requested){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    intent.clear();original_requested=0;using namespace CrossplayNativeCodec;
    if((original_slot!=1 && original_slot!=2) || !Context(binding,admission,claim,identity,ids))return false;
    const auto pin=ReadyDetail::Pin(Crossplay::Edition::Gold);
    if(!ReadyDetail::Source(pin,event,ids.guest) || !Detail::Readable(event,0x18)
        || CrossplayChat::Word(static_cast<const unsigned char*>(event)+0x10)!=original_slot
        || static_cast<const unsigned char*>(event)[0x14]>1)return false;
    intent.resize(CrossplayReady::WireSize);CrossplayChat::Put(intent.data(),CrossplayReady::Class);
    memcpy(intent.data()+4,static_cast<const unsigned char*>(event)+4,9);
    original_requested=static_cast<const unsigned char*>(event)[0x14];
    return CrossplayReady::Validate(intent,ids.guest);
}
inline bool CaptureIntent(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                    const CrossplayNative::GoldGuestRowClaim& claim,const CrossplayNative::GoldGuestIdentity& identity,
                    CrossplaySessionConfig::Connections ids,const void* event,std::vector<unsigned char>& intent,
                    unsigned char& original_requested){
    return CaptureIntentForSlot(binding,admission,claim,identity,ids,2,event,intent,original_requested);
}
// Only the separately pinned original SDK-emitter boundary may use this form.
// SDK slot1 and its private Ready bit are not common-session Ready authority.
inline bool CapturePrivateSdkIntent(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                    const CrossplayNative::GoldGuestRowClaim& claim,const CrossplayNative::GoldGuestIdentity& identity,
                    CrossplaySessionConfig::Connections ids,const void* event,std::vector<unsigned char>& intent){
    unsigned char ignored=0;
    return CaptureIntentForSlot(binding,admission,claim,identity,ids,1,event,intent,ignored);
}
inline bool StateWire(uint32_t slot,uint32_t source,unsigned ready,std::vector<unsigned char>& wire){
    wire.clear();if(slot<1 || slot>2 || !source || source==UINT32_MAX || ready>1)return false;
    wire.resize(CrossplayReady::NativeStateSize);CrossplayChat::Put(wire.data(),CrossplayReady::Class);
    CrossplayChat::Put(wire.data()+4,CrossplayReady::Type);CrossplayChat::Put(wire.data()+8,source);wire[12]=1;
    CrossplayChat::Put(wire.data()+13,slot);wire[17]=static_cast<unsigned char>(ready);return true;
}
inline bool EnqueueState(const std::vector<unsigned char>& wire,uint32_t slot,uint32_t source){
    using namespace CrossplayNativeCodec;
    std::vector<unsigned char> expected;
    if(wire.size()!=CrossplayReady::NativeStateSize || !StateWire(slot,source,wire[17],expected) || expected!=wire
        || !Methods())return false;
    const auto pin=ReadyDetail::Pin(Crossplay::Edition::Gold);Detail::OwnedStream stream(pin);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{pin.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=Detail::GoldRead(pin.read,&reader,name);
    if(!decoded || !Detail::Readable(decoded,0x18) || CrossplayChat::Word(static_cast<const unsigned char*>(decoded))!=pin.event_vt)return false;
    struct Owned {void* object;uintptr_t table;~Owned(){reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(
        *reinterpret_cast<const uintptr_t*>(table))(object,1);}} owned{decoded,pin.event_vt};
    const auto* bytes=static_cast<const unsigned char*>(decoded);std::vector<unsigned char> repeated;
    if(stream.value.position!=wire.size() || !ReadyDetail::Source(pin,decoded,source)
        || CrossplayChat::Word(bytes+0x10)!=slot || bytes[0x14]!=wire[17]
        || !Detail::Encode(pin,decoded,repeated) || repeated!=wire)return false;
    uintptr_t send=0;void* transport=Detail::Transport(Crossplay::Edition::Gold,send);if(!transport)return false;
    reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    return true;
}
struct Pending {DWORD owner=0;uintptr_t session=0,cache=0;CrossplayReadyState::Snapshot value{};ULONGLONG started=0;};
inline bool Apply(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                    const CrossplayNative::GoldGuestRowClaim& claim,const CrossplayNative::GoldGuestIdentity& identity,
                    CrossplaySessionConfig::Connections ids,const CrossplayReadyState::Snapshot& observed,Pending& pending){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;pending={};using namespace CrossplayNativeCodec;
    if(!CrossplayReadyState::Valid(observed) || !Context(binding,admission,claim,identity,ids))return false;
    CrossplayNative::LobbySnapshot lobby;if(!binding.CaptureLobby(lobby))return false;
    auto* cache=SessionConfigDetail::GoldConfiguration();if(!cache)return false;
    SessionConfigDetail::ConfigLock lock(cache);if(!lock.acquired)return false;
    if(!SessionConfigDetail::Fields(SessionConfigDetail::Pin(Crossplay::Edition::Gold),cache+0x24,cache+0x34,cache+0x44,ids))return false;
    const auto* session=reinterpret_cast<const unsigned char*>(lobby.instance);
    if(!Detail::Readable(session,0x34) || CrossplayChat::Word(session+0x30)!=2)return false;
    std::vector<unsigned char> host,guest;
    if(!StateWire(1,ids.host,observed.host,host) || !StateWire(2,ids.guest,observed.guest,guest)
        || !EnqueueState(host,1,ids.host) || !EnqueueState(guest,2,ids.guest))return false;
    pending={GetCurrentThreadId(),lobby.instance,reinterpret_cast<uintptr_t>(cache),observed,GetTickCount64()};return true;
}
inline CrossplayNativeCodec::ReadyObservation Observe(const CrossplayNative::ParticipantEvent& binding,
                    CrossplayNative::LobbyAdmission& admission,const CrossplayNative::GoldGuestRowClaim& claim,
                    const CrossplayNative::GoldGuestIdentity& identity,CrossplaySessionConfig::Connections ids,const Pending& pending){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;using namespace CrossplayNativeCodec;
    if(!pending.owner || pending.owner!=GetCurrentThreadId() || GetTickCount64()-pending.started>10000
        || !Context(binding,admission,claim,identity,ids))return ReadyObservation::Invalid;
    CrossplayNative::LobbySnapshot lobby;if(!binding.CaptureLobby(lobby) || lobby.instance!=pending.session)return ReadyObservation::Invalid;
    auto* cache=SessionConfigDetail::GoldConfiguration();if(reinterpret_cast<uintptr_t>(cache)!=pending.cache)return ReadyObservation::Invalid;
    SessionConfigDetail::ConfigLock lock(cache);if(!lock.acquired)return ReadyObservation::Pending;
    if(!SessionConfigDetail::Fields(SessionConfigDetail::Pin(Crossplay::Edition::Gold),cache+0x24,cache+0x34,cache+0x44,ids))return ReadyObservation::Invalid;
    const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+0x48);
    return slots[4]==pending.value.host && slots[0x1c]==pending.value.guest?ReadyObservation::Applied:ReadyObservation::Pending;
}
}
