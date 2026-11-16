    // Fresh four-position, two-human original start transition only. The held
    // event is not replayed here. Root/content agreement is a separate stage.
    bool gold_local_installed=false,start_hold_installed=false;
    ULONGLONG start_started=0,start_pending_at=0;
    bool start_sent=false,start_received=false,start_config_queued=false,start_applied=false;
    std::vector<unsigned char> held_start_wire,start_expected;
    std::deque<std::vector<unsigned char>> start_slots,start_teams;
    std::deque<std::vector<unsigned char>> start_frames;
    uint32_t start_slots_received=0,start_teams_received=0,start_slots_applied=0,start_teams_applied=0;
    uint32_t start_pending_slot=0,start_pending_team=0;
    uintptr_t start_pending_session=0,start_pending_root=0;
    CrossplayConfigurationReceipt::Interval start_interval;
    CrossplayConfigurationReceipt::Receipt start_receipt;
    static bool StartFrame(const std::vector<unsigned char>& frame){
        const unsigned char config[8]={'S','7','S','C','F','G',0,1};
        return frame.size()>=8 && (!memcmp(frame.data(),config,8)
            || !memcmp(frame.data(),CrossplayClosedSlot::Magic,8) || !memcmp(frame.data(),CrossplayNeutralSetup::Magic,8));
    }
    bool StartInterval(){
        if(!Owned() || phase!=Phase::RosterBound || bootstrap.saved || !neo_installed
            || !CrossplayNeoSelection::Valid(neo_selection) || !ready_requests.empty() || !ready_states.empty()
            || ready_pending.configuration || ready_projection.session || !neo_selections.empty())return false;
        CrossplayNative::LobbySnapshot lobby;
        return original.CaptureLobby(lobby) && lobby.instance==session
            && CrossplayNativeCodec::SessionConfigDetail::Roster(original,LocalConnections(),launch.host);
    }
    void GoldLocal(void* manager,const void* event,uint32_t mode){
        if(!Owned() || phase!=Phase::RosterBound || !launch.host || mode || !configuration_sent)return;
        using namespace CrossplayNativeCodec;
        if(!Detail::Readable(event,4) || *static_cast<const uintptr_t*>(event)!=CrossplayClosedSlot::Vtable(launch.channel.edition))return;
        uintptr_t sender=0;std::vector<unsigned char> wire;uint32_t slot=0;
        if(!StartInterval() || start_sent || start_slots.size()>=2 || manager!=Detail::Transport(launch.channel.edition,sender)
            || !CrossplayClosedSlot::Capture(original,LocalConnections(),event,wire)
            || !CrossplayClosedSlot::Wire(wire,admission.OwnConnection(),slot) || slot!=3+start_slots.size()){Fail();return;}
        if(!start_started)start_started=GetTickCount64();start_slots.push_back(std::move(wire));
    }
    void GoldNeutralTeam(void* manager,const void* event,uint32_t mode,uint32_t target){
        if(!launch.host || !configuration_sent || (mode!=2 && (mode!=1 || target!=admission.PeerLocalConnection())))return;
        using namespace CrossplayNativeCodec;PlayerOptionDetail::Spec spec{};
        if(!PlayerOptionDetail::Layout(launch.channel.edition,CrossplayPlayerOption::Team,spec)
            || !Detail::Readable(event,24) || *static_cast<const uintptr_t*>(event)!=spec.vt)return;
        const auto slot=CrossplayChat::Word(static_cast<const unsigned char*>(event)+16);if(slot<3 || slot>4)return;
        uintptr_t sender=0;std::vector<unsigned char> wire;uint32_t parsed=0,team=0;
        if(!StartInterval() || start_sent || start_teams.size()>=2 || manager!=Detail::Transport(launch.channel.edition,sender)
            || !CrossplayNeutralSetup::Capture(original,LocalConnections(),bootstrap.map,event,wire)
            || !CrossplayNeutralSetup::Wire(wire,admission.OwnConnection(),parsed,team) || parsed!=3+start_teams.size()){Fail();return;}
        if(!start_started)start_started=GetTickCount64();start_teams.push_back(std::move(wire));
    }
    bool HoldGoldStart(void* manager,const void* event){
        if(!launch.host || launch.channel.edition!=Crossplay::Edition::Gold || !StartInterval()
            || !configuration_sent || !held_start_wire.empty()
            || reinterpret_cast<uintptr_t>(manager)!=CrossplayNativeStart::GoldStartHold::Current().roots.manager)return false;
        const auto pin=CrossplayNativeCodec::GameStartDetail::Pin(launch.channel.edition);
        if(!CrossplayNativeCodec::GameStartDetail::Methods(launch.channel.edition)
            || !CrossplayNativeCodec::GameStartDetail::Source(pin,event,admission.OwnConnection())
            || !CrossplayNativeCodec::Detail::Encode(pin,event,held_start_wire)
            || !CrossplayGameStart::Inspect(held_start_wire,admission.OwnConnection()))return false;
        if(!start_started)start_started=GetTickCount64();return true;
    }
    bool ReceiveStartSetup(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame){
        if(launch.host || !configuration_applied || !StartInterval() || start_applied)return false;
        const auto ids=LocalConnections();
        if(!start_received){
            uint32_t own=UINT32_MAX;CrossplaySessionConfig::Layout layout;
            if(!CrossplayNative::ProjectConnection(peer.edition,admission.PeerSourceConnection(),peer.local_in_peer_namespace,own)
                || !CrossplaySessionConfig::Decode(peer,frame,{admission.PeerSourceConnection(),own},ids,start_expected)
                || !CrossplayNeutralSetup::Closed(start_expected,ids,bootstrap.map,layout))return false;
            start_received=true;start_started=GetTickCount64();return true;
        }
        std::vector<unsigned char> wire;
        if(frame.size()>=8 && !memcmp(frame.data(),CrossplayClosedSlot::Magic,8)){
            if(start_teams_received || !CrossplayClosedSlot::Receive(peer,start_slots_received,admission.PeerSourceConnection(),ids.host,frame,wire))return false;
            ++start_slots_received;start_slots.push_back(std::move(wire));return true;
        }
        if(start_slots_received!=2 || !CrossplayNeutralSetup::Receive(peer,start_teams_received,admission.PeerSourceConnection(),ids.host,frame,wire))return false;
        ++start_teams_received;start_teams.push_back(std::move(wire));return true;
    }
    bool StartConfigurationConsumed(void* cache,const unsigned char* event){
        if(!start_config_queued || start_receipt.HasConsumed() || launch.host)return false;
        using namespace CrossplayNativeCodec;std::vector<unsigned char> wire;const auto ids=LocalConnections();
        const auto pin=SessionConfigDetail::Pin(launch.channel.edition);
        if(!StartInterval() || reinterpret_cast<uintptr_t>(cache)!=start_interval.cache
            || !SessionConfigDetail::Source(pin,event,ids) || !Detail::Encode(pin,event,wire)
            || !start_receipt.Consumed(start_interval,wire,GetTickCount64()))Fail();
        return true;
    }
    bool PumpStartSetup(){
        if(!start_started)return true;
        if(!StartInterval())return false;
        if((launch.host && start_sent) || (!launch.host && start_applied))return true;
        if(GetTickCount64()-start_started>10000)return false;
        const auto ids=LocalConnections();const auto edition=launch.channel.edition;
        if(launch.host){
            if(start_sent || held_start_wire.empty() || start_slots.size()!=2 || start_teams.size()!=2)return true;
            const auto* peer=channel.GetPeer();CrossplaySessionConfig::Layout layout;std::vector<unsigned char> frame;
            if(!peer || !CrossplayNativeCodec::CaptureGoldSessionConfig(original,ids,start_expected)
                || !CrossplayNeutralSetup::Closed(start_expected,ids,bootstrap.map,layout)
                || !CrossplayNativeCodec::GameStartDetail::FreshReadyMap(edition,original,ids,true,bootstrap.map)
                || !CrossplaySessionConfig::Encode(*peer,start_expected,ids,frame) || !channel.Send(frame))return false;
            for(unsigned i=0;i<2;++i)if(!CrossplayClosedSlot::Frame(*peer,i+1,start_slots[i],ids.host,frame) || !channel.Send(frame))return false;
            for(unsigned i=0;i<2;++i)if(!CrossplayNeutralSetup::Frame(*peer,i+1,start_teams[i],ids.host,frame) || !channel.Send(frame))return false;
            start_sent=true;return true;
        }
        using namespace CrossplayNativeCodec;
        if(start_applied)return true;
        if(!start_config_queued){
            auto* cache=ReadyStateDetail::HistoryConfiguration(edition);if(!cache)return false;
            start_interval={owner,session,reinterpret_cast<uintptr_t>(cache)};
            if(!start_receipt.Arm(start_interval,ids,bootstrap.map,start_expected,GetTickCount64()))return false;
            start_config_queued=true;
            if(!EnqueueSessionConfig(edition,original,ids,start_expected))return false;
        }
        if(!start_receipt.HasConsumed())return true;
        if(start_pending_slot){
            if(GetTickCount64()-start_pending_at>10000)return false;
            if(!start_pending_team){
                uintptr_t s=0,c=0;
                if(!CrossplayClosedSlot::Context(edition,original,ids,start_pending_slot,true,s,c))return true;
                if(s!=start_pending_session || c!=start_pending_root
                    || !CrossplayClosedPreparation::Project(edition,original,ids,start_pending_slot))return false;
                ++start_slots_applied;
            }else{
                uint32_t team=0;uintptr_t s=0,rows=0;
                if(!CrossplayNeutralSetup::TeamValue(edition,original,ids,bootstrap.map,start_pending_slot,team,s,rows)
                    || s!=start_pending_session || rows!=start_pending_root)return false;
                if(team!=start_pending_team)return true;
                ++start_teams_applied;
            }
            start_pending_slot=start_pending_team=0;
        }
        if(!start_slots.empty()){
            uint32_t slot=0;
            if(!CrossplayClosedSlot::Wire(start_slots.front(),ids.host,slot) || slot!=3+start_slots_applied
                || !CrossplayClosedSlot::Deliver(edition,original,ids,start_slots.front(),start_pending_session,start_pending_root))return false;
            start_slots.pop_front();start_pending_slot=slot;start_pending_at=GetTickCount64();return true;
        }
        if(start_slots_applied!=2)return true;
        if(!start_teams.empty()){
            uint32_t slot=0,team=0,old=0;
            if(!CrossplayNeutralSetup::Wire(start_teams.front(),ids.host,slot,team) || slot!=3+start_teams_applied
                || !CrossplayNeutralSetup::TeamValue(edition,original,ids,bootstrap.map,slot,old,start_pending_session,start_pending_root)
                || !CrossplayNeutralSetup::Deliver(edition,original,ids,bootstrap.map,start_teams.front()))return false;
            start_teams.pop_front();start_pending_slot=slot;start_pending_team=team;start_pending_at=GetTickCount64();return true;
        }
        if(start_teams_applied!=2)return true;
        std::vector<unsigned char> actual;
        if(!CrossplayConfigurationNative::Snapshot(edition,original,ids,start_interval,actual))return false;
        const auto observed=start_receipt.Observe(start_interval,actual,GetTickCount64());
        if(observed==CrossplayConfigurationReceipt::Observation::Invalid)return false;
        start_applied=observed==CrossplayConfigurationReceipt::Observation::Applied;return true;
    }
    bool PumpStartFrames(){
        if(!start_frames.empty() && ready_states.empty() && !ready_projection.session){
            const auto* p=channel.GetPeer();if(!p)return false;const auto peer=*p;
            while(!start_frames.empty()){
                if(!ReceiveStartSetup(peer,start_frames.front()))return false;start_frames.pop_front();
            }
        }
        return PumpStartSetup();
    }
