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
    ULONGLONG room_ready_started=0;

    bool ValidateRoomSnapshot(const std::vector<unsigned char>& wire) {
        CrossplaySessionConfig::Layout layout;
        return CrossplaySessionConfig::InspectRoom(wire,room_local_routes,layout) && layout.map==launch.room.map;
    }

    bool ReadRoomSnapshot(std::vector<unsigned char>& wire) {
        return CrossplayConfigurationNative::SnapshotRoom(launch.channel.edition,original,room_admission,
            room_local_routes,room_configuration_interval,wire) && ValidateRoomSnapshot(wire);
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
        if(!Owned() || launch.host || !room_primed || phase!=Phase::RosterBound)return;
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
        room_changed_ready_slot=0;
        if(room_primed) {
            CrossplayConfigurationReadiness::RoomSeatReady change;
            if(!CrossplayConfigurationReadiness::RoomReadyChange(room_expected_guest,expected,room_local_routes,change)
                || room_local_routes.stations[change.player_slot-1]==room_local_routes.host)return false;
            room_changed_ready_slot=change.player_slot;
        }
        room_expected_guest=std::move(expected);
        room_offer=std::move(event.bytes);
        room_consumer_seen=room_guest_receipt_sent=room_configuration_complete=false;
        room_ready_projection={};
        room_configuration_started=GetTickCount64();
        event.bytes=std::move(wire);
        return CrossplayNativeCodec::EnqueueRoomSessionConfig(room_admission,event,source,launch.room.capacity,launch.room.map);
    }

    bool PumpRoomReady(const Crossplay::RoomSnapshot& room) {
        if(!GoldHostedReadyRoom(room))return true;
        Crossplay::RoomEvent event;
        if(!launch.host) {
            if(!outgoing_installed) {
                if(!OutgoingObserver::Install(launch.channel.edition,owner,&CaptureOutgoing))return false;
                outgoing_installed=true;
            }
            return !channel.ReceiveRoom(event) || ReceiveRoomOffer(event,room);
        }
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
        if(now-room_configuration_started>10000)return false;
        if(!room_configuration_interval.cache) {
            const auto cache=CrossplayConfigurationNative::Cache(launch.channel.edition);
            if(!cache)return true;
            if(!room_admission.CaptureConfigurationRoutes(launch.room.capacity,room_local_routes))return false;
            room_configuration_interval={owner,session,reinterpret_cast<uintptr_t>(cache)};
        }
        std::vector<unsigned char> readback;
        if(launch.host) {
            if(room_offer.empty()) {
                if(!ReadRoomSnapshot(readback))return true;
                if(!CrossplayRoomConfiguration::Encode(room_local_routes,readback,room_offer)
                    || !channel.BroadcastRoom(room_offer))return false;
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
                || !CrossplayRoomConfiguration::Acknowledge(room_offer,room_local_routes,readback,verified)
                || !CrossplayRoomConfiguration::Commit(room_offer,verified,commit)
                || !channel.BroadcastRoom(commit))return false;
            room_configuration_complete=room_primed=true;
            return true;
        }
        if(room_offer.empty()) {
            Crossplay::RoomEvent event;
            return !channel.ReceiveRoom(event) || ReceiveRoomOffer(event,room);
        }
        if(room_guest_receipt_sent){
            Crossplay::RoomEvent event;
            if(!channel.ReceiveRoom(event))return true;
            if(event.source_slot!=room.host_slot || !CrossplayRoomConfiguration::MatchesCommit(room_offer,event.bytes)
                || !ReadRoomSnapshot(readback) || readback!=room_expected_guest)return false;
            room_configuration_complete=room_primed=true;
            return true;
        }
        if(!room_consumer_seen || !ReadRoomSnapshot(readback))return true;
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
        if(!launch.host && launch.channel.edition!=Crossplay::Edition::Gold && !backend_projected){
            if(!CrossplayNative::ProjectHistoryRoomGuestHost(original,room_admission,snapshot->local_slot,snapshot->host_slot))return false;
            backend_projected=true;
        }
        if(room_configuration_complete)return PumpRoomReady(*snapshot);
        return PumpRoomConfiguration(*snapshot);
    }
