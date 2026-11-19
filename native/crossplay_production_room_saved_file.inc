    struct RoomSavedFileSend {
        CrossplayResumeTransfer::FileDigest digest;
        std::deque<std::vector<unsigned char>> frames;
        std::vector<uint32_t> ends{0};
        uint32_t checksum=0,sent=0,credited=0;
        size_t buffered=0;
        bool info_captured=false,info_sent=false;
    };
    std::array<std::vector<unsigned char>,Crossplay::RoomSnapshot::MaximumMembers> room_saved_request_wires;
    std::array<uint32_t,Crossplay::RoomSnapshot::MaximumMembers> room_saved_neo_offsets{},room_saved_neo_sequences{};
    std::unique_ptr<RoomSavedFileSend> room_saved_send;
    unsigned char room_saved_sending=0;
    unsigned room_saved_sent=0,room_saved_resident=0;
    uintptr_t room_saved_resident_cache=0;
    ULONGLONG room_saved_resident_poll=0,room_saved_resident_deadline=0;

    bool RoomSavedRoute(const Crossplay::RoomSnapshot& room,unsigned char slot,CrossplayNative::RoomRoute& route) const{
        CrossplayNative::LobbySnapshot live;
        return Owned() && phase==Phase::RosterBound && room_saved_started && !room_world_queued
            && slot && slot<=room.member_count && slot!=room.local_slot
            && original.CaptureLobby(live) && live.instance==session && live.host==launch.host
            && room_admission.Resolve(slot,false,route) && !route.local && route.host!=launch.host;
    }

    bool RoomSavedHostCurrent() {
        using namespace CrossplayNativeCodec;
        const auto edition=launch.channel.edition;
        if(!launch.host || edition!=Crossplay::Edition::Gold || !room_saved_source.lease
            || room_saved_source.lease->value==INVALID_HANDLE_VALUE)return false;
        auto* cache=SessionConfigDetail::GoldConfiguration();
        if(!cache || reinterpret_cast<uintptr_t>(cache)!=room_configuration_interval.cache)return false;
        std::string map,save;
        if(!SessionConfigDetail::NativeString(cache+0x24,map) || map!=launch.room.map
            || !SessionConfigDetail::NativeString(cache+0x34,save) || save!=room_saved_token)return false;
        return SessionConfigDetail::FieldsChecked(SessionConfigDetail::Pin(edition),cache+0x24,cache+0x34,cache+0x44,
            [&](size_t index,unsigned ready,unsigned privacy,uint32_t slot,uint32_t station,uint32_t state){
                return CrossplaySessionConfig::RoomSlotValid(index,ready,privacy,slot,station,state,room_local_routes,true);
            },room_saved_token);
    }

    bool RoomSavedHostFile() {
        using namespace CrossplayNativeCodec;
        if(!RoomSavedHostCurrent())return false;
        auto* cache=SessionConfigDetail::GoldConfiguration();
        if(reinterpret_cast<uintptr_t>(cache)!=room_configuration_interval.cache)return false;
        const auto* pending=*reinterpret_cast<const unsigned char* const*>(cache+0x18);
        const auto* stream=*reinterpret_cast<const unsigned char* const*>(cache+0x1c);
        return SavedSessionConfigDetail::PendingFileMetadata(pending) && Detail::Readable(stream,8)
            && Detail::Readable(*reinterpret_cast<const void* const*>(stream),0x24);
    }

    bool StartRoomSavedFile(const Crossplay::RoomSnapshot& room,unsigned char slot){
        CrossplayNative::RoomRoute guest;
        if(room_saved_send || room_saved_sending || !RoomSavedRoute(room,slot,guest) || !RoomSavedHostFile())return false;
        auto sender=std::make_unique<RoomSavedFileSend>();
        if(!sender->digest.Bind(room_saved_checkpoint))return false;
        room_saved_send=std::move(sender);room_saved_sending=slot;
        return CrossplayNativeCodec::ResumeRequestDetail::EnqueueGoldBound(guest.receiver_cid,room_saved_request_wires[slot-1],[&]{
            CrossplayNative::RoomRoute current_route;
            return RoomSavedRoute(room,slot,current_route) && current_route.receiver_cid==guest.receiver_cid && RoomSavedHostFile();
        });
    }

    bool CaptureRoomSavedFile(const Crossplay::RoomSnapshot& room,void* manager,const void* event,
                              uint32_t mode,uint32_t target,bool info){
        using namespace CrossplayNativeCodec;
        const auto edition=launch.channel.edition;
        CrossplayNative::RoomRoute guest;
        uintptr_t transport=0;
        if(!room_saved_send || mode!=1 || !RoomSavedRoute(room,room_saved_sending,guest)
            || target!=guest.receiver_cid || manager!=Detail::Transport(edition,transport) || !RoomSavedHostFile())return false;
        auto& sender=*room_saved_send;
        const auto& checkpoint=room_saved_checkpoint;
        const auto host=room_local_routes.host;
        std::vector<unsigned char> wire,frame;
        if(info){
            const auto pin=ResumeInfoDetail::Pin(edition);
            if(sender.info_captured || !ResumeInfoDetail::Methods(edition) || !ResumeInfoDetail::Source(pin,event,host)
                || !Detail::Encode(pin,event,wire) || !CrossplayResumeInfo::Encode(guest.peer,checkpoint,host,wire,frame))return false;
            sender.info_captured=true;
        }else{
            const auto pin=ResumeFileDetail::Pin(edition);
            if(!sender.info_captured || sender.digest.Verified() || !ResumeFileDetail::Methods(edition)
                || !ResumeFileDetail::Source(pin,event,host,checkpoint.bytes) || !Detail::Encode(pin,event,wire)
                || !CrossplayResumeTransfer::Inspect(wire,host,checkpoint.bytes))return false;
            const auto sequence=static_cast<uint32_t>(sender.ends.size()),offset=sender.ends.back();
            const auto amount=CrossplayChat::Word(wire.data()+13),checksum=CrossplayChat::Word(wire.data()+21);
            if(sequence>65537 || (sequence>1 && checksum!=sender.checksum) || (!amount && offset!=checkpoint.bytes)
                || !CrossplayResumeTransfer::Encode(guest.peer,checkpoint,sequence,host,wire,frame,offset)
                || !sender.digest.Add(wire,host))return false;
            sender.checksum=checksum;
            sender.ends.push_back(offset+amount);
        }
        sender.buffered+=frame.size();
        if(sender.buffered>CrossplayResumeTransfer::MaximumFileBytes+16u*1024u*1024u)return false;
        sender.frames.push_back(std::move(frame));return true;
    }

    bool SendRoomSavedFile(const Crossplay::RoomSnapshot& room){
        if(!room_saved_send){
            for(unsigned char slot=1;slot<=room.member_count;++slot){
                const unsigned bit=1u<<(slot-1);
                if(slot!=room.local_slot && (room_saved_requests&bit) && !(room_saved_sent&bit))
                    return StartRoomSavedFile(room,slot);
            }
            return true;
        }
        auto& sender=*room_saved_send;
        if(!sender.digest.Verified() || channel.HasPendingWrites())return true;
        if(!RoomSavedHostCurrent())return false;
        if(!sender.info_sent){
            if(sender.frames.empty() || !channel.SendRoom(sender.frames.front(),room_saved_sending))return false;
            sender.buffered-=sender.frames.front().size();sender.frames.pop_front();sender.info_sent=true;
        }
        for(unsigned budget=0;budget<8 && !sender.frames.empty() && sender.sent-sender.credited<32;++budget){
            if(!channel.SendRoom(sender.frames.front(),room_saved_sending))return false;
            sender.buffered-=sender.frames.front().size();sender.frames.pop_front();++sender.sent;
        }
        if(sender.frames.empty() && sender.sent==sender.credited){
            room_saved_sent|=1u<<(room_saved_sending-1);
            room_saved_send.reset();room_saved_sending=0;
        }
        return true;
    }

    bool ReceiveRoomSavedCredit(const CrossplayNative::RoomRoute& guest,const Crossplay::RoomEvent& event){
        if(!room_saved_send || room_saved_sending!=event.source_slot)return false;
        auto& sender=*room_saved_send;
        uint32_t sequence=0,bytes=0;
        if(!CrossplayResumeCredit::Decode(guest.peer,room_saved_checkpoint,event.bytes,sequence,bytes)
            || sequence<=sender.credited || sequence>sender.sent || sequence>=sender.ends.size()
            || sender.ends[sequence]!=bytes)return false;
        sender.credited=sequence;return true;
    }

    bool SendRoomSavedNeo(const Crossplay::RoomSnapshot& room){
        if(channel.HasPendingWrites())return true;
        const auto& serialized=room_saved_source.serialized;
        for(unsigned char slot=1;slot<=room.member_count;++slot){
            if(slot==room.local_slot)continue;
            auto& offset=room_saved_neo_offsets[slot-1];
            auto& sequence=room_saved_neo_sequences[slot-1];
            if(offset==serialized.size())continue;
            CrossplayNative::RoomRoute guest;
            std::vector<unsigned char> frame;
            if(!RoomSavedRoute(room,slot,guest) || !RoomSavedHostCurrent()
                || !CrossplayResumeNeo::Encode(guest.peer,room_saved_checkpoint,sequence+1,offset,serialized,frame)
                || !channel.SendRoom(frame,slot))return false;
            ++sequence;offset+=static_cast<uint32_t>(frame.size()-CrossplayResumeNeo::Header);
        }
        return true;
    }

    bool ReceiveRoomSavedFile(const Crossplay::RoomSnapshot& room,const Crossplay::RoomEvent& event){
        using namespace CrossplayNativeCodec;
        CrossplayNative::RoomRoute host;
        if(!room_saved_requested || !RoomSavedRoute(room,event.source_slot,host))return false;
        const auto owned=[&]{
            CrossplayNative::RoomRoute fresh;
            return RoomSavedRoute(room,event.source_slot,fresh) && fresh.receiver_cid==host.receiver_cid;
        };
        const auto edition=launch.channel.edition;
        const auto& checkpoint=room_saved_checkpoint;
        std::vector<unsigned char> wire,response;
        if(Magic(event.bytes,CrossplayResumeNeo::Magic)){
            if(!resume_neo_bound){
                if(!resume_neo_receiver.Bind(checkpoint))return false;
                resume_neo_bound=true;
            }
            if(!resume_neo_receiver.Accept(host.peer,event.bytes,resume_payload))return false;
            resume_neo_admitted=resume_neo_receiver.Complete();return true;
        }
        if(Magic(event.bytes,CrossplayResumeInfo::Magic)){
            if(resume_info_enqueued || !CrossplayResumeInfo::Decode(host.peer,checkpoint,host.source_cid,host.receiver_cid,event.bytes,wire)
                || !ResumeInfoDetail::EnqueueBound(edition,host.receiver_cid,wire,owned))return false;
            resume_info_enqueued=true;return true;
        }
        if(!resume_info_enqueued || !Magic(event.bytes,CrossplayResumeTransfer::Magic))return false;
        if(!resume_receiver_bound){
            if(!resume_receiver.Bind(checkpoint) || !resume_digest.Bind(checkpoint))return false;
            resume_receiver_bound=true;
        }
        if(!resume_receiver.Accept(host.peer,host.source_cid,host.receiver_cid,event.bytes,wire)
            || !resume_digest.Add(wire,host.receiver_cid)
            || !ResumeFileDetail::EnqueueBound(edition,host.receiver_cid,checkpoint.bytes,wire,owned))return false;
        ++resume_received_blocks;
        if(resume_received_blocks%8==0 || resume_digest.Verified())
            return CrossplayResumeCredit::Encode(host.peer,checkpoint,resume_received_blocks,resume_receiver.ReceivedBytes(),response)
                && channel.SendRoom(response,room.host_slot);
        return true;
    }

    bool ObserveRoomSavedResidentStream(const Crossplay::RoomSnapshot& room,unsigned* stage=nullptr){
        if(stage)*stage=0;
        if(launch.host || !resume_digest.Verified() || !resume_neo_admitted || resume_stream_observed)return true;
        const auto now=GetTickCount64();
        if(!room_saved_resident_deadline)room_saved_resident_deadline=now+10000;
        if(now<room_saved_resident_poll)return true;
        room_saved_resident_poll=now+100;
        const auto pending=[&](unsigned reason){
            if(stage)*stage=reason;
            return now<=room_saved_resident_deadline;
        };
        using namespace CrossplayNativeCodec;
        CrossplayNative::RoomRoute host;
        if(!RoomSavedRoute(room,room.host_slot,host))return pending(1);
        auto* cache=CrossplayConfigurationNative::Cache(launch.channel.edition);
        if(!cache)return pending(2);
        std::string map,save;
        if(!SessionConfigDetail::NativeString(cache+0x24,map) || map!=launch.room.map
            || !SessionConfigDetail::NativeString(cache+0x34,save)
            || save!="Savegames/MPSave")return pending(3);
        const auto* stream=*reinterpret_cast<const unsigned char* const*>(cache+0x1c);
        if(!Detail::Readable(stream,0x28) || *reinterpret_cast<const uintptr_t*>(stream)!=Detail::Pin(launch.channel.edition).stream_vt
            || CrossplayChat::Word(stream+0xc)!=room_saved_checkpoint.bytes)return pending(4);
        const auto* bytes=*reinterpret_cast<const unsigned char* const*>(stream+4);
        Crossplay::Digest actual{};
        if(!Detail::Readable(bytes,room_saved_checkpoint.bytes) || !CrossplayResumeNeo::Hash(bytes,room_saved_checkpoint.bytes,actual)
            || actual!=room_saved_checkpoint.file || CrossplayConfigurationNative::Cache(launch.channel.edition)!=cache
            || *reinterpret_cast<const unsigned char* const*>(cache+0x1c)!=stream
            || *reinterpret_cast<const unsigned char* const*>(stream+4)!=bytes)return pending(5);
        std::vector<unsigned char> receipt;
        if(!CrossplayResumeAdmission::Encode(host.peer,room_saved_checkpoint,CrossplayResumeAdmission::Kind::Prepared,receipt)
            || !channel.SendRoom(receipt,room.host_slot))return false;
        room_saved_resident_cache=reinterpret_cast<uintptr_t>(cache);
        resume_stream_object=reinterpret_cast<uintptr_t>(stream);
        resume_stream_data=reinterpret_cast<uintptr_t>(bytes);
        resume_stream_observed=true;return true;
    }
