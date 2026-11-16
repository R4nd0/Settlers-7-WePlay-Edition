    CrossplaySetupCheckpointNative::Checkpoint setup_checkpoint;
    bool setup_checkpoint_bound=false,setup_checkpoint_done=false;
    ULONGLONG setup_checkpoint_started=0;
    unsigned setup_diagnostic=0;
    std::deque<std::vector<unsigned char>> setup_checkpoint_frames,setup_root_frames;
    std::array<CrossplaySetupRootStream::Receiver,2> setup_receivers{};
    std::array<CrossplaySetupRootStream::Descriptor,2> setup_descriptors{};
    std::array<bool,2> setup_roots_bound{},setup_roots_verified{};
    Crossplay::Digest setup_root_digest{};
    static bool CheckpointFrame(const std::vector<unsigned char>& frame){
        const unsigned char magic[8]={'S','7','C','K','P','T',0,1};return frame.size()>=8 && !memcmp(frame.data(),magic,8);
    }
    static bool RootFrame(const std::vector<unsigned char>& frame){
        const unsigned char magic[8]={'S','7','R','O','O','T',0,1};return frame.size()>=8 && !memcmp(frame.data(),magic,8);
    }
    bool HostTeamsConsumed(){
        if(!launch.host)return start_applied;
        if(!start_sent || start_teams.size()!=2)return false;
        CrossplaySetupCheckpoint::Snapshot before{},after{};const auto ids=LocalConnections();
        if(!CrossplaySetupCheckpointNative::Detail::Local(launch.channel.edition,original,ids,true,before)
            || before.thread!=owner || before.session!=session || !before.preparation)return false;
        const auto* rows=reinterpret_cast<const unsigned char*>(before.preparation);
        if(!CrossplayNativeCodec::Detail::Readable(rows,25*0x5c4))return false;
        for(unsigned i=0;i<2;++i){
            uint32_t slot=0,team=0;
            if(!CrossplayNeutralSetup::Wire(start_teams[i],ids.host,slot,team) || slot!=3+i
                || rows[slot*0x5c4+0x68]!=team)return false;
        }
        return CrossplaySetupCheckpointNative::Detail::Local(launch.channel.edition,original,ids,true,after)
            && before.thread==after.thread && before.session==after.session && before.preparation==after.preparation
            && before.cache==after.cache && before.local==after.local;
    }
    bool PumpSetupRoot(){
        if(!start_started || (launch.host?!start_sent:!start_applied))return true;
        // These flags record a completed comparison only. They are not fresh
        // world authority; a later start must revalidate its native checkpoint.
        if(setup_roots_verified[0] && setup_roots_verified[1])return true;
        if(!StartInterval())return false;
        const auto* other=channel.GetPeer();if(!other)return false;const auto peer=*other;
        const auto ids=LocalConnections();const auto edition=launch.channel.edition;std::vector<unsigned char> output;
        if(!setup_checkpoint_bound){
            setup_diagnostic=1;
            if(!HostTeamsConsumed())return GetTickCount64()-start_started<=10000;
            if(!setup_checkpoint.Bind(edition,original,ids,launch.host,peer,1))return false;
            setup_checkpoint_bound=true;setup_checkpoint_started=GetTickCount64();
            if(launch.host && (!setup_checkpoint.Offer(original,output) || !channel.Send(output) || !setup_checkpoint.Sent(original)))return false;
        }
        setup_diagnostic=2;
        if(GetTickCount64()-setup_checkpoint_started>CrossplaySetupCheckpoint::Lifetime)return false;
        while(!setup_checkpoint_frames.empty()){
            if(!setup_checkpoint.Receive(original,setup_checkpoint_frames.front(),output))return false;
            setup_checkpoint_frames.pop_front();
            if(!output.empty() && (!channel.Send(output) || !setup_checkpoint.Sent(original)))return false;
        }
        if(setup_checkpoint.GetPhase()!=CrossplaySetupCheckpoint::Phase::Confirmed)return true;
        if(!setup_checkpoint.Confirmed(original))return false;setup_checkpoint_done=true;
        for(unsigned i=0;i<2;++i)if(!setup_roots_bound[i]){
            setup_diagnostic=10+i;CrossplaySetupRootCapture::Snapshot captured{};
            unsigned native_stage=0;std::vector<std::vector<unsigned char>> frames;
            if(!CrossplaySetupRootCapture::Capture(edition,original,ids,launch.host,captured,i+1,&native_stage)){
                setup_diagnostic=1000+native_stage;return false;
            }
            if(!setup_checkpoint.Confirmed(original)
                || !CrossplaySetupRootStream::Encode(peer,1,i+1,captured.serialized,frames,&setup_descriptors[i])
                || captured.complete.digest!=setup_descriptors[i].digest || captured.complete.bytes!=setup_descriptors[i].bytes
                || !setup_receivers[i].Bind(peer,setup_descriptors[i]))return false;
            setup_roots_bound[i]=true;
            for(const auto& frame:frames)if(!channel.Send(frame))return false;
        }
        while(!setup_root_frames.empty()){
            setup_diagnostic=20;const auto& frame=setup_root_frames.front();
            if(frame.size()<CrossplaySetupRootStream::Header)return false;
            const auto version=CrossplayChat::Word(frame.data()+44);if(version<1 || version>2)return false;
            const unsigned i=version-1;
            if(!setup_roots_bound[i] || setup_roots_verified[i] || !setup_checkpoint.Confirmed(original)
                || !setup_receivers[i].Accept(frame))return false;
            setup_root_frames.pop_front();
            if(setup_receivers[i].GetState()!=CrossplaySetupRootStream::Receiver::State::Complete)continue;
            CrossplaySetupRootStream::Complete incoming{};CrossplaySetupRootCapture::Snapshot fresh{};
            setup_diagnostic=30+i;unsigned native_stage=0;
            if(!setup_receivers[i].Take(incoming)
                || !CrossplaySetupRootCapture::Capture(edition,original,ids,launch.host,fresh,version,&native_stage)
                || incoming.bytes!=fresh.serialized || incoming.descriptor.digest!=fresh.complete.digest
                || !setup_checkpoint.Confirmed(original)){setup_diagnostic=2000+native_stage;return false;}
            if(version==2){
                setup_diagnostic=40;CrossplaySetupRootReader::Verification verification{};
                if(!CrossplaySetupRootReader::VerifyReceived(edition,original,ids,launch.host,version,incoming.bytes,verification)
                    || !verification.active_unchanged || !setup_checkpoint.Confirmed(original))return false;
                setup_root_digest=fresh.complete.digest;
            }
            setup_roots_verified[i]=true;
        }
        if(setup_roots_verified[0] && setup_roots_verified[1])setup_diagnostic=100;
        return true;
    }
