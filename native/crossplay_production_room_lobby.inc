    CrossplayNative::RoomLobbyAdmission room_admission;
    CrossplayConfigurationReceipt::Interval room_configuration_interval;
    CrossplaySessionConfig::RoomConnections room_local_routes;
    std::vector<unsigned char> room_offer,room_expected_guest;
    unsigned room_receipts=0;
    CrossplayNativeCodec::PendingHistoryRoomReady room_ready_projection;
    bool room_consumer_seen=false,room_guest_receipt_sent=false,room_configuration_complete=false;
    bool room_primed=false;
    ULONGLONG room_configuration_started=0;
    unsigned char room_changed_ready_slot=0;
    std::deque<Crossplay::RoomEvent> room_ready_requests;
    std::vector<unsigned char> room_ready_before,room_ready_after;
    std::vector<unsigned char> room_held_start;
    std::vector<unsigned char> room_held_setup;
    ULONGLONG room_ready_started=0;
    enum class TeamPhase { Idle, AwaitingReceipts, Applying, AwaitingCommit, Confirmed };
    TeamPhase room_team_phase=TeamPhase::Idle;
    CrossplayRoomTeams::Teams room_teams{};
    Crossplay::Digest room_team_config{};
    unsigned room_team_receipts=0;
    ULONGLONG room_team_started=0;
    uint32_t room_ai_slot=0;
    CrossplayRoomAI::Value room_ai{};
    CrossplayAIProfile::ContextSnapshot room_ai_context{};
    bool room_ai_applied=false;

    bool ValidateRoomSnapshot(const std::vector<unsigned char>& wire) {
        CrossplaySessionConfig::Layout layout;
        return CrossplaySessionConfig::InspectRoom(wire,room_local_routes,layout) && layout.map==launch.room.map;
    }

    bool ReadRoomSnapshot(std::vector<unsigned char>& wire) {
        return CrossplayConfigurationNative::SnapshotRoom(launch.channel.edition,original,room_admission,
            room_local_routes,room_configuration_interval,wire) && ValidateRoomSnapshot(wire);
    }

    bool ReadRoomAIContext(CrossplayAIProfile::ContextSnapshot& context,unsigned* stage) {
        using namespace CrossplayNativeCodec;
        context={};
        if(stage)*stage=1;
        std::vector<unsigned char> wire;
        if(!ReadRoomSnapshot(wire))return false;
        auto* cache=CrossplayConfigurationNative::Cache(launch.channel.edition);
        const auto offset=launch.channel.edition==Crossplay::Edition::Gold?0x48:0x44;
        if(!cache || reinterpret_cast<uintptr_t>(cache)!=room_configuration_interval.cache
            || !Detail::Readable(cache+offset,sizeof(uintptr_t)))return false;
        CrossplayAIProfile::CacheLock lock(launch.channel.edition,cache);
        if(!lock.acquired || CrossplayConfigurationNative::Cache(launch.channel.edition)!=cache)return false;
        const auto slots=*reinterpret_cast<const uintptr_t*>(cache+offset);
        if(!Detail::Readable(reinterpret_cast<const void*>(slots),8*0x18))return false;
        context={session,reinterpret_cast<uintptr_t>(cache),slots};
        if(stage)*stage=0;
        return true;
    }

    bool RoomAIRows(unsigned char*& rows,CrossplayAIProfile::ContextSnapshot& context) {
        rows=nullptr;
        CrossplayAIPreparationSource::View view;
        unsigned stage=0;
        if(!room_ai_slot || !ReadRoomAIContext(context,nullptr) || !CrossplayAIProfile::Same(context,room_ai_context)
            || !CrossplayAIPreparationSource::CaptureView(launch.channel.edition,room_local_routes.capacity,view,stage)
            || view.rows!=view.getrows || CrossplayAIPreparationSource::ReadRow(view,room_ai_slot,true).controller!=1
            || !view.Stable())return false;
        rows=view.rows;
        return true;
    }

    bool RoomAIMatches(const std::vector<unsigned char>& observed) {
        if(!room_ai_slot)return true;
        uint32_t slot=0;
        CrossplayRoomAI::Value selected;
        return room_ai_applied && CrossplayRoomAI::Seat(observed,room_local_routes,slot) && slot==room_ai_slot
            && CrossplayRoomAI::ReadSelected(launch.channel.edition,room_local_routes.capacity,slot,selected)
            && CrossplayRoomAI::Same(selected,room_ai);
    }

    bool PumpRoomAI(const Crossplay::RoomSnapshot& room,const std::vector<unsigned char>& observed) {
        if(!room_ai_slot || room_ai_applied)return true;
        if(!room_ai.slot){
            Crossplay::RoomEvent event;
            if(!channel.ReceiveRoom(event))return true;
            Crossplay::Digest config{};
            if(event.source_slot!=room.host_slot || !CrossplaySetupSignatureNative::Hash(room_offer,config)
                || !CrossplayRoomAI::Decode(event.bytes,config,room_ai_slot,room_ai))return false;
            std::vector<unsigned char> wire(CrossplayAIProfile::WireSize);
            CrossplayChat::Put(wire.data(),CrossplayAIProfile::Class);
            CrossplayChat::Put(wire.data()+4,CrossplayAIProfile::Type);
            CrossplayChat::Put(wire.data()+8,room_local_routes.host);
            wire[12]=1;
            CrossplayChat::Put(wire.data()+13,room_ai.slot);
            CrossplayChat::Put(wire.data()+17,room_ai.profile);
            return CrossplayAIProfile::DeliverBound(launch.channel.edition,room_local_routes.host,wire,room_ai_context,
                [&](CrossplayAIProfile::ContextSnapshot& view,unsigned* stage){return ReadRoomAIContext(view,stage);});
        }
        uint32_t native_slot=0;
        if(!CrossplayRoomAI::Seat(observed,room_local_routes,native_slot))return false;
        if(native_slot!=room_ai_slot)return true;
        CrossplayRoomAI::Value selected;
        if(CrossplayRoomAI::ReadSelected(launch.channel.edition,room_local_routes.capacity,native_slot,selected)
            && CrossplayRoomAI::Same(selected,room_ai)){
            room_ai_applied=true;
            return true;
        }
        unsigned stage=0;
        if(!CrossplayAIPreparation::ApplyBound(launch.channel.edition,room_ai,
            [&](unsigned char*& rows,CrossplayAIProfile::ContextSnapshot& view){return RoomAIRows(rows,view);},stage))return false;
        room_ai_applied=true;
        return true;
    }

    bool ReadRoomTeams(CrossplayRoomTeams::Teams& teams) {
        return CrossplayRoomTeams::Capture(launch.channel.edition,original,room_admission,
            room_local_routes,session,teams,room_ai_slot);
    }
    bool RoomSetupConfigurationCurrent() {
        std::vector<unsigned char> observed,expected;
        if(!ReadRoomSnapshot(observed) || !RoomAIMatches(observed))return false;
        if(!launch.host)return observed==room_expected_guest;
        CrossplaySessionConfig::RoomConnections source;
        return CrossplayRoomConfiguration::Decode(room_offer,source,expected)
            && source.host==room_local_routes.host && source.stations==room_local_routes.stations && observed==expected;
    }

    bool BeginRoomTeams(const Crossplay::RoomSnapshot& room) {
        if(!launch.host || room_team_phase!=TeamPhase::Idle || !RoomAllHumansReadyObserved()
            || !RoomSetupConfigurationCurrent())return false;
        std::vector<unsigned char> frame;
        if(!CrossplaySetupSignatureNative::Hash(room_offer,room_team_config) || !ReadRoomTeams(room_teams)
            || !CrossplayRoomTeams::Frame(CrossplayRoomTeams::Offer,room_team_config,room_local_routes,room_teams,frame,room_ai_slot))return false;
        if(!channel.BroadcastRoom(frame)){Fail();return false;}
        room_team_receipts=1u<<(room.local_slot-1);
        room_team_started=GetTickCount64();
        room_team_phase=TeamPhase::AwaitingReceipts;
        return true;
    }

    bool ReceiveRoomTeams(const Crossplay::RoomEvent& event,const Crossplay::RoomSnapshot& room) {
        if(launch.host || room_team_phase!=TeamPhase::Idle || event.source_slot!=room.host_slot
            || !RoomAllHumansReadyObserved() || !RoomSetupConfigurationCurrent()
            || !CrossplaySetupSignatureNative::Hash(room_offer,room_team_config)
            || !CrossplayRoomTeams::Decode(CrossplayRoomTeams::Offer,event.bytes,room_team_config,room_local_routes,room_teams,room_ai_slot)
            || !CrossplayRoomTeams::Deliver(launch.channel.edition,original,room_admission,room_local_routes,session,room_teams,room_ai_slot))return false;
        room_team_started=GetTickCount64();
        room_team_phase=TeamPhase::Applying;
        return true;
    }

    bool PumpRoomTeams(const Crossplay::RoomSnapshot& room) {
        if(GetTickCount64()-room_team_started>10000 || !RoomSetupConfigurationCurrent())return false;
        Crossplay::RoomEvent event;
        CrossplayRoomTeams::Teams observed;
        std::vector<unsigned char> frame;
        if(room_team_phase==TeamPhase::AwaitingReceipts) {
            while(channel.ReceiveRoom(event)){
                if(!event.source_slot || event.source_slot>room.member_count
                    || (room_team_receipts&(1u<<(event.source_slot-1)))
                    || !CrossplayRoomTeams::Decode(CrossplayRoomTeams::Applied,event.bytes,room_team_config,room_local_routes,observed,room_ai_slot)
                    || observed!=room_teams)return false;
                room_team_receipts|=1u<<(event.source_slot-1);
            }
            if(room_team_receipts!=((1u<<room.member_count)-1))return true;
            if(!ReadRoomTeams(observed) || observed!=room_teams
                || !CrossplayRoomTeams::Frame(CrossplayRoomTeams::Committed,room_team_config,room_local_routes,room_teams,frame,room_ai_slot)
                || !channel.BroadcastRoom(frame))return false;
            room_team_phase=TeamPhase::Confirmed;
            return true;
        }
        if(room_team_phase==TeamPhase::Applying){
            if(!ReadRoomTeams(observed) || observed!=room_teams)return true;
            if(!CrossplayRoomTeams::Frame(CrossplayRoomTeams::Applied,room_team_config,room_local_routes,room_teams,frame,room_ai_slot)
                || !channel.SendRoom(frame,room.host_slot))return false;
            room_team_phase=TeamPhase::AwaitingCommit;
            return true;
        }
        if(!channel.ReceiveRoom(event))return true;
        if(event.source_slot!=room.host_slot
            || !CrossplayRoomTeams::Decode(CrossplayRoomTeams::Committed,event.bytes,room_team_config,room_local_routes,observed,room_ai_slot)
            || observed!=room_teams || !ReadRoomTeams(observed) || observed!=room_teams)return false;
        room_team_phase=TeamPhase::Confirmed;
        return true;
    }

    bool GoldHostedReadyRoom(const Crossplay::RoomSnapshot& room) const {
        if(!room.host_slot || room.host_slot>room.member_count
            || room.members[room.host_slot-1].edition!=Crossplay::Edition::Gold)return false;
        for(unsigned i=0;i<room.member_count;++i)
            if(i+1!=room.host_slot && room.members[i].edition==Crossplay::Edition::Gold)return false;
        return true;
    }

    void RoomOutgoing(void* manager,const void* event,uint32_t mode,uint32_t target) {
        using namespace CrossplayNativeCodec;
        if(!Owned() || launch.host || !room_primed || phase!=Phase::RosterBound || room_team_phase!=TeamPhase::Idle)return;
        const auto* room=channel.GetRoom();
        if(!room || !GoldHostedReadyRoom(*room))return;
        const auto edition=launch.channel.edition;
        if(!Detail::Readable(event,4) || *static_cast<const uintptr_t*>(event)!=ReadyDetail::Pin(edition).event_vt)return;
        CrossplayNative::RoomRoute own,host;
        if(!room_admission.Resolve(room->local_slot,false,own) || !own.local || own.host
            || !room_admission.Resolve(room->host_slot,false,host) || host.local || !host.host){Fail();return;}
        if(mode!=2 && (mode!=1 || target!=host.receiver_cid))return;
        uintptr_t send=0;
        std::vector<unsigned char> intent,frame;
        if(manager!=Detail::Transport(edition,send) || !ReadyDetail::EncodeIntent(edition,own.receiver_cid,event,intent)
            || !CrossplayReady::Encode(host.peer,intent,own.receiver_cid,frame)
            || !channel.SendRoom(frame,room->host_slot))Fail();
    }

    bool QueueRoomReady(const Crossplay::RoomEvent& event) {
        CrossplayNative::RoomRoute guest;
        std::vector<unsigned char> intent;
        if(room_ready_requests.size()>=Crossplay::RoomSnapshot::MaximumMembers
            || !room_admission.Resolve(event.source_slot,false,guest) || guest.local || guest.host
            || !CrossplayReady::Decode(guest.peer,event.bytes,guest.source_cid,guest.receiver_cid,intent))return false;
        room_ready_requests.push_back(event);
        return true;
    }

    bool BeginRoomReady(const Crossplay::RoomEvent& event) {
        using namespace CrossplayNativeCodec;
        const auto* original_session=reinterpret_cast<const unsigned char*>(session);
        if(!Detail::Readable(original_session,0x34) || CrossplayChat::Word(original_session+0x30)!=2)return false;
        CrossplayNative::RoomRoute guest;
        CrossplaySessionConfig::RoomConnections source;
        CrossplaySessionConfig::Layout layout;
        std::vector<unsigned char> intent,previous,native;
        if(!room_admission.Resolve(event.source_slot,false,guest) || guest.local || guest.host
            || !CrossplayReady::Decode(guest.peer,event.bytes,guest.source_cid,guest.receiver_cid,intent)
            || !CrossplayRoomConfiguration::Decode(room_offer,source,previous) || source.capacity!=room_local_routes.capacity
            || source.host!=room_local_routes.host || source.stations!=room_local_routes.stations
            || !CrossplaySessionConfig::InspectRoom(previous,source,layout) || !ReadRoomSnapshot(native) || native!=previous
            || !guest.player_slot || guest.player_slot>source.capacity
            || source.stations[guest.player_slot-1]!=guest.receiver_cid)return false;
        const auto ready_at=layout.slots_at+(guest.player_slot-1)*CrossplaySessionConfig::SlotBytes;
        auto desired=previous;
        desired[ready_at]=static_cast<unsigned char>(!previous[ready_at]);
        intent.resize(CrossplayReady::NativeStateSize);
        CrossplayChat::Put(intent.data()+13,guest.player_slot);
        intent[17]=desired[ready_at];
        if(!ReadyDetail::EnqueueState(Crossplay::Edition::Gold,intent,guest.receiver_cid,guest.player_slot))return false;
        room_ready_before=std::move(previous);
        room_ready_after=std::move(desired);
        room_ready_started=GetTickCount64();
        return true;
    }

    bool ReceiveRoomOffer(Crossplay::RoomEvent& event,const Crossplay::RoomSnapshot& room) {
        if(event.source_slot!=room.host_slot)return false;
        CrossplaySessionConfig::RoomConnections source;
        std::vector<unsigned char> wire,expected;
        if(!CrossplayRoomConfiguration::Decode(event.bytes,source,wire) || source.capacity!=launch.room.capacity
            || !CrossplaySessionConfig::ProjectRoom(wire,source,room_local_routes,expected)
            || !ValidateRoomSnapshot(expected))return false;
        uint32_t cpu=0;
        if(!CrossplayRoomAI::Seat(expected,room_local_routes,cpu)
            || (cpu && (room.members[room.host_slot-1].edition!=Crossplay::Edition::Gold
                         || launch.channel.edition==Crossplay::Edition::Gold)))return false;
        room_changed_ready_slot=0;
        if(room_primed) {
            CrossplayConfigurationReadiness::RoomSeatReady change;
            if(cpu!=room_ai_slot || !CrossplayConfigurationReadiness::RoomReadyChange(room_expected_guest,expected,room_local_routes,change)
                || room_local_routes.stations[change.player_slot-1]==room_local_routes.host)return false;
            room_changed_ready_slot=change.player_slot;
        }
        room_ai_slot=cpu;
        room_expected_guest=std::move(expected);
        room_offer=std::move(event.bytes);
        room_consumer_seen=room_guest_receipt_sent=room_configuration_complete=false;
        room_ready_projection={};
        room_configuration_started=GetTickCount64();
        event.bytes=std::move(wire);
        return CrossplayNativeCodec::EnqueueRoomSessionConfig(room_admission,event,source,launch.room.capacity,launch.room.map);
    }

    bool HoldRoomGoldStart(const void* event) {
        using namespace CrossplayNativeCodec;
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
        if(room_saved_started)return HoldRoomSavedStart(event);
#endif
        if(!launch.host || !room_held_start.empty() || !RoomAllHumansReadyObserved())return false;
        if(room_team_phase!=TeamPhase::Idle){
            CrossplayRoomTeams::Teams actual;
            if(room_team_phase!=TeamPhase::Confirmed || !ReadRoomTeams(actual) || actual!=room_teams)return false;
        }
        const auto p=GameStartDetail::Pin(Crossplay::Edition::Gold);
        std::vector<unsigned char> wire;
        if(!GameStartDetail::Source(p,event,room_local_routes.host)
            || !Detail::Encode(p,event,wire) || !CrossplayGameStart::Inspect(wire,room_local_routes.host))return false;
        if(room_ai_slot && !CrossplaySetupRootCapture::CaptureRoom(launch.channel.edition,room_local_routes.capacity,
            [&]{return RoomTeamsConfirmed() && RoomAllHumansReadyObserved();},room_held_setup))return false;
        room_held_start=std::move(wire);
        return true;
    }

#include "crossplay_production_room_root.inc"
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
#include "crossplay_production_room_world.inc"
#include "crossplay_production_room_saved.inc"
#endif

    bool PumpRoomReady(const Crossplay::RoomSnapshot& room) {
        if(room_team_phase==TeamPhase::Confirmed){
            if(!room_ai_slot)return true;
            if(!PumpRoomRoot(room))return false;
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
            if(room_root_phase==RoomRootPhase::Confirmed)return PumpRoomStart(room);
#endif
            return true;
        }
        if(room_team_phase!=TeamPhase::Idle)return PumpRoomTeams(room);
        if(!GoldHostedReadyRoom(room))return true;
        Crossplay::RoomEvent event;
        if(!launch.host) {
            if(!outgoing_installed) {
                if(!OutgoingObserver::Install(launch.channel.edition,owner,&CaptureOutgoing))return false;
                outgoing_installed=true;
            }
            if(!channel.ReceiveRoom(event))return true;
            if(event.bytes.size()>=sizeof(CrossplayRoomTeams::Offer)
                && !memcmp(event.bytes.data(),CrossplayRoomTeams::Offer,sizeof(CrossplayRoomTeams::Offer)))
                return ReceiveRoomTeams(event,room);
            return ReceiveRoomOffer(event,room);
        }
        if(!start_hold_installed){
            if(!CrossplayNativeStart::GoldStartHold::InstallRoomBound(original,room_admission,room.local_slot,&CaptureRoomGoldStart))return false;
            start_hold_installed=true;
        }
        if(!room_held_start.empty())return true;
        while(channel.ReceiveRoom(event))
            if(!QueueRoomReady(event))return false;
        if(room_ready_after.empty()) {
            if(room_ready_requests.empty())return true;
            if(!BeginRoomReady(room_ready_requests.front()))return false;
            room_ready_requests.pop_front();
            return true;
        }
        const auto now=GetTickCount64();
        std::vector<unsigned char> readback;
        if(now-room_ready_started>10000)return false;
        if(!ReadRoomSnapshot(readback) || readback==room_ready_before)return true;
        if(readback!=room_ready_after || !CrossplayRoomConfiguration::Encode(room_local_routes,readback,room_offer)
            || !channel.BroadcastRoom(room_offer))return false;
        room_ready_before.clear();room_ready_after.clear();
        room_receipts=1u<<(room.local_slot-1);
        room_configuration_complete=false;
        room_configuration_started=now;
        return true;
    }

    void RoomConfigurationConsumed(void* cache,const unsigned char* event) {
        using namespace CrossplayNativeCodec;
        if(launch.host || phase!=Phase::RosterBound || room_expected_guest.empty())return;
        if(reinterpret_cast<uintptr_t>(cache)!=room_configuration_interval.cache){Fail();return;}
        const auto p=SessionConfigDetail::Pin(launch.channel.edition);
        std::vector<unsigned char> wire,canonical;
        if(!SessionConfigDetail::RoomSource(p,event,room_local_routes) || !Detail::Encode(p,event,wire)
            || !CrossplaySessionConfig::ProjectRoom(wire,room_local_routes,room_local_routes,canonical,true)) {
            Fail();
            return;
        }
        if(canonical==room_expected_guest)room_consumer_seen=true;
    }

    bool PumpRoomConfiguration(const Crossplay::RoomSnapshot& room) {
        const auto now=GetTickCount64();
        if(!room_configuration_started)room_configuration_started=now;
        if(room_configuration_complete)return true;
        bool saved=false;
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
        saved=room_saved_started || (launch.host && !resume_ui_token.empty());
#endif
        if(!saved && now-room_configuration_started>10000)return false;
        if(!room_configuration_interval.cache) {
            const auto cache=CrossplayConfigurationNative::Cache(launch.channel.edition);
            if(!cache)return now-room_configuration_started<=10000;
            if(!room_admission.CaptureConfigurationRoutes(launch.room.capacity,room_local_routes))return false;
            room_configuration_interval={owner,session,reinterpret_cast<uintptr_t>(cache)};
        }
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
        if(saved)return PumpRoomSaved(room);
#endif
        std::vector<unsigned char> readback;
        if(launch.host) {
            if(room_offer.empty()) {
                if(!ReadRoomSnapshot(readback))return true;
                if(!CrossplayRoomAI::Seat(readback,room_local_routes,room_ai_slot))return false;
                if(room_ai_slot && launch.channel.edition!=Crossplay::Edition::Gold)return false;
                if(room_ai_slot && !CrossplayRoomAI::ReadSelected(launch.channel.edition,room_local_routes.capacity,room_ai_slot,room_ai))return true;
                if(!CrossplayRoomConfiguration::Encode(room_local_routes,readback,room_offer))return false;
                std::vector<unsigned char> ai_offer;
                if(room_ai_slot){
                    Crossplay::Digest config{};
                    if(!CrossplaySetupSignatureNative::Hash(room_offer,config)
                        || !CrossplayRoomAI::Frame(room_ai,config,ai_offer))return false;
                    room_ai_applied=true;
                }
                if(!channel.BroadcastRoom(room_offer) || (!ai_offer.empty() && !channel.BroadcastRoom(ai_offer)))return false;
                room_receipts=1u<<(room.local_slot-1);
                return true;
            }
            Crossplay::RoomEvent event;
            while(channel.ReceiveRoom(event)) {
                constexpr unsigned char ready_magic[]={'S','7','R','E','A','D','Y',1};
                if(event.bytes.size()>=sizeof(ready_magic) && !memcmp(event.bytes.data(),ready_magic,sizeof(ready_magic))) {
                    if(!room_primed || !GoldHostedReadyRoom(room) || !QueueRoomReady(event))return false;
                    continue;
                }
                if(!event.source_slot || event.source_slot>room.member_count || event.source_slot==room.host_slot
                    || (room_receipts&(1u<<(event.source_slot-1)))
                    || !CrossplayRoomConfiguration::MatchesReceipt(room_offer,event.bytes))return false;
                room_receipts|=1u<<(event.source_slot-1);
            }
            if(room_receipts!=(1u<<room.member_count)-1)return true;
            std::vector<unsigned char> verified,commit;
            if(!ReadRoomSnapshot(readback)
                || !RoomAIMatches(readback)
                || !CrossplayRoomConfiguration::Acknowledge(room_offer,room_local_routes,readback,verified)
                || !CrossplayRoomConfiguration::Commit(room_offer,verified,commit)
                || !channel.BroadcastRoom(commit))return false;
            room_configuration_complete=room_primed=true;
            return true;
        }
        if(room_offer.empty()) {
            Crossplay::RoomEvent event;
            if(!channel.ReceiveRoom(event))return true;
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
            if(Magic(event.bytes,RoomSavedOffer))return ReceiveRoomSavedConfiguration(event);
#endif
            return ReceiveRoomOffer(event,room);
        }
        if(room_guest_receipt_sent){
            Crossplay::RoomEvent event;
            if(!channel.ReceiveRoom(event))return true;
            if(event.source_slot!=room.host_slot || !CrossplayRoomConfiguration::MatchesCommit(room_offer,event.bytes)
                || !ReadRoomSnapshot(readback) || readback!=room_expected_guest)return false;
            if(!RoomAIMatches(readback))return false;
            room_configuration_complete=room_primed=true;
            return true;
        }
        if(!room_consumer_seen || !ReadRoomSnapshot(readback))return true;
        if(room_ai_slot && !room_ai_applied)return PumpRoomAI(room,readback);
        if(!RoomAIMatches(readback))return false;
        using namespace CrossplayNativeCodec;
        if(room_ready_projection.session) {
            const auto status=ObserveHistoryRoomReady(launch.channel.edition,original,room_admission,
                room_local_routes,room_ready_projection);
            if(status==ReadyObservation::Invalid)return false;
            if(status!=ReadyObservation::Applied || readback!=room_expected_guest)return true;
        } else if(readback!=room_expected_guest) {
            // Project only the held seat, after the original consumer saw the
            // exact offer. Any other change still blocks the receipt.
            if(launch.channel.edition==Crossplay::Edition::Gold)return true;
            CrossplayConfigurationReadiness::RoomSeatReady ready;
            if(room_changed_ready_slot) {
                if(!CrossplayConfigurationReadiness::RoomReadyChange(readback,room_expected_guest,room_local_routes,ready)
                    || ready.player_slot!=room_changed_ready_slot)return true;
            } else if(CrossplayConfigurationReadiness::CompareRoom(room_expected_guest,readback,room_local_routes,ready)
                    !=CrossplayConfigurationReadiness::Difference::HostOnly)return true;
            return ApplyHistoryRoomReady(launch.channel.edition,original,room_admission,room_local_routes,
                ready.player_slot,ready.value,room_ready_projection);
        }
        std::vector<unsigned char> receipt;
        if(!CrossplayRoomConfiguration::Acknowledge(room_offer,room_local_routes,readback,receipt)
            || !channel.SendRoom(receipt,room.host_slot))return false;
        room_guest_receipt_sent=true;
        return true;
    }

    bool PumpRoomLobby() {
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
        if(room_world_queued)return PumpRoomWorld();
#endif
        const auto* snapshot=channel.GetRoom();
        if(phase==Phase::Connecting) {
            if(!snapshot || !lobby_captured)return true;
            const auto* event=static_cast<const unsigned char*>(original.Get());
            if(!event || !InstallGoldSlot()
                || !room_admission.Begin(channel,snapshot->local_slot,launch.room.map_slots,event+0x44,extra))return false;
            if(!original.Reset())return false;
            phase=Phase::Admitting;
        }
        if(!room_admission.Poll())return false;
        if(room_admission.GetState()!=CrossplayNative::AdmissionState::RosterBound)return true;
        phase=Phase::RosterBound;
        snapshot=channel.GetRoom();
        if(!snapshot)return false;
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
        if(!neo_installed && !InstallNeo())return false;
#endif
        if(!launch.host && launch.channel.edition!=Crossplay::Edition::Gold && !backend_projected){
            if(!CrossplayNative::ProjectHistoryRoomGuestHost(original,room_admission,snapshot->local_slot,snapshot->host_slot))return false;
            backend_projected=true;
        }
        if(room_configuration_complete)return PumpRoomReady(*snapshot);
        return PumpRoomConfiguration(*snapshot);
    }
