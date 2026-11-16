#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <intrin.h>
#pragma comment(lib,"user32.lib")
#include "crossplay_production_runtime.h"
#include "crossplay_production_observer.h"
#include "crossplay_admission.h"
#include "crossplay_connection.h"
#include "crossplay_history_role.h"
#include "crossplay_history_claim.h"
#include "crossplay_load_progress_codec.h"
#include "crossplay_bootstrap.h"
#include "crossplay_session_config_codec.h"
#include "crossplay_gold_host_choices.h"
#include "crossplay_configuration_observer.h"
#include "crossplay_configuration_native.h"
#include "crossplay_configuration_readiness.h"
#include "crossplay_history_choices.h"
#include "crossplay_outgoing_observer.h"
#include "crossplay_ready_codec.h"
#include "crossplay_logistics_intent.h"
#include "crossplay_neo_selection.h"
#include "crossplay_gold_start_hold.h"
#include "crossplay_closed_preparation.h"
#include "crossplay_neutral_setup_codec.h"
#include "crossplay_setup_root_reader.h"
#include "crossplay_setup_root_stream.h"
#include <bcrypt.h>
#include <deque>
namespace CrossplayProduction {
namespace {
struct Runtime;
Runtime* current=nullptr;
void CaptureLocal(void* session,const unsigned char* event);
void CaptureConfiguration(void* cache,const unsigned char* event);
void CaptureOutgoing(void* manager,const void* event,uint32_t mode,uint32_t target);
void __stdcall CaptureGoldLocal(void* manager,const void* event,uint32_t mode);
bool __stdcall CaptureGoldStart(void* manager,const void* event);
bool NeoWorldReady();
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
    CrossplayNative::HistoryGuestClaim guest_claim;
    CrossplayNative::Participant captured_player;
    CrossplayCastleSetup::Value captured_castle;
    uintptr_t session=0,gate=0;
    uint32_t extra=0;
    bool backend_projected=false,native_bound=false,gate_installed=false,lobby_captured=false;
    bool bootstrap_sent=false,bootstrap_received=false;
    CrossplayBootstrap::Context bootstrap;
    bool configuration_observer_installed=false,configuration_sent=false,configuration_enqueued=false,configuration_applied=false;
    ULONGLONG configuration_started=0;
    CrossplayConfigurationReceipt::Interval configuration_interval;
    CrossplayConfigurationReceipt::Receipt configuration_receipt;
    bool configuration_readiness_queued=false;
    CrossplayNativeCodec::PendingHistoryReadyState configuration_readiness;
    bool outgoing_installed=false;
    std::deque<std::vector<unsigned char>> ready_requests;
    std::deque<CrossplayReadyState::Snapshot> ready_states;
    CrossplayNativeCodec::PendingReadyToggle ready_pending;
    CrossplayNativeCodec::PendingHistoryReadyState ready_projection;
    uint32_t ready_sent=0,ready_received=0;
    ULONGLONG ready_started=0,projection_started=0;
    bool neo_installed=false,neo_staged=false,neo_choice=false,neo_dirty=false;
    CrossplayNeoSelection::Value neo_selection;
    std::deque<std::vector<unsigned char>> neo_selections;
    ULONGLONG neo_selection_started=0;
    // Full world agreement and original queue ownership are separate from the
    // fresh selection frame. Only the later native world controller may bind
    // this agreement and record its original start delivery.
    CrossplayNeo::Agreement neo_world_agreement;
    bool neo_world_queued=false;
    uintptr_t neo_world_session=0;
    UINT_PTR timer=0;
    explicit Runtime(const Launch& input):launch(input){}
    bool Fail(){phase=Phase::Failed;channel.Close();original.Reset();return false;}
    bool Owned()const{return owner==GetCurrentThreadId() && CurrentLaunch() && CurrentLaunch()->mixed;}
#include "crossplay_production_start_setup.inc"
#include "crossplay_production_setup_root.inc"
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
        captured_player=lobby.members[0].player;captured_castle=std::move(castle);lobby_captured=true;
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
            CrossplayBootstrap::Context captured;std::vector<unsigned char> frame;
            const auto observed=CrossplayGoldChoices::Capture(original,owner,session,captured_player,captured);
            if(observed==CrossplayGoldChoices::Observation::Pending)return true;
            if(observed!=CrossplayGoldChoices::Observation::Captured)return false;
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
    CrossplaySessionConfig::Connections LocalConnections()const{
        return launch.host?CrossplaySessionConfig::Connections{admission.OwnConnection(),admission.PeerLocalConnection()}
            :CrossplaySessionConfig::Connections{admission.PeerLocalConnection(),admission.OwnConnection()};
    }
    static bool Jump(uintptr_t at,unsigned size,uintptr_t target){
        using CrossplayNativeCodec::Detail::Readable;
        if(size<5 || !Readable(reinterpret_cast<void*>(at),size))return false;
        const auto* code=reinterpret_cast<const unsigned char*>(at);int32_t delta=0;memcpy(&delta,code+1,4);
        if(code[0]!=0xe9 || at+5+delta!=target)return false;
        for(unsigned i=5;i<size;++i)if(code[i]!=0x90)return false;
        return true;
    }
    bool NeoModules()const{
        if(!neo_installed || !CrossplayLogisticsActive::installed || !CrossplayLogisticsIntent::installed
            || CrossplayLogisticsActive::owner!=owner || CrossplayLogisticsIntent::owner!=owner
            || CrossplayLogisticsActive::edition!=launch.channel.edition || CrossplayLogisticsIntent::edition!=launch.channel.edition
            || CrossplayLogisticsActive::ready!=&NeoWorldReady)return false;
        const auto intent=CrossplayLogisticsIntent::Pin(launch.channel.edition);
        const bool gold=launch.channel.edition==Crossplay::Edition::Gold;
        if(CrossplayLogisticsIntent::resume!=intent.entry+intent.prefix)return false;
        const auto expected=CrossplayLogisticsBinding::Pin(launch.channel.edition);
        const auto actual=CrossplayLogisticsBinding::pin;
        if(actual.global!=expected.global || actual.logic!=expected.logic || actual.main!=expected.main
            || actual.priority!=expected.priority || actual.request!=expected.request || actual.production!=expected.production
            || actual.task_registry!=expected.task_registry || actual.record_registry!=expected.record_registry
            || actual.inverse!=expected.inverse || actual.wrapper!=expected.wrapper || actual.wrapper_value!=expected.wrapper_value
            || actual.compare!=expected.compare)return false;
        if(gold){
            if(GoldLogisticsSort::entry!=0x91ad00 || GoldLogisticsSort::resume!=0x91ad05 || GoldLogisticsSort::caller!=0x93b5f3
                || GoldLogisticsSort::owner!=owner || GoldLogisticsInline::owner!=owner
                || GoldLogisticsSort::ready!=&NeoWorldReady || GoldLogisticsInline::ready!=&NeoWorldReady
                || GoldLogisticsSort::policy!=&CrossplayLogisticsActive::Gold || GoldLogisticsInline::policy!=&CrossplayLogisticsActive::Gold
                || GoldLogisticsSort::original!=reinterpret_cast<uintptr_t>(&GoldLogisticsSort::Replay)
                || GoldLogisticsInline::resume!=GoldLogisticsInline::Resume)return false;
        }else if(CrossplayLogisticsActive::history_entry!=expected.compare || CrossplayLogisticsActive::history_resume!=expected.compare+5
            || CrossplayLogisticsActive::history_global!=expected.global
            || CrossplayLogisticsActive::history_original!=reinterpret_cast<uintptr_t>(&CrossplayLogisticsActive::HistoryReplay))return false;
        if(!Jump(intent.entry,intent.prefix,reinterpret_cast<uintptr_t>(gold?&CrossplayLogisticsIntent::GoldBridge:&CrossplayLogisticsIntent::HistoryBridge)))return false;
        return gold?(Jump(GoldLogisticsSort::entry,5,reinterpret_cast<uintptr_t>(&GoldLogisticsSort::Bridge))
            && Jump(GoldLogisticsInline::Site,9,reinterpret_cast<uintptr_t>(&GoldLogisticsInline::Bridge)))
            :Jump(CrossplayLogisticsBinding::Pin(launch.channel.edition).compare,5,reinterpret_cast<uintptr_t>(&CrossplayLogisticsActive::HistoryBridge));
    }
    bool InstallNeo(){
        if(!CrossplayLogisticsActive::installed && !CrossplayLogisticsActive::Install(launch.channel.edition,owner,&NeoWorldReady))return false;
        if(CrossplayLogisticsActive::edition!=launch.channel.edition || CrossplayLogisticsActive::owner!=owner
            || CrossplayLogisticsActive::ready!=&NeoWorldReady)return false;
        if(!CrossplayLogisticsIntent::installed && !CrossplayLogisticsIntent::Install(launch.channel.edition,owner))return false;
        neo_installed=true;return NeoModules();
    }
    CrossplayGoldChoices::Observation NeoContext(Crossplay::Digest& digest,bool& guest_ready){
        using Observation=CrossplayGoldChoices::Observation;using namespace CrossplayNativeCodec;
        digest={};guest_ready=false;
        if(!Owned() || phase!=Phase::RosterBound || !NeoModules() || bootstrap.saved
            || !CrossplayBootstrap::Valid(bootstrap) || !admission.Poll())return Observation::Invalid;
        if(launch.host?!configuration_sent:!configuration_applied)return Observation::Pending;
        CrossplayBootstrap::Context first,second;std::vector<unsigned char> wire,canonical;
        const auto ids=LocalConnections();
        if(launch.host){
            auto observed=CrossplayGoldChoices::CaptureConfigured(original,owner,session,ids,first);
            if(observed!=Observation::Captured)return observed;
            if(!CaptureGoldSessionConfig(original,ids,wire))return Observation::Pending;
            observed=CrossplayGoldChoices::CaptureConfigured(original,owner,session,ids,second);
            if(observed!=Observation::Captured)return observed;
        }else{
            first=bootstrap;second=bootstrap;
            if(!AppliedGuestChoices(first.flags,first.map_template))return Observation::Invalid;
            if(!CrossplayConfigurationNative::Snapshot(launch.channel.edition,original,ids,configuration_interval,wire))return Observation::Pending;
            if(!AppliedGuestChoices(second.flags,second.map_template))return Observation::Invalid;
        }
        if(first.map!=bootstrap.map || first.flags!=bootstrap.flags || first.map_template!=bootstrap.map_template
            || first.saved || second.map!=first.map || second.flags!=first.flags || second.map_template!=first.map_template
            || second.saved || !CrossplayNeoSelection::Canonical(wire,ids,first,canonical))return Observation::Invalid;
        CrossplaySessionConfig::Layout layout;
        if(!CrossplaySessionConfig::Inspect(wire,ids,layout))return Observation::Invalid;
        guest_ready=wire[layout.slots_at+CrossplaySessionConfig::SlotBytes]!=0;
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
        bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
            && BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
        if(okay)okay=BCryptHashData(hash,canonical.data(),static_cast<ULONG>(canonical.size()),0)>=0
            && BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
        if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
        return okay && CrossplayNeoSelection::Nonzero(digest)?Observation::Captured:Observation::Invalid;
    }
    bool NeoSelectionCurrent(std::string& output){
        if(!neo_staged || neo_dirty || !CrossplayNeoSelection::Valid(neo_selection))return false;
        Crossplay::Digest context{};bool guest_ready=false;
        if(NeoContext(context,guest_ready)!=CrossplayGoldChoices::Observation::Captured
            || context!=neo_selection.configuration || neo_selection.content!=launch.channel.content)return false;
        output=neo_selection.enabled?"1":"0";return true;
    }
    bool PumpNeoSelection(){
        if((launch.host && !neo_dirty) || (!launch.host && neo_selections.empty()))return true;
        Crossplay::Digest context{};bool guest_ready=false;const auto observed=NeoContext(context,guest_ready);
        if(observed==CrossplayGoldChoices::Observation::Invalid)return false;
        if(observed==CrossplayGoldChoices::Observation::Pending){
            const auto now=GetTickCount64();if(!neo_selection_started)neo_selection_started=now;
            return now-neo_selection_started<=10000;
        }
        neo_selection_started=0;
        const auto* peer=channel.GetPeer();if(!peer)return false;
        if(launch.host){
            if(!neo_staged || guest_ready || neo_selection.revision==UINT32_MAX)return false;
            CrossplayNeoSelection::Value value{neo_selection.revision+1,neo_choice,launch.channel.content,context};
            std::vector<unsigned char> frame;
            if(!CrossplayNeoSelection::Encode(*peer,value,frame) || !channel.Send(frame))return false;
            neo_selection=value;neo_dirty=false;return true;
        }
        while(!neo_selections.empty()){
            CrossplayNeoSelection::Value value;
            if(!CrossplayNeoSelection::Decode(*peer,neo_selections.front(),neo_selection.revision,launch.channel.content,context,value))return false;
            neo_selection=value;neo_staged=true;neo_choice=value.enabled;neo_selections.pop_front();
        }
        return true;
    }
    void Outgoing(void* manager,const void* event,uint32_t mode,uint32_t target){
        using namespace CrossplayNativeCodec;
        if(Owned() && phase==Phase::RosterBound && launch.host){GoldNeutralTeam(manager,event,mode,target);return;}
        if(!Owned() || launch.host || phase!=Phase::RosterBound || !configuration_applied
            || (mode!=2 && (mode!=1 || target!=admission.PeerLocalConnection())))return;
        const auto pin=ReadyDetail::Pin(launch.channel.edition);
        if(!Detail::Readable(event,4) || *static_cast<const uintptr_t*>(event)!=pin.event_vt)return;
        uintptr_t send=0;CrossplayNative::LobbySnapshot lobby;uint32_t flags=0;std::string map_template,choice;
        std::vector<unsigned char> wire,frame;
        if(manager!=Detail::Transport(launch.channel.edition,send) || !original.CaptureLobby(lobby)
            || lobby.instance!=session || !AppliedGuestChoices(flags,map_template) || !NeoSelectionCurrent(choice)){Fail();return;}
        const auto* peer=channel.GetPeer();
        if(!peer || !EncodeReadyRequest(launch.channel.edition,original,LocalConnections(),event,wire)
            || !CrossplayReady::Encode(*peer,wire,admission.OwnConnection(),frame) || !channel.Send(frame))Fail();
    }
    bool Readiness(){
        using namespace CrossplayNativeCodec;
        if(launch.host?!configuration_sent:!configuration_applied)return true;
        const auto* peer_pointer=channel.GetPeer();if(!peer_pointer)return false;
        const auto peer=*peer_pointer;
        const auto ids=LocalConnections();std::vector<unsigned char> frame,wire;
        for(unsigned i=0;i<8 && channel.Receive(frame);++i){
            if(CheckpointFrame(frame)){
                if((setup_roots_verified[0] && setup_roots_verified[1]) || setup_checkpoint_frames.size()>=4
                    || frame.size()!=CrossplaySetupCheckpoint::FrameSize)return false;
                setup_checkpoint_frames.push_back(std::move(frame));continue;
            }
            if(RootFrame(frame)){
                if((setup_roots_verified[0] && setup_roots_verified[1]) || setup_root_frames.size()>=16 || frame.size()<CrossplaySetupRootStream::Header
                    || frame.size()>CrossplaySetupRootStream::Header+CrossplaySetupRootStream::ChunkBytes)return false;
                setup_root_frames.push_back(std::move(frame));continue;
            }
            if(StartFrame(frame)){
                if(launch.host || start_frames.size()>=5 || frame.size()>CrossplaySessionConfig::Header+CrossplaySessionConfig::MaximumWire)return false;
                start_frames.push_back(std::move(frame));continue;
            }
            if(start_started)return false;
            if(frame.size()>=8 && !memcmp(frame.data(),CrossplayNeoSelection::Magic,8)){
                if(launch.host || !neo_installed || neo_selections.size()>=4 || frame.size()!=CrossplayNeoSelection::FrameSize)return false;
                neo_selections.push_back(std::move(frame));continue;
            }
            if(launch.host){
                std::string choice;if(!NeoSelectionCurrent(choice))return false;
                if(ready_requests.size()>=4 || !CrossplayReady::Decode(peer,frame,admission.PeerSourceConnection(),ids.guest,wire))return false;
                ready_requests.push_back(std::move(wire));
            }else{
                CrossplayReadyState::Snapshot value;
                if(ready_states.size()>=4 || !CrossplayReadyState::Decode(peer,frame,ready_received,value))return false;
                ready_received=value.revision;ready_states.push_back(value);
            }
        }
        if(start_started)return PumpStartFrames() && PumpSetupRoot();
        if(!PumpNeoSelection())return false;
        const auto now=GetTickCount64();
        if(launch.host){
            if(ready_pending.configuration){
                const auto observed=ObserveReadyToggle(original,ready_pending);
                if(observed==ReadyObservation::Invalid)return false;
                if(observed==ReadyObservation::Pending)return now-ready_started<=10000;
                CrossplayReadyState::Snapshot value;
                if(ready_sent==UINT32_MAX || !CaptureGoldReadySnapshot(original,ready_pending,ready_sent+1,value)
                    || !CrossplayReadyState::Encode(peer,value,frame) || !channel.Send(frame))return false;
                ++ready_sent;ready_pending={};
            }
            if(!ready_requests.empty()){
                if(!EnqueueReadyToggle(launch.channel.edition,original,ids,ready_requests.front(),ready_pending))return false;
                ready_requests.pop_front();ready_started=now;
            }
        }else{
            if(ready_projection.session){
                const auto observed=ObserveHistoryReadySnapshot(launch.channel.edition,original,ids,ready_projection);
                if(observed==ReadyObservation::Invalid)return false;
                if(observed==ReadyObservation::Pending)return now-projection_started<=10000;
                ready_projection={};
            }
            if(!ready_states.empty()){
                uint32_t flags=0;std::string map_template;
                if(!AppliedGuestChoices(flags,map_template)
                    || !ApplyHistoryReadySnapshot(launch.channel.edition,original,ids,ready_states.front(),ready_projection))return false;
                ready_states.pop_front();projection_started=now;
            }
        }
        return PumpStartFrames() && PumpSetupRoot();
    }
    void ConfigurationConsumed(void* cache,const unsigned char* event){
        if(Owned() && phase==Phase::RosterBound && StartConfigurationConsumed(cache,event))return;
        if(!Owned() || launch.host || phase!=Phase::RosterBound || !configuration_enqueued || configuration_applied)return;
        using namespace CrossplayNativeCodec;
        CrossplayNative::LobbySnapshot lobby;std::vector<unsigned char> wire;
        const auto ids=LocalConnections();const auto pin=SessionConfigDetail::Pin(launch.channel.edition);
        if(reinterpret_cast<uintptr_t>(cache)!=configuration_interval.cache
            || !original.CaptureLobby(lobby) || lobby.instance!=session
            || !SessionConfigDetail::Roster(original,ids,false)
            || !SessionConfigDetail::Source(pin,event,ids) || !Detail::Encode(pin,event,wire)
            || !configuration_receipt.Consumed({owner,lobby.instance,reinterpret_cast<uintptr_t>(cache)},wire,GetTickCount64()))Fail();
    }
    bool Configure(){
        if(configuration_applied || (launch.host && configuration_sent))return true;
        const auto now=GetTickCount64();if(!configuration_started)configuration_started=now;
        if(now-configuration_started>10000 || bootstrap.saved)return false; // Saved configuration has a separate source-bound protocol.
        const auto* peer=channel.GetPeer();if(!peer)return false;
        const auto ids=LocalConnections();std::vector<unsigned char> wire;
        if(launch.host){
            if(!CrossplayNativeCodec::CaptureGoldSessionConfig(original,ids,wire))return true;
            CrossplaySessionConfig::Layout layout;std::vector<unsigned char> frame;
            if(!CrossplaySessionConfig::Inspect(wire,ids,layout) || layout.map!=bootstrap.map
                || !CrossplaySessionConfig::Encode(*peer,wire,ids,frame) || !channel.Send(frame))return false;
            configuration_sent=true;return true;
        }
        if(!guest_claim.Applied(original,admission))return true;
        if(!configuration_enqueued){
            std::vector<unsigned char> frame;if(!channel.Receive(frame))return true;
            uint32_t own_at_peer=UINT32_MAX;
            if(!CrossplayNative::ProjectConnection(peer->edition,admission.PeerSourceConnection(),peer->local_in_peer_namespace,own_at_peer))return false;
            const CrossplaySessionConfig::Connections source{admission.PeerSourceConnection(),own_at_peer};
            auto* cache=CrossplayNativeCodec::ReadyStateDetail::HistoryConfiguration(launch.channel.edition);
            if(!cache || !CrossplaySessionConfig::Decode(*peer,frame,source,ids,wire))return false;
            configuration_interval={owner,session,reinterpret_cast<uintptr_t>(cache)};
            if(!configuration_receipt.Arm(configuration_interval,ids,bootstrap.map,wire,now))return false;
            // Arm before the original clone operation, including a synchronous consumer.
            configuration_enqueued=true;
            if(!CrossplayNativeCodec::EnqueueSessionConfig(launch.channel.edition,original,ids,wire))return false;
        }
        if(!configuration_receipt.HasConsumed())return true;
        if(configuration_readiness_queued){
            const auto ready=CrossplayNativeCodec::ObserveHistoryReadySnapshot(launch.channel.edition,original,ids,configuration_readiness);
            if(ready==CrossplayNativeCodec::ReadyObservation::Invalid)return false;
            if(ready==CrossplayNativeCodec::ReadyObservation::Pending)return true;
        }
        if(!CrossplayConfigurationNative::Snapshot(launch.channel.edition,original,ids,configuration_interval,wire))return false;
        CrossplayReadyState::Snapshot observed_ready;
        const auto difference=CrossplayConfigurationReadiness::Compare(configuration_receipt.Expected(),wire,ids,observed_ready);
        if(difference==CrossplayConfigurationReadiness::Difference::Invalid)return false;
        if(difference==CrossplayConfigurationReadiness::Difference::HostOnly){
            if(configuration_readiness_queued
                || !CrossplayNativeCodec::ApplyHistoryReadySnapshot(launch.channel.edition,original,ids,observed_ready,configuration_readiness))return false;
            configuration_readiness_queued=true;return true;
        }
        const auto observed=configuration_receipt.Observe(configuration_interval,wire,GetTickCount64());
        if(observed==CrossplayConfigurationReceipt::Observation::Invalid)return false;
        configuration_applied=observed==CrossplayConfigurationReceipt::Observation::Applied;
        return true;
    }
    void Pump(){
        if(!Owned() || phase==Phase::Dormant || phase==Phase::Closed || phase==Phase::Failed)return;
        if(!channel.Poll() || (outgoing_installed && OutgoingObserver::failed)
            || (gold_local_installed && CrossplayNativeObserver::GoldLocalObserver::Current().failed)
            || (start_hold_installed && CrossplayNativeStart::GoldStartHold::Current().failed)){Fail();return;}
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
                    if(!guest_claim.Begin(launch.channel.edition,original,admission,captured_player.principal,captured_castle,extra)){Fail();return;}
                    if(!CrossplayNative::ProjectHistoryGuestHost(launch.channel.edition,original,admission)){Fail();return;}
                    backend_projected=true;
                }
                phase=Phase::RosterBound;
                if(!outgoing_installed){
                    if(!OutgoingObserver::Install(launch.channel.edition,owner,&CaptureOutgoing)){Fail();return;}
                    outgoing_installed=true;
                }
                if(launch.host && !gold_local_installed){
                    if(!CrossplayNativeObserver::GoldLocalObserver::InstallBound(original,admission,&CaptureGoldLocal)){Fail();return;}
                    gold_local_installed=true;
                }
                if(launch.host && !start_hold_installed){
                    if(!CrossplayNativeStart::GoldStartHold::InstallBound(original,admission,&CaptureGoldStart)){Fail();return;}
                    start_hold_installed=true;
                }
                if(!neo_installed && !InstallNeo()){Fail();return;}
                if(!Configure()){Fail();return;}
                if(!Readiness()){Fail();return;}
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
void CaptureConfiguration(void* cache,const unsigned char* event){
    if(current)try{current->ConfigurationConsumed(cache,event);}catch(...){current->Fail();}
}
void CaptureOutgoing(void* manager,const void* event,uint32_t mode,uint32_t target){
    if(current)try{current->Outgoing(manager,event,mode,target);}catch(...){current->Fail();}
}
void __stdcall CaptureGoldLocal(void* manager,const void* event,uint32_t mode){
    if(current)try{current->GoldLocal(manager,event,mode);}catch(...){current->Fail();}
}
bool __stdcall CaptureGoldStart(void* manager,const void* event){
    if(current)try{return current->HoldGoldStart(manager,event);}catch(...){current->Fail();}return false;
}
bool NeoWorldReady(){
    return current && current->Owned() && current->phase==Phase::RosterBound && current->neo_world_queued
        && current->neo_world_agreement.Ready() && current->NeoModules()
        && CrossplayNativeCodec::LoadProgressDetail::World(current->original,current->owner,current->neo_world_session,
            current->admission.OwnConnection(),current->admission.PeerLocalConnection());
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
    if(!launch->host){
        if(!ConfigurationObserver::Install(launch->channel.edition,GetCurrentThreadId(),&CaptureConfiguration)){
            SessionObserver::Release();return false;
        }
        next->configuration_observer_installed=true;
    }
    next->timer=SetTimer(nullptr,0,20,&Tick);
    if(!next->timer){
        if(next->configuration_observer_installed)ConfigurationObserver::Release();
        SessionObserver::Release();return false;
    }
    next->phase=Phase::Connecting;current=next.release();return true;
}
void PumpRuntime(){ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    if(current)try{current->Pump();}catch(...){current->Fail();}
}
bool CloseRuntime(){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    if(!current || !current->Close())return false;
    if(current->start_hold_installed){
        if(!CrossplayNativeStart::GoldStartHold::Release())return false;current->start_hold_installed=false;
    }
    if(current->gold_local_installed){
        if(!CrossplayNativeObserver::GoldLocalObserver::Release())return false;current->gold_local_installed=false;
    }
    if(current->outgoing_installed){
        if(!OutgoingObserver::Release())return false;
        current->outgoing_installed=false;
    }
    if(current->configuration_observer_installed){
        if(!ConfigurationObserver::Release())return false;
        current->configuration_observer_installed=false;
    }
    if(!SessionObserver::Release())return false;
    delete current;current=nullptr;return true;
}
Phase RuntimePhase(){return current && current->Owned()?current->phase:Phase::Dormant;}
std::string BootstrapMap(){
    return current && current->Owned() && !current->launch.host && current->bootstrap_received
        && current->phase!=Phase::Failed && current->phase!=Phase::Closed?current->bootstrap.map:std::string();
}
bool BootstrapChoices(uint32_t& flags,std::string& map_template){
    flags=0;map_template.clear();
    if(!current || !current->Owned() || current->launch.host || !current->bootstrap_received
        || current->phase==Phase::Failed || current->phase==Phase::Closed || !CrossplayBootstrap::Valid(current->bootstrap))return false;
    flags=current->bootstrap.flags;map_template=current->bootstrap.map_template;return true;
}
bool AppliedGuestChoices(uint32_t& flags,std::string& map_template){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    flags=0;map_template.clear();
    if(!current || !current->Owned() || current->launch.host || !current->configuration_applied
        || current->phase!=Phase::RosterBound || !CrossplayBootstrap::Valid(current->bootstrap)
        || !current->guest_claim.Applied(current->original,current->admission))return false;
    CrossplayHistoryChoices::Value observed;
    if(CrossplayHistoryChoices::Capture(current->launch.channel.edition,current->original,current->LocalConnections(),false,
            current->configuration_interval,observed)!=CrossplayHistoryChoices::Observation::Captured
        || observed.map!=current->bootstrap.map || observed.flags!=current->bootstrap.flags
        || observed.map_template!=current->bootstrap.map_template)return false;
    flags=observed.flags;map_template=std::move(observed.map_template);return true;
}
bool GuestPresentationBound(){
    uint32_t flags=0;std::string map_template;
    return AppliedGuestChoices(flags,map_template);
}
bool NeoCommand(const std::string& command,std::string& output){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;output.clear();
    if(!current || !current->Owned() || current->phase==Phase::Failed || current->phase==Phase::Closed)return false;
    if(command=="capabilities"){
        if(!current->NeoModules())return false;output="NEO-FIFO-INTENT-1";return true;
    }
    if(command=="stage:0" || command=="stage:1"){
        if(!current->launch.host || current->neo_world_queued || current->bootstrap.saved || current->start_started)return false;
        const bool choice=command.back()=='1';
        if(current->phase==Phase::RosterBound){
            Crossplay::Digest context{};bool guest_ready=false;
            if(current->ready_pending.configuration || !current->ready_requests.empty()
                || current->NeoContext(context,guest_ready)!=CrossplayGoldChoices::Observation::Captured || guest_ready)return false;
        }
        if(!current->neo_staged || current->neo_choice!=choice){current->neo_dirty=true;current->neo_selection_started=0;}
        current->neo_staged=true;current->neo_choice=choice;output="1";return true;
    }
    if(command=="network-state" || command=="selection")return current->NeoSelectionCurrent(output);
    // World/start and selected-save commands remain unavailable until their
    // original native delivery/source-bound lifecycle has been connected.
    return false;
}
}
