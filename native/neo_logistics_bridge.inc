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
        neo_wire_local=0;neo_wire_received=2;neo_wire_confirmed=false;neo_wire_start_choice=2;return true;
    }
    if(command=="stage:0" || command=="stage:1") {
        // A menu may retain the previous world's priority system. Staging
        // changes only the next match, never that world's scheduler/ledger.
        neo_map_pending=command.back()=='1';neo_map_pending_valid=true;
        neo_wire_local=neo_map_pending?1:0;return true;
    }
    if(command=="load") {
        NeoLogisticsClear();neo_map_pending_valid=false;neo_map_loading=true;return true;
    }
    const auto mode=NeoLogisticsMode();if(mode!=1 && mode!=2)return false;
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
    if(command=="snapshot") {
        if(!priority || neo_map_loading || neo_fifo_intent_errors || neo_fifo_compare_errors)return false;
        neo_logistics::SavedState state;state.enabled=neo_fifo_enabled && neo_fifo_fresh;state.turn=turn;
        if(state.enabled)for(const auto& mark:neo_fifo_marks) {
            if(!mark.id || mark.intent==neo_fifo::Intent::Ordinary)continue;
            const auto task=LabPriorityResolve(priority,mark.id);
            if(task && mark.Matches(mark.id,LabPriorityWord(task+0x2c),task))
                state.marks.push_back({mark.id,static_cast<int32_t>(mark.created),mark.intent});
        }
        std::sort(state.marks.begin(),state.marks.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        output=neo_logistics::Encode(state);is_string=true;return !output.empty();
    }
    constexpr char restore[]="restore:";
    if(command.compare(0,sizeof(restore)-1,restore)==0) {
        if(!neo_map_loading || !priority)return false;
        neo_logistics::SavedState saved;
        if(!neo_logistics::Decode(command.substr(sizeof(restore)-1),saved)
            || (saved.enabled && saved.turn!=turn))return false;
        if(mode==2) {
            uint32_t choice=2;
            if(!NeoNetworkChoice(choice) || choice!=(saved.enabled?1u:0u))return false;
        }
        std::vector<neo_fifo::Mark> bound;
        for(const auto& mark:saved.marks) {
            const auto task=LabPriorityResolve(priority,mark.id);
            if(!task || LabPriorityWord(task+8)!=mark.id
                || static_cast<int32_t>(LabPriorityWord(task+0x2c))!=mark.created)return false;
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
