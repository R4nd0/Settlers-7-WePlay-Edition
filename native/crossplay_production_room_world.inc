    std::array<CrossplayFreshStart::Barrier,Crossplay::RoomSnapshot::MaximumMembers> room_start_barriers;
    std::array<CrossplayNeo::Agreement,Crossplay::RoomSnapshot::MaximumMembers> room_neo_agreements;
    std::array<std::array<uint32_t,4>,Crossplay::RoomSnapshot::MaximumMembers> room_world_sent{};
    CrossplayWorldEvents::Inbox room_world_events;
    ULONGLONG room_start_started=0,room_world_queued_at=0;
    bool room_world_arming=false,room_world_queued=false,room_world_running=false;
    bool room_neo_selected=false,room_neo_offer_received=false;
    static constexpr uint32_t RoomNeoFeatures=CrossplayNeo::Fifo|CrossplayNeo::Intent;

    bool RoomStarted() const {
        const auto* room=channel.GetRoom();
        CrossplayNative::RoomRoute local;
        return Owned() && phase==Phase::RosterBound && room_world_queued && room
            && room_admission.Resolve(room->local_slot,true,local) && local.local;
    }

    bool RoomNeoSelectable(){
        if(!launch.host || !room_configuration_complete || !room_held_start.empty() || !NeoModules()
            || !room_ready_requests.empty() || !room_ready_after.empty())return false;
        std::vector<unsigned char> configuration;
        CrossplaySessionConfig::Layout layout;
        if(!ReadRoomSnapshot(configuration) || !CrossplaySessionConfig::InspectRoom(configuration,room_local_routes,layout))return false;
        for(size_t i=0;i<room_local_routes.capacity;++i){
            const auto station=room_local_routes.stations[i];
            if(station!=UINT32_MAX && station!=room_local_routes.host
                && configuration[layout.slots_at+i*CrossplaySessionConfig::SlotBytes])return false;
        }
        return true;
    }

    bool RoomNeoCommand(const std::string& command,std::string& output) {
        if(command=="capabilities"){
            if(!NeoModules())return false;
            output="NEO-FIFO-INTENT-1";return true;
        }
        if(command=="selectable"){output=RoomNeoSelectable()?"1":"0";return true;}
        if(command=="stage:0" || command=="stage:1"){
            if(!RoomNeoSelectable())return false;
            neo_choice=command.back()=='1';output="1";return true;
        }
        if(!room_world_queued && launch.host && command=="selection" && RoomConfigurationApplied()){
            output=neo_choice?"1":"0";return true;
        }
        if(!RoomStarted() || !NeoModules())return false;
        if(command=="network-state" || command=="selection") {output=room_neo_selected?"1":"0";return true;}
        if(command=="start-event") {output="1";return true;}
        if(command=="begin:0" || command=="begin:1"){
            if((command.back()=='1')!=room_neo_selected)return false;
            world_lua_begin=true;output="1";return true;
        }
        if(command=="ready" && world_lua_begin && CrossplayLogisticsActive::Advance()
            && CrossplayLogisticsActive::begun && CrossplayLogisticsActive::ledger.enabled==room_neo_selected){
            world_lua_ready=true;output="1";return true;
        }
        if(command=="status" && world_lua_ready && CrossplayLogisticsActive::Advance()
            && CrossplayLogisticsActive::begun && CrossplayLogisticsActive::ledger.enabled==room_neo_selected){
            output=room_neo_selected?"1":"0";return true;
        }
        uint32_t turn=0;
        if(world_lua_ready && CrossplayNeoSnapshot::LogicTurn(command,turn))
            return CrossplayNeoSnapshot::Capture(launch.channel.edition,owner,turn,output);
        return false;
    }

    CrossplayNeo::Selection RoomNeoSelection(bool enabled)const{
        return {RoomNeoFeatures,enabled,launch.channel.content,room_root_digest,{}};
    }

    bool OfferRoomNeo(unsigned char slot,const CrossplayNative::RoomRoute& route,bool enabled) {
        auto& agreement=room_neo_agreements[slot-1];
        std::vector<unsigned char> output;
        return agreement.Bind(route.peer,RoomNeoSelection(enabled))
            && agreement.Offer(output) && channel.SendRoom(output,slot);
    }

    bool ReceiveRoomNeo(const Crossplay::RoomSnapshot& room,const Crossplay::RoomEvent& event,ULONGLONG now) {
        const auto slot=event.source_slot;
        auto& agreement=room_neo_agreements[slot-1];
        auto& barrier=room_start_barriers[slot-1];
        CrossplayNative::RoomRoute remote;
        if(agreement.Ready() || !room_admission.Resolve(slot,false,remote) || remote.local)return false;
        if(!launch.host && !room_neo_offer_received){
            uint32_t kind=0;
            CrossplayNeo::Selection offer;
            if(slot!=room.host_slot || !CrossplayNeo::Decode(remote.peer,event.bytes,kind,offer) || kind!=1
                || !CrossplayNeo::Same(offer,RoomNeoSelection(offer.enabled)) || !OfferRoomNeo(slot,remote,offer.enabled))return false;
            room_neo_offer_received=true;
        }
        std::vector<unsigned char> output;
        if(!agreement.Consume(event.bytes,output) || (!output.empty() && !channel.SendRoom(output,slot)))return false;
        if(!agreement.Ready())return true;
        return barrier.Bind(remote.peer,launch.host,{room.generation,room_root_digest},launch.channel.content,now)
            && barrier.Ready(now,output) && channel.SendRoom(output,slot) && barrier.Sent(now);
    }

    bool PrepareRoomWorld(const Crossplay::RoomSnapshot& room,ULONGLONG now) {
        bool changed=false;
        std::vector<unsigned char> actual;
        const auto edition=launch.channel.edition;
        if(!SharedEngineMode::Prepare(edition,owner,changed) || !SharedEngineMode::Verify(edition,owner)
            || !InstallWorldMath() || !InstallNeo()
            || !CaptureCurrentRoomRoot(actual) || actual!=room_held_setup)return false;
        if(!outgoing_installed){
            if(!OutgoingObserver::Install(edition,owner,&CaptureOutgoing))return false;
            outgoing_installed=true;
        }
        if(launch.host){
            for(unsigned char slot=1;slot<=room.member_count;++slot){
                if(slot==room.local_slot)continue;
                CrossplayNative::RoomRoute remote;
                if(!room_admission.Resolve(slot,false,remote) || remote.local || !OfferRoomNeo(slot,remote,neo_choice))return false;
            }
        }
        room_start_started=now;
        return true;
    }

    bool QueueRoomWorld(const std::vector<unsigned char>& wire,bool neo_enabled) {
        const auto& hold=CrossplayNativeStart::GoldStartHold::Current();
        if(launch.host && (!start_hold_installed || !hold.attempted || !hold.captured || hold.failed
            || hold.roots.owner!=owner || hold.roots.session!=session || hold.host!=room_local_routes.host
            || !CrossplayNativeStart::GoldStartHold::Release()))return false;
        start_hold_installed=false;
        room_neo_selected=neo_enabled;
        CrossplayLogisticsActive::selected=neo_enabled;
        room_world_arming=true;
        const bool queued=CrossplayNativeCodec::GameStartDetail::DecodeAndEnqueue(launch.channel.edition,wire,room_local_routes.host);
        room_world_arming=false;
        if(!queued)return false;
        room_world_queued=true;
        room_world_queued_at=GetTickCount64();
        return true;
    }

    bool PumpRoomStart(const Crossplay::RoomSnapshot& room) {
        using StartPhase=CrossplayFreshStart::Phase;
        const auto now=GetTickCount64();
        if(!room_start_started && !PrepareRoomWorld(room,now))return false;
        if(now-room_start_started>CrossplayFreshStart::Lifetime)return false;
        Crossplay::RoomEvent event;
        while(channel.ReceiveRoom(event)){
            if(!event.source_slot || event.source_slot>room.member_count || event.source_slot==room.local_slot)return false;
            auto& barrier=room_start_barriers[event.source_slot-1];
            if(CrossplayWorldEvents::Identify(event.bytes)!=CrossplayWorldEvents::Kind::Unknown){
                if((launch.host && !barrier.CanBufferWorldEvents(now)) || !room_world_events.PushRoom(event))return false;
                continue;
            }
            if(!launch.host && event.source_slot!=room.host_slot)return false;
            if(Magic(event.bytes,CrossplayNeo::Magic)){
                if(!ReceiveRoomNeo(room,event,now))return false;
                continue;
            }
            if(Magic(event.bytes,CrossplayFreshStart::ReadyMagic)){
                if(!barrier.ReceiveReady(now,event.bytes))return false;
                continue;
            }
            if(launch.host){
                if(!barrier.HostAttempt(now,event.bytes))return false;
                continue;
            }
            CrossplayNative::RoomRoute host;
            std::vector<unsigned char> actual,wire,receipt;
            return room_admission.Resolve(room.host_slot,false,host) && host.host && !host.local
                && CaptureCurrentRoomRoot(actual) && actual==room_held_setup
                && barrier.GuestAttempt(now,event.bytes,host.source_cid,host.receiver_cid,wire)
                && QueueRoomWorld(wire,room_neo_agreements[room.host_slot-1].Value().enabled)
                && barrier.GuestQueued(now,true,receipt)
                && channel.SendRoom(receipt,room.host_slot) && barrier.Sent(now);
        }
        if(!launch.host)return true;
        bool agreed=true,guest_queued=true;
        for(unsigned char slot=1;slot<=room.member_count;++slot){
            if(slot==room.local_slot)continue;
            const auto state=room_start_barriers[slot-1].GetPhase();
            agreed=agreed && state==StartPhase::Agreed;
            guest_queued=guest_queued && state==StartPhase::HostAttempt;
        }
        if(!agreed && !guest_queued)return true;
        std::vector<unsigned char> actual;
        if(!CaptureCurrentRoomRoot(actual) || actual!=room_held_setup)return false;
        if(guest_queued && !QueueRoomWorld(room_held_start,neo_choice))return false;
        for(unsigned char slot=1;slot<=room.member_count;++slot){
            if(slot==room.local_slot)continue;
            auto& barrier=room_start_barriers[slot-1];
            std::vector<unsigned char> frame;
            if(guest_queued){
                if(!barrier.HostQueued(now,true))return false;
            }else if(!barrier.Start(now,room_held_start,room_local_routes.host,frame)
                || !channel.SendRoom(frame,slot) || !barrier.Sent(now))return false;
        }
        return true;
    }

    void OutgoingRoomWorld(void* manager,const void* event,uint32_t mode,uint32_t target) {
        using namespace CrossplayNativeCodec;
        using CrossplayWorldEvents::Kind;
        if(!Owned() || phase!=Phase::RosterBound || (mode!=1 && mode!=2) || !Detail::Readable(event,4))return;
        const auto edition=launch.channel.edition;
        const auto table=*static_cast<const uintptr_t*>(event);
        Kind kind=Kind::Unknown;
        if(table==LoadProgressDetail::Pin(edition).event_vt)kind=Kind::Progress;
        else if(table==TurnBatchDetail::Pin(edition).event_vt)kind=Kind::Turn;
        else if(table==DesyncTurnDetail::Pin(edition).event_vt)kind=Kind::Desync;
        if(kind==Kind::Unknown)return;
        uintptr_t send=0;
        const auto* room=channel.GetRoom();
        CrossplayNative::RoomRoute own;
        if(!room || manager!=Detail::Transport(edition,send)
            || !room_admission.Resolve(room->local_slot,true,own) || !own.local){Fail();return;}
        for(unsigned char slot=1;slot<=room->member_count;++slot){
            if(slot==room->local_slot)continue;
            CrossplayNative::RoomRoute remote;
            if(!room_admission.Resolve(slot,true,remote) || remote.local){Fail();return;}
            if(mode==1 && target!=remote.receiver_cid)continue;
            auto& sequence=room_world_sent[slot-1][static_cast<unsigned>(kind)];
            std::vector<unsigned char> frame;
            bool encoded=false;
            if(kind==Kind::Progress && sequence<256)
                encoded=LoadProgressDetail::EncodeRoom(edition,own,remote,sequence+1,event,frame);
            if(kind==Kind::Turn && sequence<UINT32_MAX)
                encoded=TurnBatchDetail::EncodeRoom(edition,room_admission,room->local_slot,slot,sequence+1,event,frame);
            if(kind==Kind::Desync && sequence<CrossplayDesyncTurn::MaxSequence)
                encoded=DesyncTurnDetail::EncodeRoom(edition,own,remote,sequence+1,event,frame);
            if(!encoded || !channel.SendRoom(frame,slot)){Fail();return;}
            ++sequence;
        }
    }

    bool PumpRoomWorld() {
        Crossplay::RoomEvent event;
        while(channel.ReceiveRoom(event))if(!room_world_events.PushRoom(event))return false;
        if(!RoomStarted())return !room_world_running && GetTickCount64()-room_world_queued_at<=30000;
        room_world_running=true;
        if(!CrossplayLogisticsActive::Advance()){
            const auto reason=CrossplayLogisticsActive::pending_reason;
            if(CrossplayLogisticsActive::begun || (reason!=3 && reason!=4))return false;
        }
        const auto edition=launch.channel.edition;
        using CrossplayWorldEvents::Kind;
        using namespace CrossplayNativeCodec;
        for(unsigned budget=0;budget<16 && !room_world_events.Empty();++budget){
            if(!room_world_events.DeliverRoom(room_admission,[&](unsigned char source,Kind kind,const auto& wire){
                switch(kind){
                case Kind::Progress:return LoadProgressDetail::EnqueueRoom(edition,room_admission,source,wire);
                case Kind::Turn:return TurnBatchDetail::EnqueueRoom(edition,room_admission,source,wire);
                case Kind::Desync:return DesyncTurnDetail::EnqueueRoom(edition,room_admission,source,wire);
                default:return false;
                }
            }))return false;
        }
        return true;
    }
