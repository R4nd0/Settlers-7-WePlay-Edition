    inline static constexpr unsigned char RoomSavedOffer[]={'S','7','R','S','A','V',0,1};
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
        if(!launch.host || launch.channel.edition!=Crossplay::Edition::Gold || resume_ui_token.empty()
            || !CrossplayGoldChoices::Methods())return false;
        CrossplayGoldChoices::Cache cache;
        std::wstring path;
        if(CrossplayGoldChoices::Snapshot(cache)!=CrossplayGoldChoices::Observation::Captured
            || cache.map!=launch.room.map || cache.save!=resume_ui_token
            || !CrossplaySelectedSavedSource::Path(launch.profile,cache.save,path)
            || !CrossplayResumeSource::CaptureSelected(path,launch.channel.content,cache.map,room_saved_source)
            || room_saved_source.archive.bytes!=resume_ui_archive.bytes
            || room_saved_source.archive.sha256!=resume_ui_archive.sha256
            || !room_saved_source.players_observed)return false;
        if(!CrossplaySavedAI::FromArchive(room_saved_source.players,room_saved_ai_mask,room_saved_ai,RoomHumanSeats()))return false;
        CrossplayGoldChoices::Cache after;
        if(CrossplayGoldChoices::Snapshot(after)!=CrossplayGoldChoices::Observation::Captured
            || !CrossplayGoldChoices::Same(cache,after))return false;
        room_saved_token=cache.save;
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
            if(!CrossplayNativeStart::GoldStartHold::InstallRoomBound(original,room_admission,room.local_slot,&CaptureRoomGoldStart))return false;
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
            CrossplayGoldChoices::Cache cache;
            const auto observed=CrossplayGoldChoices::Snapshot(cache);
            if(observed!=CrossplayGoldChoices::Observation::Captured || cache.save.empty())
                return now-room_saved_selection_wait<=30000;
            return SendRoomSavedConfiguration(room);
        }
        constexpr ULONGLONG timeout=240000;
        Crossplay::RoomEvent event;
        if(!launch.host){
            for(unsigned budget=0;budget<16 && channel.ReceiveRoom(event);++budget){
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
        for(unsigned budget=0;budget<16 && channel.ReceiveRoom(event);++budget){
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
                    && room_saved_send->digest.Verified() && room_saved_send->frames.empty()
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
        if(!Owned() || !room_saved_started || !Detail::Readable(event,4))return;
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
