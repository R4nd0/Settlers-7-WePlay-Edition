void NeoLogisticsClear() {
    memset(neo_fifo_marks,0,sizeof(neo_fifo_marks));
    neo_fifo_enabled=false; neo_fifo_groups=false; neo_fifo_fresh=false;
    neo_fifo_priority=0; neo_fifo_last_turn=0;
    neo_fifo_intent_errors=0; neo_fifo_compare_errors=0;
}
void NeoLogisticsEpoch(uintptr_t priority,uint32_t turn) {
    if(priority!=neo_fifo_priority || turn<neo_fifo_last_turn) {
        memset(neo_fifo_marks,0,sizeof(neo_fifo_marks));
        neo_fifo_priority=priority;
        neo_fifo_enabled=neo_map_pending_valid && neo_map_pending && turn<=1;
        neo_fifo_fresh=neo_fifo_enabled;
    }
    neo_fifo_last_turn=turn;
}
uint32_t NeoLogisticsMode() {
    const auto manager=LabPriorityWord(NativeVA(0x13a5210));
    const auto state=manager>=0x10000?LabPriorityWord(manager+0x14):0;
    return state>=0x10000?LabPriorityWord(state+0x28):UINT32_MAX;
}
bool NeoSavePath(const std::string& name,std::string& path) {
    // The same native CWindowsFileSystem root that resolves GetSaveGameFileName
    // in the original loader. Never scan profiles or accept caller directories.
    if((name.size()!=5 && name.size()!=9) || name.compare(0,4,"s100") || name[4]<'1' || name[4]>'9'
        || (name.size()==9 && name.compare(5,4,".s7s")))return false;
    const auto root=LabPriorityWord(NativeVA(0x13789a8));
    if(root<0x10000 || LabPriorityWord(root)!=NativeVA(0x11337c4)
        || LabPriorityWord(root+0x74)!=NativeVA(0x113652c))return false;
    const auto data=LabPriorityWord(root+0x7c),length=LabPriorityWord(root+0x84);
    if(data<0x10000 || !length || length>1024)return false;
    const auto* bytes=reinterpret_cast<const char*>(data);
    if(bytes[length] || memchr(bytes,0,length))return false;
    path.assign(bytes,length);path+='\\';path+=name;if(name.size()==5)path+=".s7s";return true;
}
bool StageNeoSavedFile(const std::string& name,uint32_t choice) {
    std::string path;if(!NeoSavePath(name,path))return false;
    std::ifstream file(path,std::ios::binary);if(!file)return false;
    bool corrected=false;
    if(choice>1)return false;
    const auto payload=neo_save::Read(file,choice==1,&corrected);
    {std::lock_guard<std::mutex> lock(neo_wire_state_mutex);neo_wire_local_state=payload;neo_wire_local=choice;}
    neo_map_pending=choice==1;neo_map_pending_valid=true;
    if(corrected)ReportLocalStatus("neo-logistics-saved-turn-normalized");
    ReportLocalStatus("neo-logistics-saved-state-staged");return true;
}
bool NeoLogisticsExecute(const std::string& command,std::string& output,bool& is_string) {
    if(command=="capabilities") {output="NEO1"; is_string=true;return neo_map_installed;}
#ifdef S7_LOCAL_PAIR_LAB
    // Deterministic test roles without relying on profile/persona names. This
    // command is absent from original-executable production builds.
    if(command=="probe-instance") {
        const auto slot=local_pair_bind.load();
        if(slot!=0x7f000002u && slot!=0x7f000003u)return false;
        output=slot==0x7f000002u?"A":"B";is_string=true;return true;
    }
#endif
    if(command=="end") {
        NeoLogisticsClear();neo_map_pending_valid=false;neo_map_loading=false;
        neo_wire_local=0;neo_wire_received=2;neo_wire_confirmed=false;neo_wire_start_choice=2;ResetNeoWireState();return true;
    }
    if(command=="stage:0" || command=="stage:1") {
        // A menu may retain the previous world's priority system. Staging
        // changes only the next match, never that world's scheduler/ledger.
        neo_map_pending=command.back()=='1';neo_map_pending_valid=true;
        {std::lock_guard<std::mutex> lock(neo_wire_state_mutex);neo_wire_local=neo_map_pending?1:0;neo_wire_local_state.clear();}return true;
    }
    if(command.compare(0,11,"stage-file:")==0)
        return command.size()>13 && (command[11]=='0' || command[11]=='1') && command[12]==':'
            && StageNeoSavedFile(command.substr(13),command[11]=='1'?1u:0u);
    if(command=="load") {
        NeoLogisticsClear();neo_map_pending_valid=false;neo_map_loading=true;return true;
    }
    const auto mode=NeoLogisticsMode();if(mode!=1 && mode!=2) {
        if(command=="restore-network")ReportLocalStatus("neo-logistics-restore-mode-rejected");
        return false;
    }
    if(command=="start-event")return mode==2 && AcceptNeoNetworkStart();
    if(command=="network-state") {
        uint32_t choice=2;if(mode!=2 || !NeoNetworkChoice(choice))return false;
        output=choice?"1":"0";is_string=true;return true;
    }
    if(command=="selection") {
        uint32_t choice=neo_wire_local.load();
        if(mode==2 && !NeoNetworkChoice(choice))return false;
        output=choice?"1":"0";is_string=true;return true;
    }
    uint32_t turn=0;const auto priority=NeoFifoWorld(turn);
    if(command=="begin:0" || command=="begin:1") {
        if(priority && turn>1)return false;
        if(mode==2) {
            uint32_t choice=2;
            if(!NeoNetworkChoice(choice,true) || choice!=(command.back()=='1'?1u:0u))return false;
        }
        const bool enabled=command.back()=='1';
        // Priority commands may already have arrived during world creation.
        // Retain those marks only for this exact, fresh native world.
        const bool prepared=priority && priority==neo_fifo_priority && turn<=1
            && neo_fifo_last_turn<=1 && neo_map_pending_valid && neo_map_pending==enabled
            && neo_fifo_enabled==enabled;
        if(!prepared)NeoLogisticsClear();neo_map_pending=enabled;
        neo_map_pending_valid=true;neo_map_loading=false;return true;
    }
    if(command=="legacy") {
        if(!neo_map_loading || !priority)return false;
        NeoLogisticsClear();neo_fifo_priority=priority;neo_fifo_last_turn=turn;
        neo_map_loading=false;return true;
    }
    if(command=="ready") {
        if(!neo_map_pending_valid || !priority || turn>1)return false;
        NeoFifoEpoch(priority,turn);neo_map_pending_valid=false;
        ReportLocalStatus(neo_fifo_enabled?"neo-logistics-started":"neo-logistics-off");return true;
    }
    if(command=="status") {
        if(!priority || neo_fifo_intent_errors || neo_fifo_compare_errors)return false;
        output=neo_fifo_enabled && neo_fifo_fresh?"1":"0";is_string=true;return true;
    }
    if(command=="snapshot" || command.compare(0,14,"save-snapshot:")==0) {
        if(!priority || neo_map_loading || neo_fifo_intent_errors || neo_fifo_compare_errors)return false;
        neo_logistics::SavedState state;state.enabled=neo_fifo_enabled && neo_fifo_fresh;state.turn=turn;
        if(command!="snapshot") {
            size_t pos=14;int64_t savedTurn=0;const auto text=command+'\n';
            if(!neo_logistics::Number(text,pos,'\n',0,UINT32_MAX,savedTurn) || pos!=text.size()
                || static_cast<uint64_t>(savedTurn)+1!=turn)return false;
            if(mode==2)state.logic_turn=savedTurn;
        }
        if(state.enabled)for(const auto& mark:neo_fifo_marks) {
            if(!mark.id || mark.intent==neo_fifo::Intent::Ordinary)continue;
            const auto task=LabPriorityResolve(priority,mark.id);
            if(task && mark.Matches(mark.id,LabPriorityWord(task+0x2c),task))
            {
                state.marks.push_back({mark.id,static_cast<int32_t>(mark.created),mark.intent});
            }
        }
        std::sort(state.marks.begin(),state.marks.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        output=neo_logistics::Encode(state);is_string=true;return !output.empty();
    }
    constexpr char restore[]="restore:";
    if(command=="restore-network") {
        std::string payload;
        if(mode!=2) {ReportLocalStatus("neo-logistics-restore-mode-rejected");return false;}
        if(!NeoNetworkSavedState(payload))return false;
        return NeoLogisticsExecute(std::string(restore)+payload,output,is_string);
    }
    if(command.compare(0,sizeof(restore)-1,restore)==0) {
        if(!neo_map_loading) {ReportLocalStatus("neo-logistics-restore-not-loading");return false;}
        if(!priority) {ReportLocalStatus("neo-logistics-restore-world-missing");return false;}
        neo_logistics::SavedState saved;
        if(!neo_logistics::Decode(command.substr(sizeof(restore)-1),saved)) {ReportLocalStatus("neo-logistics-restore-payload-invalid");return false;}
        // Game.GetCurrentTurn() and SavegameInfo.LogicTurn are replayTurn - 1
        // in both executable profiles. The live replay counter is the next turn.
        if(saved.enabled && (saved.turn!=turn || (saved.logic_turn>=0 && static_cast<uint64_t>(saved.logic_turn)+1!=turn))) {
            ReportLocalStatus("neo-logistics-restore-turn-mismatch");return false;
        }
        if(mode==2) {
            uint32_t choice=2;
            if(!NeoNetworkChoice(choice) || choice!=(saved.enabled?1u:0u)) {ReportLocalStatus("neo-logistics-restore-choice-mismatch");return false;}
        }
        std::vector<neo_fifo::Mark> bound;
        for(const auto& mark:saved.marks) {
            const auto task=LabPriorityResolve(priority,mark.id);
            if(!task || LabPriorityWord(task+8)!=mark.id
                || static_cast<int32_t>(LabPriorityWord(task+0x2c))!=mark.created) {ReportLocalStatus("neo-logistics-restore-task-mismatch");return false;}
            bound.push_back({mark.id,static_cast<uint32_t>(mark.created),task,mark.intent});
        }
        NeoLogisticsClear();
        for(const auto& mark:bound)neo_fifo_marks[mark.id&65535u]=mark;
        neo_fifo_priority=priority;neo_fifo_last_turn=turn;
        neo_fifo_enabled=saved.enabled;neo_fifo_fresh=saved.enabled;
        neo_map_pending_valid=false;neo_map_loading=false;
        ReportLocalStatus(saved.enabled?"neo-logistics-restored":"neo-logistics-off");return true;
    }
    return false;
}
bool NeoLogisticsExecuteGuarded(const std::string& command,std::string& output,bool& is_string) {
    __try {return NeoLogisticsExecute(command,output,is_string);}
    __except(GetExceptionCode()==0xe06d7363u?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER) {
        neo_fifo_enabled=false;return false;
    }
}
int TryNeoLogisticsConsole(void* state) {
    if(!state || !NeoFifoOwner() || !neo_map_installed)return 0;
    const DWORD incoming=GetLastError();size_t length=0;
    const auto text=Native<const char* (__cdecl*)(void*,int,size_t*)>(0x45b650)(state,1,&length);
    constexpr char prefix[]="weplay-neo-v1:";
    if(!text || length<sizeof(prefix)-1 || memcmp(text,prefix,sizeof(prefix)-1)) {SetLastError(incoming);return 0;}
    bool result=false,is_string=false;std::string output;
    if(length<=neo_logistics::MaximumStateBytes+32 && !memchr(text,0,length)) {
        try {result=NeoLogisticsExecuteGuarded(std::string(text+sizeof(prefix)-1,length-(sizeof(prefix)-1)),output,is_string);}
        catch(const std::exception&) {neo_fifo_enabled=false;result=false;}
    }
    if(result && is_string)Native<void (__cdecl*)(void*,const char*,size_t)>(0x45b790)(state,output.data(),output.size());
    else Native<void (__cdecl*)(void*,int)>(0x44c6e0)(state,result?1:0);
    SetLastError(incoming);return 1;
}
int __cdecl NeoLogisticsConsole(void* state) {
    const DWORD incoming=GetLastError();const auto handled=TryNeoLogisticsConsole(state);
    SetLastError(incoming);if(handled)return handled;
#ifdef S7_LOCAL_PAIR_LAB
    if(!steam_history)return ObserveLabLuaConsole(state);
#endif
    return Native<int (__cdecl*)(void*)>(0x5d1550)(state);
}
void __cdecl ObserveNeoLogisticsPriority(const uint32_t* frame,uint32_t kind) {
    const DWORD incoming=GetLastError();
    if(neo_fifo_enabled || neo_map_pending_valid)TrackNeoFifoIntent(frame,kind);
    SetLastError(incoming);
}
#define ObserveLabPriorityEntry ObserveNeoLogisticsPriority
#include "lab_priority_bridge.inc"
#undef ObserveLabPriorityEntry
#include "neo_logistics_group.inc"
