#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <intrin.h>
#pragma comment(lib,"user32.lib")
#include "crossplay_production_runtime.h"
#include "crossplay_production_observer.h"
#include "crossplay_admission.h"
#include "crossplay_history_role.h"
#include "crossplay_load_progress_codec.h"
#include "crossplay_bootstrap.h"
#include "crossplay_session_config_codec.h"
namespace CrossplayProduction {
namespace {
struct Runtime;
Runtime* current=nullptr;
void CaptureLocal(void* session,const unsigned char* event);
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD);
struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}};
bool RoundTrip(Crossplay::Edition edition,const void* event){
    using namespace CrossplayNativeCodec::Detail;
    const auto p=Pin(edition);if(!Methods(p))return false;
    std::vector<unsigned char> wire;
    if(!Encode(p,event,wire))return false;
    OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=wire.data();stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Visitor reader{p.reader_vt,1,&stream.value,0};constexpr char name[]="WePlayEvent";
    void* decoded=p.gold?GoldRead(p.read,&reader,name):reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Readable(decoded,0x10) || *static_cast<uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    const bool okay=stream.value.position==wire.size() && Encode(p,decoded,repeated) && repeated==wire;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
struct Runtime {
    const Launch launch;
    const DWORD owner=GetCurrentThreadId();
    Phase phase=Phase::Dormant;
    Crossplay::Channel channel;
    CrossplayNative::ParticipantEvent original;
    CrossplayNative::LobbyAdmission admission;
    uintptr_t session=0,gate=0;
    uint32_t extra=0;
    bool backend_projected=false,native_bound=false,gate_installed=false,lobby_captured=false;
    bool bootstrap_sent=false,bootstrap_received=false;
    CrossplayBootstrap::Context bootstrap;
    UINT_PTR timer=0;
    explicit Runtime(const Launch& input):launch(input){}
    bool Fail(){phase=Phase::Failed;channel.Close();original.Reset();return false;}
    bool Owned()const{return owner==GetCurrentThreadId() && CurrentLaunch() && CurrentLaunch()->mixed;}
    void Capture(void* source,const unsigned char* event){
        if(!Owned() || lobby_captured || phase==Phase::Closed || phase==Phase::Failed)return;
        CrossplayNative::LobbySnapshot lobby;CrossplayCastleSetup::Value castle;
        const bool gold=launch.channel.edition==Crossplay::Edition::Gold;
        if(!native_bound || !original.CaptureLobby(lobby)
            || lobby.instance!=reinterpret_cast<uintptr_t>(source) || lobby.members.size()!=1 || !lobby.host || lobby.local_slot!=1
            || lobby.members[0].status!=2 || !lobby.members[0].player.local || !original.CaptureCastleSetup(event+0x44,castle)){
            Fail();return;
        }
        session=lobby.instance;extra=*reinterpret_cast<const uint32_t*>(event+(gold?0x94:0x8c));
        if(!original.ConstructConfirmedSetup(lobby.members[0].player,castle,extra) || !RoundTrip(launch.channel.edition,event)){
            Fail();return;
        }
        lobby_captured=true;
    }
    static bool __fastcall GoldSlot(void* self,void*,uint32_t station,uint32_t* result){
        const DWORD error=GetLastError();auto* runtime=current;
        if(runtime && runtime->Owned() && runtime->phase!=Phase::Failed && runtime->gate==reinterpret_cast<uintptr_t>(self)
            && runtime->launch.host && reinterpret_cast<uintptr_t>(_ReturnAddress())==0xa51d9b
            && runtime->channel.GetState()==Crossplay::State::Connected){
            const auto* peer=runtime->channel.GetPeer();uint32_t alias=0;
            if(peer)memcpy(&alias,peer->peer_in_local_namespace.data(),4);
            CrossplayNative::LobbySnapshot lobby;
            if(alias && station==alias && CrossplayNativeCodec::Detail::Readable(result,4)
                && runtime->original.CaptureLobby(lobby) && lobby.instance==runtime->session && lobby.host && lobby.local_slot==1){
                *result=2;SetLastError(error);return true;
            }
        }
        SetLastError(error);
        return reinterpret_cast<bool (__thiscall*)(void*,uint32_t,uint32_t*)>(0x98fec0)(self,station,result);
    }
    bool InstallGoldSlot(){
        using CrossplayNativeCodec::Detail::Readable;
        if(launch.channel.edition!=Crossplay::Edition::Gold)return true;
        const auto* main=*reinterpret_cast<const unsigned char* const*>(0x10f83dc);
        if(!Readable(main,0x18) || *reinterpret_cast<const uintptr_t*>(main)!=0xf55600)return false;
        const auto* transport=*reinterpret_cast<const unsigned char* const*>(main+0x10);
        if(!Readable(transport,0x14) || *reinterpret_cast<const uintptr_t*>(transport)!=0xf566cc)return false;
        const auto* head=*reinterpret_cast<const unsigned char* const*>(transport+8);
        if(!Readable(head,12))return false;
        const auto* node=*reinterpret_cast<const unsigned char* const*>(head);
        for(unsigned i=0;node!=head && i<64;++i){
            if(!Readable(node,12))return false;
            auto* plugin=*reinterpret_cast<const unsigned char* const*>(node+8);
            if(!Readable(plugin,4))return false;
            if(*reinterpret_cast<const uint32_t*>(plugin)==0xf4e074){if(gate)return false;gate=reinterpret_cast<uintptr_t>(plugin);}
            node=*reinterpret_cast<const unsigned char* const*>(node);
        }
        if(node!=head || !gate)return false;
        const unsigned char prefix[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x57,0x8b,0xf9};
        gate_installed=Readable(reinterpret_cast<void*>(0x98fec0),sizeof(prefix)) && !memcmp(reinterpret_cast<void*>(0x98fec0),prefix,sizeof(prefix))
            && SessionObserver::Exchange(0xf4e074+0x34,0x98fec0,reinterpret_cast<uintptr_t>(&GoldSlot));
        return gate_installed;
    }
    bool Bootstrap(){
        const auto* peer=channel.GetPeer();if(!peer)return false;
        if(launch.host && !bootstrap_sent){
            if(!lobby_captured)return true;
            CrossplayNative::LobbySnapshot lobby;
            if(!original.CaptureLobby(lobby) || lobby.instance!=session || !lobby.host || lobby.members.size()!=1)return false;
            using namespace CrossplayNativeCodec::SessionConfigDetail;
            if(!Methods(Crossplay::Edition::Gold))return false;
            auto* cache=GoldConfiguration();if(!cache)return true;
            std::string map,save;
            {ConfigLock lock(cache);if(!lock.acquired || !NativeString(cache+0x24,map) || !NativeString(cache+0x34,save))return false;}
            if(map.empty())return true;
            CrossplayBootstrap::Context captured{map,!save.empty()};std::vector<unsigned char> frame;
            if(!CrossplayBootstrap::Encode(*peer,captured,frame) || !channel.Send(frame))return false;
            bootstrap=std::move(captured);bootstrap_sent=true;
        }
        if(!launch.host && !bootstrap_received){
            std::vector<unsigned char> frame;if(!channel.Receive(frame))return true;
            if(!CrossplayBootstrap::Decode(*peer,frame,bootstrap))return false;
            bootstrap_received=true;
        }
        return true;
    }
    void Pump(){
        if(!Owned() || phase==Phase::Dormant || phase==Phase::Closed || phase==Phase::Failed)return;
        if(!channel.Poll()){Fail();return;}
        if(phase==Phase::Connecting){
            if(channel.GetState()!=Crossplay::State::Connected)return;
            if(!Bootstrap()){Fail();return;}
            if(!lobby_captured || (launch.host?!bootstrap_sent:!bootstrap_received))return;
            const auto* event=static_cast<const unsigned char*>(original.Get());
            if(!event || !InstallGoldSlot() || !admission.Begin(channel,launch.channel.edition,launch.host,event+0x44,extra)){
                Fail();return;
            }
            original.Reset();phase=Phase::Admitting;
        }
        if(phase==Phase::Admitting || phase==Phase::RosterBound){
            if(!admission.Poll()){Fail();return;}
            if(admission.GetState()==CrossplayNative::AdmissionState::RosterBound){
                if(!launch.host && !backend_projected){
                    if(!CrossplayNative::ProjectHistoryGuestHost(launch.channel.edition,original,admission)){Fail();return;}
                    backend_projected=true;
                }
                phase=Phase::RosterBound;
            }
        }
    }
    bool Close(){
        if(!Owned())return false;
        if(timer){if(!KillTimer(nullptr,timer))return false;timer=0;}
        channel.Close();original.Reset();
        if(gate_installed && !SessionObserver::Exchange(0xf4e074+0x34,reinterpret_cast<uintptr_t>(&GoldSlot),0x98fec0))return false;
        gate=0;gate_installed=false;phase=Phase::Closed;return true;
    }
};
void CaptureLocal(void* session,const unsigned char* event){
    if(current)try{current->Capture(session,event);}catch(...){current->Fail();}
}
void CALLBACK Tick(HWND,UINT,UINT_PTR id,DWORD){if(current && current->timer==id)PumpRuntime();}
}
bool PrepareRuntime(){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    const auto* launch=CurrentLaunch();if(!launch || !launch->mixed || current)return false;
    std::unique_ptr<Runtime> next(new Runtime(*launch));
    if(!next->original.Bind(launch->channel.edition))return false;
    next->native_bound=true;
    if(!next->channel.Start(launch->channel))return false;
    if(!SessionObserver::Install(launch->channel.edition,GetCurrentThreadId(),&CaptureLocal))return false;
    next->timer=SetTimer(nullptr,0,20,&Tick);
    if(!next->timer){SessionObserver::Release();return false;}
    next->phase=Phase::Connecting;current=next.release();return true;
}
void PumpRuntime(){ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    if(current)try{current->Pump();}catch(...){current->Fail();}
}
bool CloseRuntime(){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    if(!current || !current->Close() || !SessionObserver::Release())return false;
    delete current;current=nullptr;return true;
}
Phase RuntimePhase(){return current && current->Owned()?current->phase:Phase::Dormant;}
std::string BootstrapMap(){
    return current && current->Owned() && !current->launch.host && current->bootstrap_received
        && current->phase!=Phase::Failed && current->phase!=Phase::Closed?current->bootstrap.map:std::string();
}
}
