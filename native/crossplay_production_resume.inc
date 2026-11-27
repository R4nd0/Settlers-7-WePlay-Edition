    CrossplaySelectedSavedSource::Value resume_source;
    CrossplayResumeSource::ArchiveIdentity resume_ui_archive{};
    std::string resume_ui_token;
    CrossplayResumeTransfer::Checkpoint resume_checkpoint{};
    CrossplayResumeTransfer::FileDigest resume_digest;
    CrossplayResumeTransfer::Receiver resume_receiver;
    CrossplayResumeNeo::Receiver resume_neo_receiver;
    std::string resume_token,resume_payload,resume_serialized;
    bool resume_bound=false,resume_config_sent=false,resume_config_enqueued=false;
    bool resume_request_captured=false,resume_request_sent=false,resume_request_enqueued=false;
    bool resume_info_captured=false,resume_info_sent=false,resume_info_enqueued=false;
    bool resume_zero_end=false,resume_receiver_bound=false,resume_neo_bound=false,resume_neo_admitted=false;
    bool resume_stream_observed=false,resume_neo_armed=false,resume_prepared_sent=false,resume_peer_prepared=false;
    uintptr_t resume_stream_object=0,resume_stream_data=0;
    bool resume_start_sent=false,resume_start_attempted=false,resume_peer_queued=false;
    bool resume_start_received=false;
    std::vector<unsigned char> resume_incoming_start;
    bool resume_world_prepared=false;
    ULONGLONG resume_started=0,resume_configuration_at=0;
    std::vector<unsigned char> resume_request_wire;
    std::deque<std::vector<unsigned char>> resume_send_queue;
    std::vector<uint32_t> resume_send_ends{0};
    uint32_t resume_sent_blocks=0,resume_credit_blocks=0,resume_received_blocks=0;
    uint32_t resume_native_checksum=0,resume_neo_offset=0,resume_neo_sequence=0;
    size_t resume_buffered_bytes=0;
    uint32_t resume_ready_sent=0,resume_ready_received=0,resume_ready_observed=0,resume_ready_pending_revision=0;
    std::deque<std::pair<uint32_t,std::vector<unsigned char>>> resume_ready_requests;
    std::deque<std::vector<unsigned char>> resume_guest_ready_wires;
    std::deque<CrossplayReadyState::Snapshot> resume_ready_states;
    CrossplayNativeCodec::PendingReadyToggle resume_ready_pending;
    CrossplayNativeCodec::PendingHistoryReadyState resume_ready_projection;
    ULONGLONG resume_ready_at=0;
    bool ResumeInterval()const{
        return Owned() && bootstrap.saved && phase==Phase::RosterBound && !neo_world_queued
            && channel.GetState()==Crossplay::State::Connected
            && CrossplayNativeCodec::ResumeFileDetail::Owner(original,owner,session,LocalConnections(),launch.host);
    }
    bool ConfigureSaved(){
        if(!ResumeInterval())return false;
        if(!resume_started)resume_started=GetTickCount64();
        // A distinct, bounded saved-file operation; no fresh setup lease is
        // extended or reused. The source and original consumers own this path.
        if(GetTickCount64()-resume_started>120000)return false;
        const auto* peer=channel.GetPeer();if(!peer)return false;
        const auto ids=LocalConnections();
        if(launch.host){
            if(resume_config_sent)return CrossplaySelectedSavedSource::Current(original,resume_source);
            if(!CrossplaySelectedSavedSource::Capture(launch,bootstrap,original,owner,session,ids,resume_source))return false;
            if(!resume_ui_token.empty() && (resume_source.cache.save!=resume_ui_token
                || resume_source.selected.archive.bytes!=resume_ui_archive.bytes
                || resume_source.selected.archive.sha256!=resume_ui_archive.sha256))return false;
            resume_checkpoint=resume_source.selected.checkpoint;resume_token=resume_source.cache.save;
            resume_serialized=resume_source.selected.serialized;resume_payload=resume_source.selected.payload;
            if(!resume_digest.Bind(resume_checkpoint)
                || !CrossplayResumeNeo::Inspect(resume_checkpoint,resume_serialized,resume_payload))return false;
            std::vector<unsigned char> frame;
            if(!CrossplaySavedSessionConfig::Encode(*peer,resume_checkpoint,ids,bootstrap.map,resume_token,resume_source.configuration,frame)
                || !channel.Send(frame))return false;
            resume_bound=resume_config_sent=resume_neo_admitted=true;configuration_sent=true;return true;
        }
        if(!guest_claim.Applied(original,admission))return true;
        if(!resume_config_enqueued){
            std::vector<unsigned char> frame,wire;if(!channel.Receive(frame))return true;
            uint32_t peer_guest=UINT32_MAX;
            if(!CrossplayNative::ProjectConnection(peer->edition,admission.PeerSourceConnection(),peer->local_in_peer_namespace,peer_guest)
                || !CrossplaySavedSessionConfig::Discover(*peer,launch.channel.content,{admission.PeerSourceConnection(),peer_guest},
                    ids,bootstrap.map,frame,resume_checkpoint,resume_token,wire)
                || !resume_digest.Bind(resume_checkpoint))return false;
            resume_bound=resume_config_enqueued=true;resume_configuration_at=GetTickCount64();
            if(!CrossplayNativeCodec::SavedSessionConfigDetail::Enqueue(launch.channel.edition,original,owner,session,ids,
                bootstrap.map,resume_token,wire))return false;
        }
        // An actual original file-request event is the saved-config receipt.
        // Fresh configuration/readiness projection is never used for this.
        if(!resume_request_captured)return GetTickCount64()-resume_configuration_at<=15000;
        configuration_applied=true;return true;
    }
    bool OutgoingSaved(void* manager,const void* event,uint32_t mode,uint32_t target){
        if(!bootstrap.saved || neo_world_queued)return false;
        using namespace CrossplayNativeCodec;
        if(!Detail::Readable(event,4))return true;
        const auto edition=launch.channel.edition;const auto table=*static_cast<const uintptr_t*>(event);
        const bool request=table==ResumeRequestDetail::Pin(edition).event_vt;
        const bool info=table==ResumeInfoDetail::Pin(edition).event_vt;
        const bool file=table==ResumeFileDetail::Pin(edition).event_vt;
        const bool ready=table==ReadyDetail::Pin(edition).event_vt;
        if(!request && !info && !file && !ready)return true;
        if(ready && launch.host)return true; // Original local Gold consumer; observe it independently below.
        uintptr_t sender=0;
        if(!ResumeInterval() || !resume_bound || manager!=Detail::Transport(edition,sender))return Fail();
        const auto ids=LocalConnections();const auto* peer=channel.GetPeer();std::vector<unsigned char> wire,frame;
        if(!peer)return Fail();
        if(ready){
            // The original final file consumer may emit this synchronously.
            // Capture only here; verify its completed resident stream and send
            // later on the normal owner pump, outside the native cache lock.
            if(launch.host || !resume_config_enqueued || !held_start_wire.empty() || resume_guest_ready_wires.size()>=4
                || (mode!=2 && (mode!=1 || target!=ids.host))
                || !EncodeReadyRequest(edition,original,ids,event,wire))return Fail();
            resume_guest_ready_wires.push_back(std::move(wire));guest_ready_observed=true;return true;
        }
        if(request){
            if(launch.host || !resume_config_enqueued || resume_request_captured
                || (mode!=2 && (mode!=1 || target!=ids.host))
                || !ResumeRequestDetail::Encode(edition,original,owner,session,ids,event,resume_request_wire))return Fail();
            resume_request_captured=true;return true;
        }
        if(!launch.host || !resume_request_enqueued || mode!=1 || target!=ids.guest
            || !CrossplaySelectedSavedSource::Current(original,resume_source))return Fail();
        if(info){
            if(resume_info_captured || !ResumeInfoDetail::Encode(edition,original,owner,session,ids,event,wire)
                || !CrossplayResumeInfo::Encode(*peer,resume_checkpoint,ids.host,wire,frame))return Fail();
            resume_info_captured=true;resume_buffered_bytes=frame.size();resume_send_queue.push_back(std::move(frame));return true;
        }
        if(!resume_info_captured || resume_zero_end
            || !ResumeFileDetail::Encode(edition,original,owner,session,ids,resume_checkpoint.bytes,event,wire))return Fail();
        const uint32_t amount=CrossplayChat::Word(wire.data()+13),checksum=CrossplayChat::Word(wire.data()+21);
        const uint32_t sequence=static_cast<uint32_t>(resume_send_ends.size()),offset=resume_send_ends.back();
        if(sequence>65537 || amount>resume_checkpoint.bytes-offset || (!amount && offset!=resume_checkpoint.bytes)
            || (sequence>1 && checksum!=resume_native_checksum)
            || !CrossplayResumeTransfer::Encode(*peer,resume_checkpoint,sequence,ids.host,wire,frame,offset)
            || !resume_digest.Add(wire,ids.host))return Fail();
        resume_native_checksum=checksum;resume_zero_end=!amount;resume_send_ends.push_back(offset+amount);
        resume_buffered_bytes+=frame.size();
        if(resume_buffered_bytes>CrossplayResumeTransfer::MaximumFileBytes+16u*1024u*1024u)return Fail();
        resume_send_queue.push_back(std::move(frame));return true;
    }
    bool SendSavedFile(){
        if(!launch.host || !resume_info_captured || !resume_digest.Verified() || channel.HasPendingWrites())return true;
        if(!CrossplaySelectedSavedSource::Current(original,resume_source))return false;
        if(!resume_info_sent){
            if(resume_send_queue.empty() || !channel.Send(resume_send_queue.front()))return false;
            resume_buffered_bytes-=resume_send_queue.front().size();resume_send_queue.pop_front();resume_info_sent=true;
        }
        // Credits pace authenticated transport only; original game flags and
        // acknowledgements are never manufactured from these counters.
        for(unsigned budget=0;budget<8 && !resume_send_queue.empty() && resume_sent_blocks-resume_credit_blocks<32;++budget){
            if(!channel.Send(resume_send_queue.front()))return false;
            resume_buffered_bytes-=resume_send_queue.front().size();resume_send_queue.pop_front();++resume_sent_blocks;
        }
        return true;
    }
    bool SavedFrame(const std::vector<unsigned char>& frame,bool& recognized){
        const bool request=Magic(frame,CrossplayResumeRequest::Magic),info=Magic(frame,CrossplayResumeInfo::Magic);
        const bool file=Magic(frame,CrossplayResumeTransfer::Magic),credit=Magic(frame,CrossplayResumeCredit::Magic);
        const bool neo=Magic(frame,CrossplayResumeNeo::Magic),receipt=Magic(frame,CrossplayResumeAdmission::Magic);
        const bool start=Magic(frame,CrossplayResumeStart::Magic),agreement=Magic(frame,CrossplayNeo::Magic);
        const bool ready=Magic(frame,CrossplaySavedReady::Magic);
        recognized=request||info||file||credit||neo||receipt||start||agreement||ready;
        if(!recognized)return true;
        if(!ResumeInterval() || !resume_bound)return false;
        const auto* peer=channel.GetPeer();if(!peer)return false;const auto ids=LocalConnections();
        std::vector<unsigned char> wire,response;
        if(ready){
            if(launch.host){
                if(!resume_digest.Verified() || !resume_neo_admitted || !held_start_wire.empty()
                    || !CrossplaySelectedSavedSource::Current(original,resume_source) || resume_ready_requests.size()>=4
                    || !CrossplaySavedReady::DecodeRequest(*peer,resume_checkpoint,resume_ready_received,
                        admission.PeerSourceConnection(),ids.guest,frame,wire))return false;
                ++resume_ready_received;resume_ready_requests.emplace_back(resume_ready_received,std::move(wire));return true;
            }
            CrossplayReadyState::Snapshot observed;
            if(!resume_stream_observed || !resume_neo_admitted || resume_ready_states.size()>=4
                || !CrossplaySavedReady::DecodeObserved(*peer,resume_checkpoint,resume_ready_observed,resume_ready_sent,frame,observed))return false;
            resume_ready_observed=observed.revision;resume_ready_states.push_back(observed);return true;
        }
        if(request){
            if(!launch.host || !resume_config_sent || resume_request_enqueued
                || !CrossplaySelectedSavedSource::Current(original,resume_source)
                || !CrossplayResumeRequest::Decode(*peer,resume_checkpoint,admission.PeerSourceConnection(),ids.guest,frame,wire))return false;
            // Arm before the original consumer: it can synchronously emit all
            // original info/file events through the existing owned observer.
            resume_request_enqueued=true;
            return CrossplayNativeCodec::ResumeRequestDetail::EnqueueGold(original,owner,session,ids,bootstrap.map,resume_token,wire);
        }
        if(credit){
            uint32_t sequence=0,bytes=0;
            if(!launch.host || !CrossplayResumeCredit::Decode(*peer,resume_checkpoint,frame,sequence,bytes)
                || sequence<=resume_credit_blocks || sequence>resume_sent_blocks || sequence>=resume_send_ends.size()
                || resume_send_ends[sequence]!=bytes)return false;
            resume_credit_blocks=sequence;return true;
        }
        if(info || file){
            if(launch.host || !resume_config_enqueued || !resume_request_sent)return false;
            if(info){
                if(resume_info_enqueued || !CrossplayResumeInfo::Decode(*peer,resume_checkpoint,admission.PeerSourceConnection(),ids.host,frame,wire)
                    || !CrossplayNativeCodec::ResumeInfoDetail::Enqueue(launch.channel.edition,original,owner,session,ids,wire))return false;
                resume_info_enqueued=true;return true;
            }
            if(!resume_info_enqueued)return false;
            if(!resume_receiver_bound){if(!resume_receiver.Bind(resume_checkpoint))return false;resume_receiver_bound=true;}
            if(!resume_receiver.Accept(*peer,admission.PeerSourceConnection(),ids.host,frame,wire) || !resume_digest.Add(wire,ids.host)
                || !CrossplayNativeCodec::ResumeFileDetail::Enqueue(launch.channel.edition,original,owner,session,ids,resume_checkpoint.bytes,wire))return false;
            ++resume_received_blocks;
            if(resume_received_blocks%8==0 || resume_digest.Verified()){
                if(!CrossplayResumeCredit::Encode(*peer,resume_checkpoint,resume_received_blocks,resume_receiver.ReceivedBytes(),response)
                    || !channel.Send(response))return false;
            }
            return true;
        }
        if(neo){
            if(launch.host || !resume_config_enqueued || resume_neo_admitted)return false;
            if(!resume_neo_bound){if(!resume_neo_receiver.Bind(resume_checkpoint))return false;resume_neo_bound=true;}
            if(!resume_neo_receiver.Accept(*peer,frame,resume_payload))return false;
            resume_neo_admitted=resume_neo_receiver.Complete();return true;
        }
        if(agreement){
            if(frame.size()!=CrossplayNeo::FrameSize || world_neo_frames.size()>=4)return false;
            world_neo_frames.push_back(frame);return true;
        }
        if(receipt){
            CrossplayResumeAdmission::Kind kind{};
            if(!launch.host || !CrossplayResumeAdmission::Decode(*peer,resume_checkpoint,frame,kind))return false;
            if(kind==CrossplayResumeAdmission::Kind::Prepared){
                if(resume_peer_prepared)return false;resume_peer_prepared=true;return true;
            }
            if(!resume_start_sent || resume_peer_queued || !EnqueueSavedWorld(held_start_wire))return false;
            resume_peer_queued=true;return true;
        }
        if(launch.host || resume_start_received || !resume_prepared_sent || !ResumeNativeReady()
            || !CrossplayResumeStart::Decode(*peer,resume_checkpoint,admission.PeerSourceConnection(),ids.host,resume_token,frame,wire))return false;
        resume_incoming_start=std::move(wire);resume_start_received=true;return true;
    }
    bool ResumeNativeReady(){
        if(!ResumeInterval() || !resume_bound || !resume_digest.Verified() || !resume_neo_admitted || !NeoModules())return false;
        using namespace CrossplayNativeCodec;const auto edition=launch.channel.edition;const auto ids=LocalConnections();
        auto* cache=launch.host?SessionConfigDetail::GoldConfiguration():ReadyStateDetail::HistoryConfiguration(edition);
        const auto pin=GameStartDetail::Pin(edition);std::string map,save;
        if(!cache || !SessionConfigDetail::NativeString(cache+0x24,map) || map!=bootstrap.map
            || !SessionConfigDetail::NativeString(cache+0x34,save) || save!=(launch.host?resume_token:"Savegames/MPSave"))return false;
        const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+(pin.gold?0x48:0x44));
        if(!Detail::Readable(slots,8*0x18) || !CrossplayResumeAdmission::SavedSlots(slots,8*0x18,ids,launch.host))return false;
        if(launch.host){if(!CrossplaySelectedSavedSource::Current(original,resume_source))return false;}
        else{
            const auto* stream=*reinterpret_cast<const unsigned char* const*>(cache+0x1c);
            if(!Detail::Readable(stream,0x28) || *reinterpret_cast<const uintptr_t*>(stream)!=Detail::Pin(edition).stream_vt
                || CrossplayChat::Word(stream+0xc)!=resume_checkpoint.bytes)return false;
            const auto* bytes=*reinterpret_cast<const unsigned char* const*>(stream+4);Crossplay::Digest actual{};
            if(!Detail::Readable(bytes,resume_checkpoint.bytes))return false;
            if(!resume_stream_observed || resume_stream_object!=reinterpret_cast<uintptr_t>(stream) || resume_stream_data!=reinterpret_cast<uintptr_t>(bytes)){
                if(!CrossplayResumeNeo::Hash(bytes,resume_checkpoint.bytes,actual) || actual!=resume_checkpoint.file
                    || *reinterpret_cast<const unsigned char* const*>(cache+0x1c)!=stream
                    || *reinterpret_cast<const unsigned char* const*>(stream+4)!=bytes
                    || CrossplayChat::Word(stream+0xc)!=resume_checkpoint.bytes)return Fail();
                resume_stream_observed=true;resume_stream_object=reinterpret_cast<uintptr_t>(stream);resume_stream_data=reinterpret_cast<uintptr_t>(bytes);
            }
        }
        return true;
    }
    bool PrepareSavedWorld(){
        if(!ResumeNativeReady() || !neo_world_agreement.Ready())return false;
        if(!world_mode_prepared){
            bool changed=false;if(!SharedEngineMode::Prepare(launch.channel.edition,owner,changed) || !ResumeNativeReady())return false;
            world_mode_prepared=true;
        }
        if(!SharedEngineMode::Verify(launch.channel.edition,owner) || !InstallWorldMath())return false;
        if(!resume_neo_armed){
            if(!CrossplayLogisticsActive::ArmResume(resume_payload,resume_checkpoint.logic_turn,
                resume_checkpoint.replay_turn,resume_checkpoint.enabled))return false;
            resume_neo_armed=true;
        }
        neo_world_session=session;world_bound=true;resume_world_prepared=true;
        if(launch.channel.edition==Crossplay::Edition::Gold && !CrossplayGoldResume::installed
            && !CrossplayGoldResume::Install(launch.channel.edition,owner,resume_checkpoint.logic_turn,&SavedPostloadAuthority,&SavedPostloadReport))return false;
        return true;
    }
    static bool SavedPostloadAuthority(){
#ifdef S7_CROSSPLAY_ROOM_WORLD
        if(current && current->launch.IsRoom())
            return current->Owned() && current->launch.channel.edition==Crossplay::Edition::Gold
                && current->phase==Phase::RosterBound && current->room_saved_world_prepared
                && (current->room_world_arming || current->room_world_queued)
                && CrossplayResumeTransfer::Valid(current->room_saved_checkpoint)
                && current->channel.GetState()==Crossplay::State::Connected;
#endif
        return current && current->Owned() && current->launch.channel.edition==Crossplay::Edition::Gold
            && current->bootstrap.saved
            && current->phase==Phase::RosterBound && current->resume_world_prepared && current->resume_neo_armed
            && current->resume_bound && CrossplayResumeTransfer::Valid(current->resume_checkpoint)
            && current->channel.GetState()==Crossplay::State::Connected;
    }
    static void SavedPostloadReport(bool okay,const char*){if(current && !okay)current->Fail();}
    bool ReleaseSavedObservers(){
        if(!ResumeNativeReady() || !resume_world_prepared || !world_bound)return false;
        if(!launch.host)return !start_hold_installed && !gold_local_installed;
        const auto& hold=CrossplayNativeStart::GoldStartHold::Current();
        if(!start_hold_installed || !gold_local_installed || !hold.attempted || !hold.captured || hold.failed
            || hold.roots.owner!=owner || hold.roots.session!=session || hold.host!=admission.OwnConnection()
            || CrossplayNativeObserver::GoldLocalObserver::Current().failed)return false;
        if(!CrossplayNativeStart::GoldStartHold::Release())return false;start_hold_installed=false;
        if(!CrossplayNativeObserver::GoldLocalObserver::Release())return false;gold_local_installed=false;return true;
    }
    bool EnqueueSavedWorld(const std::vector<unsigned char>& wire){
        if(resume_start_attempted || resume_ready_pending.configuration || !resume_ready_requests.empty()
            || resume_ready_projection.session || !resume_ready_states.empty() || !resume_guest_ready_wires.empty()
            || (!launch.host && resume_ready_sent!=resume_ready_observed) || !PrepareSavedWorld() || !ReleaseSavedObservers())return false;
        resume_start_attempted=true;
        if(!launch.host){
            // Rehash at the actual original queue boundary as well as at file
            // admission. Ready presentation alone cannot bind a replaced stream.
            Crossplay::Digest current_file{};
            if(!CrossplayResumeNeo::Hash(reinterpret_cast<const unsigned char*>(resume_stream_data),resume_checkpoint.bytes,current_file)
                || current_file!=resume_checkpoint.file || !ResumeNativeReady())return false;
        }
        const auto host=LocalConnections().host;
        if(!CrossplayResumeStart::Inspect(wire,host,resume_token)
            || !CrossplayNativeCodec::GameStartDetail::DecodeAndEnqueue(launch.channel.edition,wire,host))return false;
        neo_world_queued=true;world_dispatch_started=GetTickCount64();return true;
    }
    bool HoldSavedStart(void* manager,const void* event){
        if(!launch.host || !ResumeInterval() || !resume_bound || !resume_config_sent || !held_start_wire.empty()
            || !CrossplaySelectedSavedSource::Current(original,resume_source)
            || reinterpret_cast<uintptr_t>(manager)!=CrossplayNativeStart::GoldStartHold::Current().roots.manager)return false;
        const auto pin=CrossplayNativeCodec::GameStartDetail::Pin(launch.channel.edition);
        return CrossplayNativeCodec::GameStartDetail::Methods(launch.channel.edition)
            && CrossplayNativeCodec::GameStartDetail::Source(pin,event,admission.OwnConnection())
            && CrossplayNativeCodec::Detail::Encode(pin,event,held_start_wire)
            && CrossplayResumeStart::Inspect(held_start_wire,admission.OwnConnection(),resume_token);
    }
    bool PumpSavedReadiness(){
        const auto* peer=channel.GetPeer();if(!peer)return false;
        using namespace CrossplayNativeCodec;
        if(launch.host){
            if(!CrossplaySelectedSavedSource::Current(original,resume_source))return false;
            if(resume_ready_pending.configuration){
                CrossplayReadyState::Snapshot state;
                const auto observed=SavedReadyDetail::Observe(original,owner,bootstrap.map,resume_token,resume_ready_pending,resume_ready_pending_revision,state);
                if(observed==ReadyObservation::Invalid)return false;
                if(observed==ReadyObservation::Pending)return GetTickCount64()-resume_ready_at<=10000;
                std::vector<unsigned char> frame;
                if(!CrossplaySavedReady::EncodeObserved(*peer,resume_checkpoint,state,frame) || !channel.Send(frame))return false;
                resume_ready_pending={};resume_ready_pending_revision=0;
            }
            if(!resume_ready_requests.empty()){
                if(!SavedReadyDetail::Enqueue(original,owner,session,LocalConnections(),bootstrap.map,resume_token,
                    resume_ready_requests.front().second,resume_ready_pending))return false;
                resume_ready_pending_revision=resume_ready_requests.front().first;resume_ready_requests.pop_front();resume_ready_at=GetTickCount64();
            }
        }else{
            if(resume_ready_projection.session){
                const auto observed=ObserveHistoryReadySnapshot(launch.channel.edition,original,LocalConnections(),resume_ready_projection);
                if(observed==ReadyObservation::Invalid)return false;
                if(observed==ReadyObservation::Pending)return GetTickCount64()-resume_ready_at<=10000;
                resume_ready_projection={};
            }
            if(!resume_ready_states.empty()){
                if(!ApplyHistoryReadySnapshot(launch.channel.edition,original,LocalConnections(),resume_ready_states.front(),resume_ready_projection))return false;
                resume_ready_states.pop_front();resume_ready_at=GetTickCount64();
            }
        }
        return true;
    }
    bool FlushSavedGuestReady(){
        if(launch.host || resume_guest_ready_wires.empty())return true;
        if(!resume_stream_observed || !resume_neo_admitted || !ResumeNativeReady())return phase!=Phase::Failed;
        const auto* peer=channel.GetPeer();std::vector<unsigned char> frame;
        if(!peer || resume_ready_sent>=UINT32_MAX-1
            || !CrossplaySavedReady::EncodeRequest(*peer,resume_checkpoint,resume_ready_sent+1,LocalConnections().guest,resume_guest_ready_wires.front(),frame)
            || !channel.Send(frame))return false;
        ++resume_ready_sent;resume_guest_ready_wires.pop_front();return true;
    }
    bool PumpSaved(){
        if(neo_world_queued)return Readiness();
        if(!ResumeInterval())return false;
        if(!resume_bound)return true;
        if(GetTickCount64()-resume_started>120000)return false;
        const auto* peer=channel.GetPeer();if(!peer)return false;std::vector<unsigned char> frame;
        for(unsigned budget=0;budget<16 && channel.Receive(frame);++budget){
            bool recognized=false;if(!SavedFrame(frame,recognized))return false;
            if(!recognized){
                if(CrossplayWorldEvents::Identify(frame)==CrossplayWorldEvents::Kind::Unknown
                    || !(resume_start_sent || resume_start_attempted) || !resume_world_prepared || !world_events.Push(frame))return false;
            }
            if(neo_world_queued)return Readiness();
        }
        if(!launch.host && resume_request_captured && !resume_request_sent){
            if(!CrossplayResumeRequest::Encode(*peer,resume_checkpoint,LocalConnections().guest,resume_request_wire,frame)
                || !channel.Send(frame))return false;resume_request_sent=true;
        }
        if(!SendSavedFile())return false;
        if(!PumpSavedReadiness())return false;
        if(!FlushSavedGuestReady())return false;
        if(!resume_incoming_start.empty()){
            // The original readiness consumer/projection may complete on its
            // next engine tick. Preserve frame order without queuing start early.
            if(resume_ready_projection.session || !resume_ready_states.empty() || !resume_guest_ready_wires.empty()
                || resume_ready_sent!=resume_ready_observed)return true;
            if(!EnqueueSavedWorld(resume_incoming_start)
                || !CrossplayResumeAdmission::Encode(*peer,resume_checkpoint,CrossplayResumeAdmission::Kind::Queued,frame)
                || !channel.Send(frame) || !channel.Poll())return false;
            resume_incoming_start.clear();return Readiness();
        }
        if(launch.host && resume_neo_offset<resume_serialized.size() && !channel.HasPendingWrites()){
            if(!CrossplayResumeNeo::Encode(*peer,resume_checkpoint,resume_neo_sequence+1,resume_neo_offset,resume_serialized,frame)
                || !channel.Send(frame))return false;
            ++resume_neo_sequence;resume_neo_offset+=static_cast<uint32_t>(frame.size()-CrossplayResumeNeo::Header);
        }
        if(!ResumeNativeReady())return phase!=Phase::Failed;
        if(!FlushSavedGuestReady())return false;
        if(!world_neo_bound){
            CrossplayNeo::Selection selection{resume_checkpoint.features,resume_checkpoint.enabled,resume_checkpoint.content,resume_checkpoint.setup,resume_checkpoint.neo};
            if(!neo_world_agreement.Bind(*peer,selection) || !neo_world_agreement.Offer(frame) || !channel.Send(frame))return false;
            world_neo_bound=true;
        }
        while(!world_neo_frames.empty()){
            std::vector<unsigned char> response;
            if(!neo_world_agreement.Consume(world_neo_frames.front(),response)
                || (!response.empty() && !channel.Send(response)))return false;
            world_neo_frames.pop_front();
        }
        if(!neo_world_agreement.Ready())return true;
        if(!PrepareSavedWorld())return false;
        if(!launch.host && !resume_prepared_sent){
            if(!CrossplayResumeAdmission::Encode(*peer,resume_checkpoint,CrossplayResumeAdmission::Kind::Prepared,frame)
                || !channel.Send(frame))return false;resume_prepared_sent=true;
        }
        if(launch.host && resume_peer_prepared && !resume_start_sent && !held_start_wire.empty()){
            if(!CrossplayResumeStart::Encode(*peer,resume_checkpoint,admission.OwnConnection(),resume_token,held_start_wire,frame)
                || !channel.Send(frame))return false;resume_start_sent=true;
        }
        return true;
    }
