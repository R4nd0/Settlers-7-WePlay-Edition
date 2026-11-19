    bool room_saved_world_prepared=false;
    ULONGLONG room_saved_start_at=0;
    unsigned room_saved_start_receipts=0;

    bool RoomSavedStartReady(const Crossplay::RoomSnapshot& room){
        if(!room_saved_started || room_world_queued || !NeoModules()
            || !CrossplayResumeTransfer::Valid(room_saved_checkpoint) || !room_saved_checkpoint.enabled)return false;
        if(!launch.host)return room_saved_ready_acked && resume_stream_observed && resume_neo_admitted;
        if(room_saved_ready_applied!=((1u<<room.member_count)-1) || !RoomSavedHostCurrent())return false;
        std::vector<unsigned char> actual;
        CrossplaySessionConfig::Layout layout;
        if(!ReadRoomSavedConfiguration(actual)
            || !CrossplaySessionConfig::RoomDetail::Inspect(actual,room_local_routes,layout,false,room_saved_token))return false;
        for(unsigned i=0;i<room_local_routes.capacity;++i)
            if(room_local_routes.stations[i]!=UINT32_MAX && !actual[layout.slots_at+i*CrossplaySessionConfig::SlotBytes])return false;
        return true;
    }

    bool RoomSavedStreamCurrent(){
        using namespace CrossplayNativeCodec;
        auto* cache=CrossplayConfigurationNative::Cache(launch.channel.edition);
        if(!cache || reinterpret_cast<uintptr_t>(cache)!=room_saved_resident_cache
            || !resume_stream_object || !resume_stream_data)return false;
        const auto* stream=*reinterpret_cast<const unsigned char* const*>(cache+0x1c);
        const auto* data=reinterpret_cast<const unsigned char*>(resume_stream_data);
        if(reinterpret_cast<uintptr_t>(stream)!=resume_stream_object || !Detail::Readable(stream,0x28)
            || *reinterpret_cast<const uintptr_t*>(stream)!=Detail::Pin(launch.channel.edition).stream_vt
            || *reinterpret_cast<const unsigned char* const*>(stream+4)!=data
            || CrossplayChat::Word(stream+0xc)!=room_saved_checkpoint.bytes
            || !Detail::Readable(data,room_saved_checkpoint.bytes))return false;
        Crossplay::Digest actual{};
        return CrossplayResumeNeo::Hash(data,room_saved_checkpoint.bytes,actual) && actual==room_saved_checkpoint.file
            && *reinterpret_cast<const unsigned char* const*>(cache+0x1c)==stream
            && *reinterpret_cast<const unsigned char* const*>(stream+4)==data;
    }

    bool PrepareRoomSavedWorld(const Crossplay::RoomSnapshot& room){
        if(room_saved_world_prepared || !RoomSavedStartReady(room)
            || (!launch.host && !RoomSavedStreamCurrent()))return false;
        const auto edition=launch.channel.edition;
        bool changed=false;
        if(!SharedEngineMode::Prepare(edition,owner,changed) || !SharedEngineMode::Verify(edition,owner)
            || !InstallWorldMath() || !CrossplayLogisticsActive::ArmResume(launch.host?room_saved_source.payload:resume_payload))return false;
        if(edition==Crossplay::Edition::Gold && (!launch.host
            || !CrossplayGoldResume::Install(edition,owner,room_saved_checkpoint.logic_turn,&SavedPostloadAuthority,&SavedPostloadReport)))return false;
        room_saved_world_prepared=true;return true;
    }

    bool HoldRoomSavedStart(const void* event){
        using namespace CrossplayNativeCodec;
        const auto* room=channel.GetRoom();
        if(!launch.host || !room || !room_held_start.empty() || !RoomSavedStartReady(*room))return false;
        const auto pin=GameStartDetail::Pin(launch.channel.edition);
        return GameStartDetail::Source(pin,event,room_local_routes.host) && Detail::Encode(pin,event,room_held_start)
            && CrossplayResumeStart::Inspect(room_held_start,room_local_routes.host,room_saved_token);
    }

    bool PumpRoomSavedStart(const Crossplay::RoomSnapshot& room){
        if(!launch.host || room_held_start.empty())return true;
        if(!room_saved_start_at){
            if(!RoomSavedStartReady(room))return false;
            for(unsigned char slot=1;slot<=room.member_count;++slot){
                if(slot==room.local_slot)continue;
                CrossplayNative::RoomRoute guest;
                std::vector<unsigned char> frame;
                if(!RoomSavedRoute(room,slot,guest) || !CrossplayResumeStart::Encode(guest.peer,room_saved_checkpoint,
                    room_local_routes.host,room_saved_token,room_held_start,frame) || !channel.SendRoom(frame,slot))return false;
            }
            room_saved_start_receipts=1u<<(room.local_slot-1);
            room_saved_start_at=GetTickCount64();
        }
        if(GetTickCount64()-room_saved_start_at>CrossplayFreshStart::Lifetime)return false;
        if(room_saved_start_receipts!=((1u<<room.member_count)-1))return true;
        return PrepareRoomSavedWorld(room) && QueueRoomWorld(room_held_start,room_saved_checkpoint.enabled);
    }

    bool ReceiveRoomSavedStart(const Crossplay::RoomSnapshot& room,const Crossplay::RoomEvent& event){
        CrossplayNative::RoomRoute host;
        std::vector<unsigned char> wire,receipt;
        if(launch.host || !RoomSavedRoute(room,event.source_slot,host) || !RoomSavedStartReady(room)
            || !CrossplayResumeStart::Decode(host.peer,room_saved_checkpoint,host.source_cid,host.receiver_cid,
                                            room_saved_token,event.bytes,wire)
            || !PrepareRoomSavedWorld(room) || !QueueRoomWorld(wire,room_saved_checkpoint.enabled))return false;
        return CrossplayResumeAdmission::Encode(host.peer,room_saved_checkpoint,CrossplayResumeAdmission::Kind::Queued,receipt)
            && channel.SendRoom(receipt,room.host_slot);
    }

    bool BufferSavedWorldEvent(const Crossplay::RoomSnapshot& room,const Crossplay::RoomEvent& event){
        if(!event.source_slot || event.source_slot>room.member_count || event.source_slot==room.local_slot)return false;
        const bool pending=launch.host?room_saved_start_at && GetTickCount64()-room_saved_start_at<=CrossplayFreshStart::Lifetime
            :room_saved_ready_acked;
        return pending && room_world_events.PushRoom(event);
    }
