    inline static constexpr unsigned char RoomSavedOffer[]={'S','7','R','S','A','V',0,1};
    static constexpr unsigned RoomSavedFrameBudget=16;
    CrossplayResumeSource::Selected room_saved_source;
    CrossplayResumeTransfer::Checkpoint room_saved_checkpoint;
    std::string room_saved_token;
    unsigned room_saved_requests=0;
    unsigned room_saved_ai_mask=0;
    std::array<CrossplaySavedAI::Row,2> room_saved_ai{};
    ULONGLONG room_saved_started=0,room_saved_selection_wait=0;
    bool room_saved_requested=false;
#include "crossplay_production_room_saved_file.inc"
#include "crossplay_production_room_saved_ready.inc"
#include "crossplay_production_room_saved_start.inc"

    bool CaptureRoomSavedSource(){
        if(!launch.host || resume_ui_token.empty())return false;
        const auto edition=launch.channel.edition;
        const bool gold=edition==Crossplay::Edition::Gold;
        CrossplayGoldChoices::Cache cache;
        if(gold && (!CrossplayGoldChoices::Methods()
            || CrossplayGoldChoices::Snapshot(cache)!=CrossplayGoldChoices::Observation::Captured
            || cache.map!=launch.room.map || cache.save!=resume_ui_token))return false;
        std::wstring path;
        std::vector<unsigned char> before,after_configuration;
        CrossplaySessionConfig::Layout layout;
        if(!CrossplayConfigurationNative::SnapshotRoom(edition,original,room_admission,room_local_routes,
                room_configuration_interval,before,resume_ui_token)
            || !CrossplaySessionConfig::RoomDetail::Inspect(before,room_local_routes,layout,false,resume_ui_token)
            || layout.map!=launch.room.map
            || !CrossplaySelectedSavedSource::Path(launch.profile,resume_ui_token,path)
            || !CrossplayResumeSource::CaptureSelected(path,launch.channel.content,layout.map,room_saved_source)
            || room_saved_source.archive.bytes!=resume_ui_archive.bytes
            || room_saved_source.archive.sha256!=resume_ui_archive.sha256
            || !room_saved_source.players_observed)return false;
        if(!CrossplaySavedAI::FromArchive(room_saved_source.players,room_saved_ai_mask,room_saved_ai,RoomHumanSeats()))return false;
        CrossplayGoldChoices::Cache after;
        if(gold && (CrossplayGoldChoices::Snapshot(after)!=CrossplayGoldChoices::Observation::Captured
            || !CrossplayGoldChoices::Same(cache,after)))return false;
        if(!CrossplayConfigurationNative::SnapshotRoom(edition,original,room_admission,room_local_routes,
            room_configuration_interval,after_configuration,resume_ui_token) || before!=after_configuration)return false;
        room_saved_token=resume_ui_token;
        room_saved_checkpoint=room_saved_source.checkpoint;
        return true;
    }

    bool SendRoomSavedConfiguration(const Crossplay::RoomSnapshot& room){
        if(!CaptureRoomSavedSource())return false;
        if(!outgoing_installed){
            if(!OutgoingObserver::Install(launch.channel.edition,owner,&CaptureOutgoing))return false;
            outgoing_installed=true;
        }
        if(!start_hold_installed){
            const auto edition=launch.channel.edition;
            const bool installed=edition==Crossplay::Edition::Gold
                ?CrossplayNativeStart::GoldStartHold::InstallRoomBound(original,room_admission,room.local_slot,&CaptureRoomGoldStart)
                :CrossplayNativeStart::HistoryStartHold::InstallRoomBound(original,room_admission,room.local_slot,
                    room_local_routes.capacity,launch.room.map,&CaptureRoomGoldStart,edition,room_saved_token);
            if(!installed)return false;
            start_hold_installed=true;
        }
        std::vector<unsigned char> captured;
        if(!CrossplayConfigurationNative::SnapshotRoom(launch.channel.edition,original,room_admission,
            room_local_routes,room_configuration_interval,captured,room_saved_token))return false;
        for(unsigned char slot=1;slot<=room.member_count;++slot){
            if(slot==room.local_slot)continue;
            CrossplayNative::RoomRoute guest;
            std::vector<unsigned char> payload,frame;
            if(!room_admission.Resolve(slot,false,guest) || guest.local || guest.host
                || !CrossplaySavedSessionConfig::EncodeRoom(guest.peer,room_saved_checkpoint,room_local_routes,
                    launch.room.map,room_saved_token,captured,payload)
                || !CrossplayRoomConfiguration::PackRoutes(RoomSavedOffer,room_local_routes,payload,frame)
                || !channel.SendRoom(frame,slot))return false;
        }
        room_saved_requests=1u<<(room.local_slot-1);
        room_saved_started=GetTickCount64();
        return true;
    }

    bool ReceiveRoomSavedConfiguration(Crossplay::RoomEvent& event){
        if(launch.host || room_saved_started || !room_offer.empty())return false;
        CrossplaySessionConfig::RoomConnections source;
        std::vector<unsigned char> bound;
        constexpr auto Maximum=CrossplaySavedSessionConfig::Header+CrossplaySessionConfig::MaximumWire;
        if(!CrossplayRoomConfiguration::UnpackRoutes(RoomSavedOffer,event.bytes,Maximum,source,bound))return false;
        if(!outgoing_installed){
            if(!OutgoingObserver::Install(launch.channel.edition,owner,&CaptureOutgoing))return false;
            outgoing_installed=true;
        }
        if(launch.channel.edition==Crossplay::Edition::Gold && !room_gold_sdk_ready_installed){
            const auto* room=channel.GetRoom();
            if(!room || !CrossplayNativeReady::GoldSdkReadyHold::InstallRoomBound(original,room_admission,room->local_slot,
                &CaptureRoomGoldSdkReady))return false;
            room_gold_sdk_ready_installed=true;
        }
        room_saved_started=GetTickCount64();
        event.bytes=std::move(bound);
        return CrossplayNativeCodec::SavedSessionConfigDetail::ReceiveRoom(room_admission,event,source,
            launch.channel.content,launch.room.capacity,launch.room.map,room_saved_checkpoint,room_saved_token,
            nullptr,&room_saved_initial_config);
    }

    bool PumpRoomSaved(const Crossplay::RoomSnapshot& room){
        if(!room_saved_started){
            if(!launch.host)return false;
            const auto now=GetTickCount64();
            if(!room_saved_selection_wait)room_saved_selection_wait=now;
            using namespace CrossplayNativeCodec;
            auto* cache=CrossplayConfigurationNative::Cache(launch.channel.edition);
            std::string token;
            if(!cache || !SessionConfigDetail::NativeString(cache+0x34,token) || token.empty())
                return now-room_saved_selection_wait<=30000;
            if(token!=resume_ui_token)return false;
            return SendRoomSavedConfiguration(room);
        }
        // The original sender prepares each guest's resident stream in
        // sequence. Every guest shares the budget for those transfers.
        const ULONGLONG timeout=240000ull*(room.member_count-1);
        Crossplay::RoomEvent event;
        if(!launch.host){
            for(unsigned budget=0;budget<RoomSavedFrameBudget && channel.ReceiveRoom(event);++budget){
                if(CrossplayWorldEvents::Identify(event.bytes)!=CrossplayWorldEvents::Kind::Unknown){
                    if(!BufferSavedWorldEvent(room,event))return false;
                }else if(Magic(event.bytes,CrossplayResumeStart::Magic)){
                    return ReceiveRoomSavedStart(room,event);
                }else if(Magic(event.bytes,CrossplaySavedReady::Magic)){
                    if(!ReceiveRoomSavedReady(room,event))return false;
                }else if(!ReceiveRoomSavedFile(room,event))return false;
            }
            return ObserveRoomSavedResidentStream(room)
                && PumpRoomSavedReadyGuest(room)
                && (room_saved_ready_acked || GetTickCount64()-room_saved_started<=timeout);
        }
        for(unsigned budget=0;budget<RoomSavedFrameBudget && channel.ReceiveRoom(event);++budget){
            CrossplayNative::RoomRoute guest;
            std::vector<unsigned char> wire;
            if(!RoomSavedRoute(room,event.source_slot,guest))return false;
            const unsigned bit=1u<<(event.source_slot-1);
            if(CrossplayWorldEvents::Identify(event.bytes)!=CrossplayWorldEvents::Kind::Unknown){
                if(!BufferSavedWorldEvent(room,event))return false;
            }else if(Magic(event.bytes,CrossplayResumeCredit::Magic)){
                if(!ReceiveRoomSavedCredit(guest,event))return false;
            }else if(Magic(event.bytes,CrossplaySavedReady::Magic)){
                if(!ReceiveRoomSavedReady(room,event))return false;
            }else if(Magic(event.bytes,CrossplayResumeAdmission::Magic)){
                CrossplayResumeAdmission::Kind kind{};
                if(!CrossplayResumeAdmission::Decode(guest.peer,room_saved_checkpoint,event.bytes,kind))return false;
                if(kind==CrossplayResumeAdmission::Kind::Queued){
                    if(!room_saved_start_at || (room_saved_start_receipts&bit) || !(room_saved_ready_applied&bit))return false;
                    room_saved_start_receipts|=bit;continue;
                }
                const bool credited=(room_saved_sent&bit) || (room_saved_send && room_saved_sending==event.source_slot
                    && room_saved_file_digest.Verified() && room_saved_send->sent==room_saved_file_packets.size()
                    && room_saved_send->sent==room_saved_send->credited);
                if(!credited || (room_saved_resident&bit)
                    || room_saved_neo_offsets[event.source_slot-1]!=room_saved_source.serialized.size())return false;
                room_saved_resident|=bit;
            }else{
                if((room_saved_requests&bit) || !CrossplayResumeRequest::Decode(guest.peer,room_saved_checkpoint,
                    guest.source_cid,guest.receiver_cid,event.bytes,wire))return false;
                room_saved_request_wires[event.source_slot-1]=std::move(wire);
                room_saved_requests|=bit;
            }
        }
        const unsigned all=(1u<<room.member_count)-1;
        if(room_saved_requests==all && (!SendRoomSavedNeo(room) || !SendRoomSavedFile(room)))return false;
        if(room_saved_resident==(all&~(1u<<(room.local_slot-1))) && !PumpRoomSavedReadyHost(room))return false;
        if(room_saved_ready_applied==all)return PumpRoomSavedStart(room);
        return GetTickCount64()-room_saved_started<=timeout;
    }

    void OutgoingRoomSaved(void* manager,const void* event,uint32_t mode,uint32_t target){
        using namespace CrossplayNativeCodec;
        if(!Owned() || phase!=Phase::RosterBound || room_world_queued || !room_saved_started || !Detail::Readable(event,4))return;
        const auto edition=launch.channel.edition;
        const auto table=*static_cast<const uintptr_t*>(event);
        const auto* room=channel.GetRoom();
        if(launch.host){
            const bool info=table==ResumeInfoDetail::Pin(edition).event_vt;
            const bool file=table==ResumeFileDetail::Pin(edition).event_vt;
            if((info || file) && (!room || !CaptureRoomSavedFile(*room,manager,event,mode,target,info)))Fail();
            return;
        }
        if(table==ReadyDetail::Pin(edition).event_vt){
            if(!room || !CaptureRoomSavedReady(*room,manager,event,mode,target))Fail();
            return;
        }
        const auto pin=ResumeRequestDetail::Pin(edition);
        if(table!=pin.event_vt)return;
        CrossplayNative::RoomRoute own,host;
        uintptr_t send=0;
        if(!room || room_saved_requested || !room_admission.Resolve(room->local_slot,false,own) || !own.local || own.host
            || !room_admission.Resolve(room->host_slot,false,host) || !host.host || host.local
            || manager!=Detail::Transport(edition,send) || (mode!=2 && (mode!=1 || target!=host.receiver_cid))){
            Fail();return;
        }
        std::vector<unsigned char> wire,frame;
        if(!ResumeRequestDetail::Methods(edition) || !ResumeRequestDetail::Source(pin,event,own.receiver_cid)
            || !Detail::Encode(pin,event,wire)
            || !CrossplayResumeRequest::Encode(host.peer,room_saved_checkpoint,own.receiver_cid,wire,frame)
            || !channel.SendRoom(frame,room->host_slot)){Fail();return;}
        room_saved_requested=true;
    }
