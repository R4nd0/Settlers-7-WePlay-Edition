    enum class RoomRootPhase { Idle, Receiving, AwaitingReceipts, AwaitingCommit, Confirmed };
    RoomRootPhase room_root_phase=RoomRootPhase::Idle;
    CrossplaySetupRootStream::Receiver room_root_receiver;
    Crossplay::Digest room_root_digest{};
    unsigned room_root_receipts=0;
    ULONGLONG room_root_started=0;
    bool room_history_save_roster=false;
    inline static constexpr unsigned char RoomRootApplied[]={'S','7','R','R','O','K',0,1};
    inline static constexpr unsigned char RoomRootCommitted[]={'S','7','R','R','O','K',1,1};

    bool CaptureCurrentRoomRoot(std::vector<unsigned char>& serialized) {
        return CrossplaySetupRootCapture::CaptureRoom(launch.channel.edition,room_local_routes.capacity,
            [&]{return RoomTeamsConfirmed() && RoomAllHumansReadyObserved();},serialized);
    }

    bool RoomRootSignal(const unsigned char (&kind)[8],const std::vector<unsigned char>& frame) const {
        return frame.size()==sizeof(kind)+room_root_digest.size() && !memcmp(frame.data(),kind,sizeof(kind))
            && !memcmp(frame.data()+sizeof(kind),room_root_digest.data(),room_root_digest.size());
    }

    std::vector<unsigned char> RoomRootSignal(const unsigned char (&kind)[8]) const {
        std::vector<unsigned char> frame(kind,kind+sizeof(kind));
        frame.insert(frame.end(),room_root_digest.begin(),room_root_digest.end());
        return frame;
    }

    bool SendRoomRoot(const Crossplay::RoomSnapshot& room) {
        std::vector<unsigned char> current_root;
        if(room_held_setup.empty() || !CaptureCurrentRoomRoot(current_root) || current_root!=room_held_setup
            || !CrossplaySetupSignatureNative::Hash(current_root,room_root_digest))return false;
        for(unsigned char slot=1;slot<=room.member_count;++slot){
            if(slot==room.host_slot)continue;
            CrossplayNative::RoomRoute guest;
            std::vector<std::vector<unsigned char>> frames;
            if(!room_admission.Resolve(slot,false,guest) || guest.local || guest.host
                || !CrossplaySetupRootStream::Encode(guest.peer,1,2,current_root,frames))return false;
            for(const auto& frame:frames)if(!channel.SendRoom(frame,slot))return false;
        }
        room_root_receipts=1u<<(room.local_slot-1);
        room_root_started=GetTickCount64();
        room_root_phase=RoomRootPhase::AwaitingReceipts;
        return true;
    }

    bool ReceiveRoomRoot(const Crossplay::RoomSnapshot& room,const Crossplay::RoomEvent& event) {
        if(event.source_slot!=room.host_slot)return false;
        if(room_root_phase==RoomRootPhase::Idle) {
            CrossplayNative::RoomRoute host;
            if(!room_admission.Resolve(room.host_slot,false,host) || !host.host || host.local
                || !CaptureCurrentRoomRoot(room_held_setup)
                || !CrossplaySetupSignatureNative::Hash(room_held_setup,room_root_digest)
                || !room_root_receiver.Bind(host.peer,{1,2,static_cast<uint32_t>(room_held_setup.size()),room_root_digest}))return false;
            room_root_phase=RoomRootPhase::Receiving;
            room_root_started=GetTickCount64();
        }
        if(!room_root_receiver.Accept(event.bytes))return false;
        if(room_root_receiver.GetState()!=CrossplaySetupRootStream::Receiver::State::Complete)return true;
        CrossplaySetupRootStream::Complete input;
        CrossplayAIPreparationSource::View view;
        CrossplaySetupRootReader::Detail::Result native;
        unsigned stage=0;
        std::vector<unsigned char> current_root;
        if(!room_root_receiver.Take(input) || input.bytes!=room_held_setup
            || !CrossplayAIPreparationSource::CaptureView(launch.channel.edition,room_local_routes.capacity,view,stage)
            || !CrossplaySetupRootReader::Detail::Probe(launch.channel.edition,reinterpret_cast<uintptr_t>(view.rows),input.bytes,2,native)
            || !CaptureCurrentRoomRoot(current_root) || current_root!=room_held_setup
            || !channel.SendRoom(RoomRootSignal(RoomRootApplied),room.host_slot))return false;
        room_root_phase=RoomRootPhase::AwaitingCommit;
        return true;
    }

    bool PumpRoomRoot(const Crossplay::RoomSnapshot& room) {
        if(room_root_phase==RoomRootPhase::Confirmed)return true;
        if(room_root_phase==RoomRootPhase::Idle && launch.host){
            if(room_held_start.empty())return true;
#ifdef S7_CROSSPLAY_ROOM_WORLD_LAB
            if(launch.channel.edition!=Crossplay::Edition::Gold && !room_history_save_roster)
                return GetTickCount64()-room_team_started<=10000;
#endif
            if(room_held_setup.empty() && !CaptureCurrentRoomRoot(room_held_setup))return false;
            return SendRoomRoot(room);
        }
        if(room_root_phase!=RoomRootPhase::Idle
            && GetTickCount64()-room_root_started>CrossplaySetupCheckpoint::Lifetime)return false;
        Crossplay::RoomEvent event;
        if(!launch.host){
            if(!channel.ReceiveRoom(event))return true;
            if(room_root_phase!=RoomRootPhase::AwaitingCommit)return ReceiveRoomRoot(room,event);
            std::vector<unsigned char> current_root;
            if(event.source_slot!=room.host_slot || !RoomRootSignal(RoomRootCommitted,event.bytes)
                || !CaptureCurrentRoomRoot(current_root) || current_root!=room_held_setup)return false;
            room_root_phase=RoomRootPhase::Confirmed;
            return true;
        }
        while(channel.ReceiveRoom(event)){
            if(!event.source_slot || event.source_slot>room.member_count
                || (room_root_receipts&(1u<<(event.source_slot-1))) || !RoomRootSignal(RoomRootApplied,event.bytes))return false;
            room_root_receipts|=1u<<(event.source_slot-1);
        }
        if(room_root_receipts!=((1u<<room.member_count)-1))return true;
        std::vector<unsigned char> current_root;
        if(!CaptureCurrentRoomRoot(current_root) || current_root!=room_held_setup
            || !channel.BroadcastRoom(RoomRootSignal(RoomRootCommitted)))return false;
        room_root_phase=RoomRootPhase::Confirmed;
        return true;
    }
