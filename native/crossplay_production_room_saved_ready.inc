    std::vector<unsigned char> room_saved_guest_ready,room_saved_ready_before,room_saved_ready_after,room_saved_initial_config;
    std::deque<Crossplay::RoomEvent> room_saved_ready_requests;
    unsigned room_saved_ready_requested=0,room_saved_ready_applied=0;
    unsigned char room_saved_ready_slot=1;
    ULONGLONG room_saved_ready_pending_at=0;
    CrossplayNativeCodec::PendingHistoryRoomReady room_saved_ready_projection;
    bool room_saved_guest_ready_sent=false,room_saved_ready_observed=false,room_saved_ready_acked=false;
    bool room_saved_guest_prepared=false;

    unsigned RoomHumanSeats()const{
        unsigned seats=0;
        for(unsigned i=0;i<room_local_routes.capacity;++i)
            if(room_local_routes.stations[i]!=UINT32_MAX)seats|=1u<<(i+1);
        return seats;
    }

    bool ReadRoomSavedConfiguration(std::vector<unsigned char>& wire){
        auto interval=room_configuration_interval;
        if(!launch.host && room_saved_resident_cache)interval.cache=room_saved_resident_cache;
        return CrossplayConfigurationNative::SnapshotRoom(launch.channel.edition,original,room_admission,
            room_local_routes,interval,wire,launch.host?room_saved_token:"Savegames/MPSave");
    }

    bool CaptureRoomSavedReady(const Crossplay::RoomSnapshot& room,void* manager,const void* event,uint32_t mode,uint32_t target,
                               bool private_gold=false){
        using namespace CrossplayNativeCodec;
        CrossplayNative::RoomRoute host,own;
        uintptr_t send=0;
        if(launch.host || !room_saved_requested || room_saved_guest_ready_sent || !room_saved_guest_ready.empty()
            || !RoomSavedRoute(room,room.host_slot,host) || !room_admission.Resolve(room.local_slot,false,own)
            || !own.local || own.host || manager!=Detail::Transport(launch.channel.edition,send)
            || (mode!=2 && (mode!=1 || target!=host.receiver_cid)))return false;
        return launch.channel.edition==Crossplay::Edition::Gold
            ?RoomGoldReadyIntent(own.receiver_cid,private_gold?1u:own.player_slot,event,room_saved_guest_ready)
            :ReadyDetail::EncodeIntent(launch.channel.edition,own.receiver_cid,event,room_saved_guest_ready);
    }

    bool ReceiveRoomSavedReady(const Crossplay::RoomSnapshot& room,const Crossplay::RoomEvent& event){
        using namespace CrossplayNativeCodec;
        CrossplayNative::RoomRoute peer;
        if(!RoomSavedRoute(room,event.source_slot,peer))return false;
        const auto& cp=room_saved_checkpoint;
        if(!launch.host){
            if(!resume_stream_observed || !room_saved_guest_ready_sent || room_saved_ready_observed
                || !CrossplaySavedReady::DecodeRoomReady(peer.peer,cp,CrossplaySavedReady::Kind::RoomObserved,
                                                         RoomHumanSeats(),event.bytes))return false;
            room_saved_ready_observed=true;return true;
        }
        const unsigned bit=1u<<(event.source_slot-1);
        if(!(room_saved_resident&bit))return false;
        if(room_saved_ready_observed){
            if((room_saved_ready_applied&bit) || !CrossplaySavedReady::DecodeRoomReady(peer.peer,cp,
                CrossplaySavedReady::Kind::RoomApplied,RoomHumanSeats(),event.bytes))return false;
            room_saved_ready_applied|=bit;return true;
        }
        std::vector<unsigned char> wire;
        if((room_saved_ready_requested&bit) || !CrossplaySavedReady::DecodeRequest(peer.peer,cp,0,
            peer.source_cid,peer.receiver_cid,event.bytes,wire))return false;
        room_saved_ready_requested|=bit;
        room_saved_ready_requests.push_back({event.source_slot,std::move(wire)});
        return true;
    }

    bool PumpRoomSavedReadyHost(const Crossplay::RoomSnapshot& room){
        using namespace CrossplayNativeCodec;
        if(room_saved_ready_observed)return true;
        std::vector<unsigned char> actual;
        if(!ReadRoomSavedConfiguration(actual))return false;
        if(!room_saved_ready_after.empty()){
            if(actual==room_saved_ready_before)return GetTickCount64()-room_saved_ready_pending_at<=10000;
            if(actual!=room_saved_ready_after)return false;
            room_saved_ready_before.clear();room_saved_ready_after.clear();
        }
        CrossplaySessionConfig::Layout layout;
        if(!CrossplaySessionConfig::RoomDetail::Inspect(actual,room_local_routes,layout,false,room_saved_token))return false;
        if(!room_saved_ready_requests.empty()){
            const auto& event=room_saved_ready_requests.front();
            CrossplayNative::RoomRoute guest;
            const auto* lobby=reinterpret_cast<const unsigned char*>(session);
            const auto edition=launch.channel.edition;
            if(!RoomSavedRoute(room,event.source_slot,guest)
                || (edition==Crossplay::Edition::Gold && (!Detail::Readable(lobby,0x34) || CrossplayChat::Word(lobby+0x30)!=2))
                || !RoomSavedHostCurrent()
                || !guest.player_slot || guest.player_slot>room_local_routes.capacity)return false;
            const auto at=layout.slots_at+(guest.player_slot-1)*CrossplaySessionConfig::SlotBytes;
            room_saved_ready_before=actual;
            room_saved_ready_after=std::move(actual);
            room_saved_ready_after[at]=static_cast<unsigned char>(!room_saved_ready_before[at]);
            if(edition==Crossplay::Edition::Gold){
                auto native=event.bytes;native.resize(CrossplayReady::NativeStateSize);
                CrossplayChat::Put(native.data()+13,guest.player_slot);native[17]=room_saved_ready_after[at];
                if(!ReadyDetail::EnqueueState(edition,native,guest.receiver_cid,guest.player_slot))return false;
            }else if(!CrossplayHistoryHostReady::EnqueueRoom(edition,original,room_admission,room.local_slot,event.source_slot,
                room_local_routes,event.bytes,nullptr,room_saved_token))return false;
            room_saved_ready_requests.pop_front();room_saved_ready_pending_at=GetTickCount64();
            return true;
        }
        if(room_saved_ready_requested!=(((1u<<room.member_count)-1)&~(1u<<(room.local_slot-1))))return true;
        for(unsigned i=0;i<room_local_routes.capacity;++i)
            if(room_local_routes.stations[i]!=UINT32_MAX && !actual[layout.slots_at+i*CrossplaySessionConfig::SlotBytes])return false;
        for(unsigned char slot=1;slot<=room.member_count;++slot){
            if(slot==room.local_slot)continue;
            CrossplayNative::RoomRoute guest;
            std::vector<unsigned char> frame;
            if(!RoomSavedRoute(room,slot,guest) || !CrossplaySavedReady::EncodeRoomReady(guest.peer,room_saved_checkpoint,
                CrossplaySavedReady::Kind::RoomObserved,RoomHumanSeats(),frame) || !channel.SendRoom(frame,slot))return false;
        }
        room_saved_ready_applied=1u<<(room.local_slot-1);
        room_saved_ready_observed=true;return true;
    }

    bool PumpRoomSavedGuestRows(bool initial){
        using namespace CrossplayNativeCodec;
        const auto edition=launch.channel.edition;
        if(edition==Crossplay::Edition::Gold)return PumpRoomSavedGoldRows(initial);
        if(room_saved_ready_projection.session){
            const auto observed=ObserveHistoryRoomReady(edition,original,room_admission,room_local_routes,room_saved_ready_projection);
            if(observed==ReadyObservation::Invalid)return false;
            if(observed==ReadyObservation::Pending)return GetTickCount64()-room_saved_ready_pending_at<=10000;
            room_saved_ready_projection={};++room_saved_ready_slot;
        }
        CrossplaySessionConfig::Layout snapshot;
        if(initial && !CrossplaySessionConfig::RoomDetail::Inspect(room_saved_initial_config,room_local_routes,
            snapshot,false,room_saved_token))return false;
        for(;room_saved_ready_slot<=room_local_routes.capacity;++room_saved_ready_slot){
            if(room_local_routes.stations[room_saved_ready_slot-1]==UINT32_MAX)continue;
            const auto value=initial?room_saved_initial_config[snapshot.slots_at+
                (room_saved_ready_slot-1)*CrossplaySessionConfig::SlotBytes]:1u;
            room_saved_ready_pending_at=GetTickCount64();
            return ApplyHistoryRoomReady(edition,original,room_admission,room_local_routes,room_saved_ready_slot,
                                         static_cast<unsigned char>(value),
                                         room_saved_ready_projection);
        }
        return true;
    }

    bool PumpRoomSavedGoldRows(bool initial){
        using namespace CrossplayNativeCodec;
        std::vector<unsigned char> actual;
        CrossplaySessionConfig::Layout stored,live;
        if(!ReadRoomSavedConfiguration(actual)
            || !CrossplaySessionConfig::RoomDetail::Inspect(actual,room_local_routes,live,false,"Savegames/MPSave")
            || (initial && !CrossplaySessionConfig::RoomDetail::Inspect(room_saved_initial_config,room_local_routes,
                    stored,false,room_saved_token)))return false;
        for(;room_saved_ready_slot<=room_local_routes.capacity;++room_saved_ready_slot){
            const auto seat=room_saved_ready_slot-1;
            const auto source=room_local_routes.stations[seat];
            if(source==UINT32_MAX)continue;
            const auto value=initial?room_saved_initial_config[stored.slots_at+seat*CrossplaySessionConfig::SlotBytes]:1u;
            if(actual[live.slots_at+seat*CrossplaySessionConfig::SlotBytes]==value){
                room_saved_ready_pending_at=0;continue;
            }
            if(room_saved_ready_pending_at)return GetTickCount64()-room_saved_ready_pending_at<=10000;
            if(!EnqueueRoomGoldReadyState(source,room_saved_ready_slot,static_cast<unsigned char>(value)))return false;
            room_saved_ready_pending_at=GetTickCount64();return true;
        }
        return true;
    }

    bool PumpRoomSavedReadyGuest(const Crossplay::RoomSnapshot& room){
        if(!resume_stream_observed || room_saved_ready_acked)return true;
        if(!room_saved_guest_prepared){
            if(!PumpRoomSavedGuestRows(true))return false;
            if(room_saved_ready_slot<=room_local_routes.capacity)return true;
            if(!BindRoomGoldGuestRows() || !PrepareRoomHistoryRows(true))return false;
            room_saved_guest_prepared=true;room_saved_ready_slot=1;
        }
        CrossplayNative::RoomRoute host,own;
        if(!RoomSavedRoute(room,room.host_slot,host))return false;
        std::vector<unsigned char> frame;
        if(!room_saved_guest_ready_sent && !room_saved_guest_ready.empty()){
            if(!room_admission.Resolve(room.local_slot,false,own) || !own.local || own.host
                || !CrossplaySavedReady::EncodeRequest(host.peer,room_saved_checkpoint,1,own.receiver_cid,
                                                       room_saved_guest_ready,frame)
                || !channel.SendRoom(frame,room.host_slot))return false;
            room_saved_guest_ready_sent=true;room_saved_guest_ready.clear();
        }
        if(!room_saved_ready_observed)return true;
        if(!PumpRoomSavedGuestRows(false))return false;
        if(room_saved_ready_slot<=room_local_routes.capacity)return true;
        if(!CrossplaySavedReady::EncodeRoomReady(host.peer,room_saved_checkpoint,CrossplaySavedReady::Kind::RoomApplied,
                                               RoomHumanSeats(),frame) || !channel.SendRoom(frame,room.host_slot))return false;
        room_saved_ready_acked=true;return true;
    }
