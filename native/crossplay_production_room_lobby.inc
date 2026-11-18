    CrossplayNative::RoomLobbyAdmission room_admission;
    CrossplayConfigurationReceipt::Interval room_configuration_interval;
    CrossplaySessionConfig::RoomConnections room_local_routes;
    std::vector<unsigned char> room_offer,room_expected_guest;
    unsigned room_receipts=0;
    CrossplayNativeCodec::PendingHistoryRoomReady room_host_ready;
    bool room_consumer_seen=false,room_guest_receipt_sent=false,room_configuration_complete=false;
    ULONGLONG room_configuration_started=0;

    bool ValidateRoomSnapshot(const std::vector<unsigned char>& wire) {
        CrossplaySessionConfig::Layout layout;
        return CrossplaySessionConfig::InspectRoom(wire,room_local_routes,layout) && layout.map==launch.room.map;
    }

    bool ReadRoomSnapshot(std::vector<unsigned char>& wire) {
        return CrossplayConfigurationNative::SnapshotRoom(launch.channel.edition,original,room_admission,
            room_local_routes,room_configuration_interval,wire) && ValidateRoomSnapshot(wire);
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
            room_configuration_complete=true;
            return true;
        }
        if(room_offer.empty()) {
            Crossplay::RoomEvent event;
            if(!channel.ReceiveRoom(event))return true;
            if(event.source_slot!=room.host_slot)return false;
            CrossplaySessionConfig::RoomConnections source;
            std::vector<unsigned char> wire;
            if(!CrossplayRoomConfiguration::Decode(event.bytes,source,wire) || source.capacity!=launch.room.capacity
                || !CrossplaySessionConfig::ProjectRoom(wire,source,room_local_routes,room_expected_guest)
                || !ValidateRoomSnapshot(room_expected_guest))return false;
            room_offer=std::move(event.bytes);
            event.bytes=std::move(wire);
            return CrossplayNativeCodec::EnqueueRoomSessionConfig(room_admission,event,source,launch.room.capacity,launch.room.map);
        }
        if(room_guest_receipt_sent){
            Crossplay::RoomEvent event;
            if(!channel.ReceiveRoom(event))return true;
            if(event.source_slot!=room.host_slot || !CrossplayRoomConfiguration::MatchesCommit(room_offer,event.bytes)
                || !ReadRoomSnapshot(readback) || readback!=room_expected_guest)return false;
            room_configuration_complete=true;
            return true;
        }
        if(!room_consumer_seen || !ReadRoomSnapshot(readback))return true;
        using namespace CrossplayNativeCodec;
        if(room_host_ready.session) {
            const auto status=ObserveHistoryRoomHostReady(launch.channel.edition,original,room_admission,
                room_local_routes,room_host_ready);
            if(status==ReadyObservation::Invalid)return false;
            if(status!=ReadyObservation::Applied || readback!=room_expected_guest)return true;
        } else if(readback!=room_expected_guest) {
            // Reuse History's original confirmed-setup event and Ready setter
            // only for the authenticated host row, after native consumption.
            // A different guest, map, slot or AI byte still blocks the receipt.
            CrossplayConfigurationReadiness::RoomHostReady ready;
            if(launch.channel.edition==Crossplay::Edition::Gold
                || CrossplayConfigurationReadiness::CompareRoom(room_expected_guest,readback,room_local_routes,ready)
                    !=CrossplayConfigurationReadiness::Difference::HostOnly)return true;
            return ApplyHistoryRoomHostReady(launch.channel.edition,original,room_admission,room_local_routes,
                ready.player_slot,ready.value,room_host_ready);
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
        return PumpRoomConfiguration(*snapshot);
    }
