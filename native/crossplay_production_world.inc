    CrossplayFreshStart::Barrier world_barrier;
    CrossplaySetupCheckpointNative::Checkpoint world_checkpoint;
    bool world_checkpoint_bound=false;
    ULONGLONG world_negotiation_started=0,world_checkpoint_started=0;
    bool world_neo_bound=false,world_bound=false,world_mode_prepared=false,world_math_installed=false;
    bool world_dispatch_ready=false;
    bool world_lua_begin=false,world_lua_ready=false;
    ULONGLONG world_dispatch_started=0;
    std::deque<std::vector<unsigned char>> world_control_frames,world_neo_frames,world_checkpoint_frames;
    CrossplayWorldEvents::Inbox world_events;
    uint32_t progress_sent=0,progress_received=0,turn_sent=0,turn_received=0,desync_sent=0,desync_received=0;
    uint32_t build_sent=0,build_received=0,road_sent=0,road_received=0,priority_sent=0,priority_received=0;
    unsigned world_diagnostic=0;
    static bool Magic(const std::vector<unsigned char>& frame,const unsigned char* magic){return frame.size()>=8 && !memcmp(frame.data(),magic,8);}
    bool WorldFrame(const std::vector<unsigned char>& frame,bool& recognized){
        recognized=true;
        const unsigned char start[8]={'S','7','S','T','A','R',0,1};
        if(CheckpointFrame(frame) && frame.size()==CrossplaySetupCheckpoint::FrameSize && CrossplayChat::Word(frame.data()+44)==2){
            if(neo_world_queued || !start_started || world_checkpoint_frames.size()>=4)return false;
            world_checkpoint_frames.push_back(frame);return true;
        }
        if(Magic(frame,CrossplayNeo::Magic)){
            if(neo_world_queued || world_neo_frames.size()>=4 || frame.size()!=CrossplayNeo::FrameSize)return false;
            world_neo_frames.push_back(frame);return true;
        }
        if(Magic(frame,CrossplayFreshStart::ReadyMagic) || Magic(frame,CrossplayFreshStart::QueuedMagic) || Magic(frame,start)){
            if(neo_world_queued || world_control_frames.size()>=4
                || (frame.size()!=CrossplayFreshStart::ReceiptSize && frame.size()!=CrossplayGameStart::FrameSize))return false;
            world_control_frames.push_back(frame);return true;
        }
        if(CrossplayWorldEvents::Identify(frame)==CrossplayWorldEvents::Kind::Unknown){recognized=false;return true;}
        if(!neo_world_queued && !world_barrier.CanBufferWorldEvents(GetTickCount64()))return false;
        return world_events.Push(frame);
    }
    bool FreshWorldRoot(){
        CrossplayAIPreparation::PreparationScope ai_scope(launch.channel.edition,original,LocalConnections(),launch.host,ai_values);
        if(ai_layout && !CrossplayAIPreparation::EligibleScope(launch.channel.edition,original,LocalConnections(),launch.host))return false;
        if(!Owned() || phase!=Phase::RosterBound || !setup_roots_verified[0] || !setup_roots_verified[1]
            || setup_checkpoint.GetPhase()!=CrossplaySetupCheckpoint::Phase::Confirmed || !StartInterval())return false;
        CrossplaySetupRootCapture::Snapshot fresh{};const auto ids=LocalConnections();
        CrossplayNative::LobbySnapshot lobby;
        if(!CrossplaySetupRootCapture::Capture(launch.channel.edition,original,ids,launch.host,fresh,2)
            || fresh.complete.digest!=setup_root_digest
            || !CrossplayNativeCodec::GameStartDetail::FreshReadyMap(launch.channel.edition,original,ids,launch.host,bootstrap.map)
            || !original.CaptureLobby(lobby) || lobby.instance!=session || lobby.members.size()!=2)return false;
        for(const auto& member:lobby.members)if(member.status!=2)return false;
        return true;
    }
    bool FreshWorldSetup(){
        return world_checkpoint_bound && world_checkpoint.Confirmed(original) && FreshWorldRoot();
    }
    bool PumpWorldCheckpoint(){
        const auto* other=channel.GetPeer();if(!other)return false;const auto peer=*other;
        std::vector<unsigned char> output;
        if(!world_checkpoint_bound){
            // Root exchange is complete. Start a separate, nonce-bound native
            // interval for world delivery; never extend or reuse its old lease.
            if(!FreshWorldRoot() || !world_checkpoint.Bind(launch.channel.edition,original,LocalConnections(),launch.host,peer,2))return false;
            world_checkpoint_bound=true;world_checkpoint_started=GetTickCount64();
            if(launch.host && (!world_checkpoint.Offer(original,output) || !channel.Send(output) || !world_checkpoint.Sent(original)))return false;
        }
        if(GetTickCount64()-world_checkpoint_started>CrossplaySetupCheckpoint::Lifetime)return false;
        while(!world_checkpoint_frames.empty()){
            if(!world_checkpoint.Receive(original,world_checkpoint_frames.front(),output))return false;
            world_checkpoint_frames.pop_front();
            if(!output.empty() && (!channel.Send(output) || !world_checkpoint.Sent(original)))return false;
        }
        return true;
    }
    bool ReleaseStartObservers(){
        if(!Owned() || phase!=Phase::RosterBound || !world_bound || neo_world_queued)return false;
        if(!launch.host)return !start_hold_installed && !gold_local_installed;
        // These observers own the preparation session only. The original start
        // may replace that session, so retire them before its native queue call.
        const auto& hold=CrossplayNativeStart::GoldStartHold::Current();
        if(!start_hold_installed || !gold_local_installed || !hold.attempted || !hold.captured || hold.failed
            || hold.roots.owner!=owner || hold.roots.session!=session || hold.host!=admission.OwnConnection()
            || CrossplayNativeObserver::GoldLocalObserver::Current().failed)return false;
        if(!CrossplayNativeStart::GoldStartHold::Release())return false;start_hold_installed=false;
        if(!CrossplayNativeObserver::GoldLocalObserver::Release())return false;gold_local_installed=false;
        return true;
    }
    bool PrepareWorldModules(){
        // Install/check local arithmetic before creating the short-lived world
        // checkpoint. These hooks remain inactive until their existing owned
        // world predicates accept. No Ready/start permission is granted here.
        if(world_mode_prepared && world_math_installed)return SharedEngineMode::Verify(launch.channel.edition,owner);
        if(!world_mode_prepared){
            bool changed=false;
            if(!SharedEngineMode::Prepare(launch.channel.edition,owner,changed) || !FreshWorldRoot())return false;
            world_mode_prepared=true;
        }
        if(!SharedEngineMode::Verify(launch.channel.edition,owner))return false;
        if(!world_math_installed){
            if(launch.channel.edition==Crossplay::Edition::Gold
                && (!GoldBuildingMath::Install(owner,&WorldMathReady) || !GoldResourceMath::Install(owner,&WorldMathReady)
                    || !GoldMovementWrapper::Install(owner,&NeoWorldReady)
                    // Path-point guards include the original movement-time
                    // routine; retain installation order before map loading.
                    || !GoldAnimalPathPoints::Install(owner,&NeoWorldReady)
                    || !GoldAnimalMovementTime::Install(owner,&NeoWorldReady)))return false;
            world_math_installed=true;
        }
        return FreshWorldRoot();
    }
    bool PumpWorldStart(){
        if(neo_world_queued || !setup_roots_verified[0] || !setup_roots_verified[1])return true;
        CrossplayAIPreparation::PreparationScope ai_scope(launch.channel.edition,original,LocalConnections(),launch.host,ai_values);
        if(ai_layout && !CrossplayAIPreparation::EligibleScope(launch.channel.edition,original,LocalConnections(),launch.host))return false;
        world_diagnostic=1;if(!FreshWorldRoot() || !NeoModules())return false;
        if(!PrepareWorldModules())return false;
        const auto* other=channel.GetPeer();if(!other)return false;const auto peer=*other;
        std::vector<unsigned char> output;
        if(!world_neo_bound){
            CrossplayNeo::Selection choice{};choice.features=CrossplayNeoSelection::Features;choice.enabled=neo_selection.enabled;
            choice.content=launch.channel.content;choice.setup=setup_root_digest;
            if(!neo_world_agreement.Bind(peer,choice) || !neo_world_agreement.Offer(output) || !channel.Send(output))return false;
            world_neo_bound=true;world_negotiation_started=GetTickCount64();
        }
        if(!neo_world_agreement.Ready() && GetTickCount64()-world_negotiation_started>10000)return false;
        while(!world_neo_frames.empty()){
            if(!neo_world_agreement.Consume(world_neo_frames.front(),output))return false;world_neo_frames.pop_front();
            if(!output.empty() && !channel.Send(output))return false;
        }
        if(!neo_world_agreement.Ready())return GetTickCount64()-world_negotiation_started<=10000;
        world_diagnostic=5;if(!PumpWorldCheckpoint())return false;
        if(world_checkpoint.GetPhase()!=CrossplaySetupCheckpoint::Phase::Confirmed)return true;
        if(!FreshWorldSetup())return false;
        CrossplayLogisticsActive::selected=neo_world_agreement.Value().enabled;
        if(!world_bound){
            if(!world_barrier.Bind(peer,launch.host,{1,setup_root_digest},launch.channel.content,GetTickCount64())
                || !world_barrier.Ready(GetTickCount64(),output) || !channel.Send(output) || !world_barrier.Sent(GetTickCount64()))return false;
            neo_world_session=session;world_bound=true;
        }
        world_diagnostic=3;
        while(!world_control_frames.empty()){
            auto input=std::move(world_control_frames.front());world_control_frames.pop_front();
            if(Magic(input,CrossplayFreshStart::ReadyMagic)){
                if(!world_barrier.ReceiveReady(GetTickCount64(),input))return false;
            }else if(launch.host){
                if(!world_barrier.HostAttempt(GetTickCount64(),input) || !FreshWorldSetup()
                    || !CrossplayGameStart::Inspect(held_start_wire,admission.OwnConnection()) || !ReleaseStartObservers())return false;
                const bool queued=CrossplayNativeCodec::GameStartDetail::DecodeAndEnqueue(launch.channel.edition,held_start_wire,admission.OwnConnection());
                if(!world_barrier.HostQueued(GetTickCount64(),queued))return false;
                neo_world_queued=true;world_dispatch_started=GetTickCount64();return true;
            }else{
                std::vector<unsigned char> wire;
                if(!FreshWorldSetup() || !world_barrier.GuestAttempt(GetTickCount64(),input,admission.PeerSourceConnection(),admission.PeerLocalConnection(),wire)
                    || !ReleaseStartObservers())return false;
                const bool queued=CrossplayNativeCodec::GameStartDetail::DecodeAndEnqueue(launch.channel.edition,wire,admission.PeerLocalConnection());
                if(!world_barrier.GuestQueued(GetTickCount64(),queued,output) || !channel.Send(output)
                    || !world_barrier.Sent(GetTickCount64()) || !channel.Poll())return false;
                neo_world_queued=true;world_dispatch_started=GetTickCount64();return true;
            }
        }
        if(launch.host && world_barrier.GetPhase()==CrossplayFreshStart::Phase::Agreed){
            if(!world_barrier.Start(GetTickCount64(),held_start_wire,admission.OwnConnection(),output)
                || !channel.Send(output) || !world_barrier.Sent(GetTickCount64()))return false;
        }
        return true;
    }
    bool WorldDispatchReady(){
        if(!Owned() || !neo_world_queued || !neo_world_agreement.Ready() || !NeoModules())return false;
        if(!CrossplayNativeCodec::LoadProgressDetail::World(original,owner,neo_world_session,admission.OwnConnection(),admission.PeerLocalConnection()))return false;
        world_dispatch_ready=true;
        if(!CrossplayLogisticsActive::Advance()){
            const auto reason=CrossplayLogisticsActive::pending_reason;
            if(CrossplayLogisticsActive::begun || (reason!=3 && reason!=4))Fail();
        }
        return phase!=Phase::Failed;
    }
    // A disconnected channel revokes event/NEO authority, but does not destroy
    // the original engine's still-running world. Its Ready observer must keep
    // recognizing the same authenticated AI rows during native teardown.
    // ParticipantEvent::Reset destroys our temporary event, not its pinned
    // read-only engine binding. Re-read the actual session and roster below.
    bool StartedWorldObservation()const{
        return Owned() && (phase==Phase::RosterBound
                || (phase==Phase::Failed && world_lua_ready && world_dispatch_ready))
            && world_bound && neo_world_queued
            && (!bootstrap.saved || (resume_world_prepared && resume_neo_armed && resume_bound))
            && neo_world_session==session
            && neo_world_agreement.Ready() && NeoModules()
            && CrossplayLogisticsActive::selected==neo_world_agreement.Value().enabled
            && CrossplayNativeCodec::LoadProgressDetail::World(original,owner,neo_world_session,
                admission.OwnConnection(),admission.PeerLocalConnection());
    }
    bool WorldNeoScope()const{
        return phase==Phase::RosterBound && channel.GetState()==Crossplay::State::Connected
            && StartedWorldObservation();
    }
    bool WorldNeoSelection(std::string& output)const{
        if(!WorldNeoScope())return false;
        output=neo_world_agreement.Value().enabled?"1":"0";return true;
    }
    bool WorldNeoCommand(const std::string& command,std::string& output){
        if(!WorldNeoScope())return false;
        if(command=="load"){
            if(!bootstrap.saved || !resume_world_prepared || !resume_neo_armed)return false;
            world_lua_begin=true;output="1";return true;
        }
        if(command=="restore-network"){
            if(!bootstrap.saved || !world_lua_begin || !resume_neo_armed || !CrossplayLogisticsActive::Advance()
                || !CrossplayLogisticsActive::begun || !CrossplayLogisticsActive::resume_restored
                || CrossplayLogisticsActive::resume_unrestored_comparisons
                || CrossplayLogisticsActive::ledger.enabled!=resume_checkpoint.enabled)return false;
            world_lua_ready=true;output="1";return true;
        }
        if(command=="start-event"){
            // Acknowledges an already consumed original start only. This query
            // cannot enqueue a start or manufacture native session readiness.
            output="1";return true;
        }
        if(command=="begin:0" || command=="begin:1"){
            if((command.back()=='1')!=neo_world_agreement.Value().enabled)return false;
            world_lua_begin=true;output="1";return true;
        }
        if(command=="ready"){
            if(!world_lua_begin || !CrossplayLogisticsActive::Advance() || !CrossplayLogisticsActive::begun
                || CrossplayLogisticsActive::ledger.enabled!=neo_world_agreement.Value().enabled)return false;
            world_lua_ready=true;output="1";return true;
        }
        if(command=="status"){
            if(!world_lua_ready || !CrossplayLogisticsActive::begun || !CrossplayLogisticsActive::Advance()
                || CrossplayLogisticsActive::ledger.enabled!=neo_world_agreement.Value().enabled)return false;
            output=CrossplayLogisticsActive::ledger.enabled?"1":"0";return true;
        }
        uint32_t logic_turn=0;
        if(world_lua_ready && CrossplayNeoSnapshot::LogicTurn(command,logic_turn))
            return CrossplayNeoSnapshot::Capture(launch.channel.edition,owner,logic_turn,output);
        return false;
    }
    bool OutgoingWorld(void* manager,const void* event,uint32_t mode,uint32_t target){
        if(!neo_world_queued || !Owned() || phase==Phase::Failed || !mode
            || (mode!=2 && (mode!=1 || target!=admission.PeerLocalConnection())))return false;
        using namespace CrossplayNativeCodec;
        if(!Detail::Readable(event,4))return false;
        const auto edition=launch.channel.edition;const auto table=*static_cast<const uintptr_t*>(event);
        const bool progress=table==LoadProgressDetail::Pin(edition).event_vt,turn=table==TurnBatchDetail::Pin(edition).event_vt,desync=table==DesyncTurnDetail::Pin(edition).event_vt;
        const bool logic=table==BuildCommandDetail::Pin(edition).event_vt;
        if(!progress && !turn && !desync && !logic)return false;
        uintptr_t sender=0;const auto* p=channel.GetPeer();std::vector<unsigned char> wire,frame;
        if(!p || manager!=Detail::Transport(edition,sender)){Fail();return true;}const auto peer=*p;
        const auto own=admission.OwnConnection(),other=admission.PeerLocalConnection();bool okay=false;
        if(progress && progress_sent<256)okay=LoadProgressDetail::Encode(edition,original,owner,neo_world_session,own,other,event,wire)
            && CrossplayLoadProgress::Encode(peer,progress_sent+1,wire,own,frame);
        if(turn && turn_sent<UINT32_MAX)okay=TurnBatchDetail::Encode(edition,original,owner,neo_world_session,own,other,event,wire)
            && CrossplayTurnBatch::Encode(peer,turn_sent+1,wire,own,frame);
        if(desync && desync_sent<CrossplayDesyncTurn::MaxSequence)okay=DesyncTurnDetail::Encode(edition,original,owner,neo_world_session,own,other,event,wire)
            && CrossplayDesyncTurn::Encode(peer,desync_sent+1,wire,own,frame);
        const uint32_t player=launch.host?1u:2u;
        const bool build=logic && BuildCommandDetail::Source(edition,event,own,player);
        const bool road=logic && RoadCommandDetail::Source(edition,event,own,player);
        const bool priority=logic && PriorityCommandDetail::Source(edition,event,own,player);
        if(build && build_sent<UINT32_MAX)okay=BuildCommandDetail::Encode(edition,original,owner,neo_world_session,own,other,player,event,wire)
            && CrossplayBuildCommand::Encode(peer,build_sent+1,wire,own,player,frame);
        if(road && road_sent<UINT32_MAX)okay=RoadCommandDetail::Encode(edition,original,owner,neo_world_session,own,other,player,event,wire)
            && CrossplayRoadCommand::Encode(peer,road_sent+1,wire,own,player,frame);
        if(priority && priority_sent<UINT32_MAX && (neo_world_agreement.Value().features&CrossplayNeo::PriorityCommands))
            okay=PriorityCommandDetail::Encode(edition,original,owner,neo_world_session,own,other,player,event,wire)
                && CrossplayPriorityCommand::Encode(peer,priority_sent+1,wire,own,player,frame);
        if(!okay || !channel.Send(frame)){Fail();return true;}
        if(progress)++progress_sent;if(turn)++turn_sent;if(desync)++desync_sent;
        if(build)++build_sent;if(road)++road_sent;if(priority)++priority_sent;return true;
    }
    bool PumpWorldEvents(){
        if(!neo_world_queued)return true;
        world_diagnostic=4;
        if(!WorldDispatchReady())return phase!=Phase::Failed && !world_dispatch_ready && GetTickCount64()-world_dispatch_started<=10000;
        const auto* other=channel.GetPeer();if(!other)return false;const auto peer=*other;
        const auto edition=launch.channel.edition;const auto own=admission.OwnConnection(),remote=admission.PeerLocalConnection();
        using namespace CrossplayWorldEvents;const uint32_t player=launch.host?2u:1u;
        for(unsigned budget=0;budget<16 && !world_events.Empty();++budget){
            if(!world_events.Deliver(peer,admission.PeerSourceConnection(),remote,player,[&](Kind kind,const std::vector<unsigned char>& wire){
                using namespace CrossplayNativeCodec;
                switch(kind){
                case Kind::Progress:return LoadProgressDetail::Enqueue(edition,original,owner,neo_world_session,own,remote,wire);
                case Kind::Turn:return TurnBatchDetail::Enqueue(edition,original,owner,neo_world_session,own,remote,wire);
                case Kind::Desync:return DesyncTurnDetail::Enqueue(edition,original,owner,neo_world_session,own,remote,wire);
                case Kind::Build:return BuildCommandDetail::Enqueue(edition,original,owner,neo_world_session,own,remote,player,wire);
                case Kind::Road:return RoadCommandDetail::Enqueue(edition,original,owner,neo_world_session,own,remote,player,wire);
                case Kind::Priority:return (neo_world_agreement.Value().features&CrossplayNeo::PriorityCommands)
                    && PriorityCommandDetail::Enqueue(edition,original,owner,neo_world_session,own,remote,player,wire);
                default:return false;
                }
            }))return false;
            progress_received=world_events.Received(Kind::Progress);turn_received=world_events.Received(Kind::Turn);
            desync_received=world_events.Received(Kind::Desync);build_received=world_events.Received(Kind::Build);
            road_received=world_events.Received(Kind::Road);priority_received=world_events.Received(Kind::Priority);
        }
        return true;
    }
