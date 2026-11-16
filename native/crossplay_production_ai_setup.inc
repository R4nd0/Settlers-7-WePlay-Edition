    // One original host chooser transaction, slots3/4 only. Neither received
    // metadata nor a CPU cache alone grants root/start/world authority.
    bool ai_layout=false;
    std::array<std::vector<unsigned char>,2> ai_profiles{};
    std::array<CrossplayAIPreparation::Value,2> ai_values{};
    unsigned ai_captured=0,ai_profiles_received=0,ai_values_received=0,ai_applied=0;
    uint32_t ai_pending=0;
    ULONGLONG ai_pending_at=0;
    CrossplayAIProfile::ContextSnapshot ai_context{};
    bool AIReadback(){
        return ai_layout && ai_values[0].slot==3 && ai_values[1].slot==4
            && ai_values[0].profile!=ai_values[1].profile
            && CrossplayAIPreparation::Readback(launch.channel.edition,original,LocalConnections(),launch.host,ai_values[0])
            && CrossplayAIPreparation::Readback(launch.channel.edition,original,LocalConnections(),launch.host,ai_values[1]);
    }
    bool GoldAIProfile(void* manager,const void* event,uint32_t mode){
        using namespace CrossplayNativeCodec;
        if(!Detail::Readable(event,4) || *static_cast<const uintptr_t*>(event)!=CrossplayAIProfile::Pin(launch.channel.edition).event_vt)return false;
        uintptr_t sender=0;std::vector<unsigned char> wire;CrossplayAIProfile::Value value{};
        if(mode || !launch.host || !StartInterval() || start_started || ai_captured>=2 || !start_slots.empty() || !start_teams.empty()
            || manager!=Detail::Transport(launch.channel.edition,sender)
            || !CrossplayAIProfile::Capture(original,LocalConnections(),event,wire)
            || !CrossplayAIProfile::Wire(wire,admission.OwnConnection(),value) || value.slot!=3+ai_captured
            || (ai_captured && value.profile==CrossplayChat::Word(ai_profiles[0].data()+17))){Fail();return true;}
        ai_layout=true;ai_profiles[ai_captured++]=std::move(wire);return true;
    }
    bool ReceiveAISetup(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame){
        if(ai_profiles_received<2){
            std::vector<unsigned char> wire;CrossplayAIProfile::Value value{};
            if(!CrossplayAIProfile::Receive(peer,ai_profiles_received,admission.PeerSourceConnection(),LocalConnections().host,frame,wire)
                || !CrossplayAIProfile::Wire(wire,LocalConnections().host,value)
                || (ai_profiles_received && value.profile==CrossplayChat::Word(ai_profiles[0].data()+17)))return false;
            ai_profiles[ai_profiles_received++]=std::move(wire);return true;
        }
        CrossplayAIPreparation::Value value{};
        if(ai_values_received>=2 || !CrossplayAIPreparation::Receive(peer,ai_values_received,frame,value)
            || value.profile!=CrossplayChat::Word(ai_profiles[ai_values_received].data()+17))return false;
        ai_values[ai_values_received++]=value;return true;
    }
    bool PumpAISetup(){
        using namespace CrossplayNativeCodec;
        const auto ids=LocalConnections();const auto edition=launch.channel.edition;
        if(launch.host){
            if(held_start_wire.empty() || ai_captured!=2)return true;
            const auto* peer=channel.GetPeer();CrossplaySessionConfig::Layout layout;std::vector<unsigned char> frame;
            for(unsigned i=0;i<2;++i)if(!CrossplayAIPreparation::Capture(original,ids,3+i,CrossplayChat::Word(ai_profiles[i].data()+17),ai_values[i]))return false;
            CrossplayAIPreparation::PreparationScope scope(edition,original,ids,true,ai_values);
            if(!peer || !AIReadback() || !CaptureGoldSessionConfig(original,ids,start_expected)
                || !CrossplayAIProfile::FreshConfig(start_expected,ids,layout)
                || !GameStartDetail::FreshReadyMap(edition,original,ids,true,bootstrap.map)
                || !CrossplaySessionConfig::Encode(*peer,start_expected,ids,frame) || !channel.Send(frame))return false;
            for(unsigned i=0;i<2;++i)if(!CrossplayAIProfile::Frame(*peer,i+1,ai_profiles[i],ids.host,frame) || !channel.Send(frame))return false;
            for(unsigned i=0;i<2;++i)if(!CrossplayAIPreparation::Frame(*peer,i+1,ai_values[i],frame) || !channel.Send(frame))return false;
            start_sent=true;return true;
        }
        if(!start_config_queued){
            auto* cache=ReadyStateDetail::HistoryConfiguration(edition);if(!cache)return false;
            start_interval={owner,session,reinterpret_cast<uintptr_t>(cache)};
            if(!start_receipt.Arm(start_interval,ids,bootstrap.map,start_expected,GetTickCount64()))return false;
            start_config_queued=true;
            if(!EnqueueSessionConfig(edition,original,ids,start_expected))return false;
        }
        if(!start_receipt.HasConsumed() || ai_profiles_received!=2 || ai_values_received!=2)return true;
        if(ai_pending){
            if(GetTickCount64()-ai_pending_at>5000)return false;
            if(!CrossplayAIProfile::CpuObserved(edition,original,ids,ai_pending,ai_context))return true;
            unsigned stage=0;
            if(!CrossplayAIPreparation::Apply(edition,original,ids,ai_values[ai_applied],stage))return false;
            ai_pending=0;++ai_applied;
        }
        if(ai_applied<2){
            if(!CrossplayAIProfile::Deliver(edition,original,ids,ai_profiles[ai_applied],ai_context))return false;
            ai_pending=3+ai_applied;ai_pending_at=GetTickCount64();return true;
        }
        std::vector<unsigned char> actual;
        if(!AIReadback() || !CrossplayConfigurationNative::Snapshot(edition,original,ids,start_interval,actual))return false;
        const auto observed=start_receipt.Observe(start_interval,actual,GetTickCount64());
        if(observed==CrossplayConfigurationReceipt::Observation::Invalid)return false;
        start_applied=observed==CrossplayConfigurationReceipt::Observation::Applied;return true;
    }
