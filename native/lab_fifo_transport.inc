#include "neo_fifo_policy.h"
// Shared transport policy; the map-option build supplies a saved-state bridge.
neo_fifo::Mark neo_fifo_marks[65536]{};
bool neo_fifo_group_deliberate[25]{};
bool neo_fifo_enabled=false,neo_fifo_groups=false,neo_fifo_fresh=false;
uintptr_t neo_fifo_priority=0;
uint32_t neo_fifo_last_turn=0,neo_fifo_stats_turn=0,neo_fifo_trace_count=0;
uint64_t neo_fifo_comparisons=0,neo_fifo_changed=0,neo_fifo_explicit=0,neo_fifo_mark_count=0;
uint64_t neo_fifo_intent_errors=0,neo_fifo_compare_errors=0;
#ifdef S7_NEO_LOGISTICS
constexpr uint32_t NeoFifoStartTurn=0;
#else
constexpr uint32_t NeoFifoStartTurn=58000;
#endif
void NeoFifoLog(const char* value,int length) {
#ifdef S7_NEO_LOGISTICS
    (void)value; (void)length;
#else
    if(length>0 && static_cast<size_t>(length)<sizeof(lab_lua_console_records[0].bytes)
        && lab_lua_console_count<_countof(lab_lua_console_records)) {
        auto& out=lab_lua_console_records[lab_lua_console_count++];
        memcpy(out.bytes,value,static_cast<size_t>(length));out.length=static_cast<DWORD>(length);
    }
#endif
}
uintptr_t NeoFifoWorld(uint32_t& turn,uintptr_t* transfers=nullptr) {
    const auto replay=LabPriorityWord(NativeVA(0x13a5200));
    const auto logic=LabPriorityWord(NativeVA(0x13a5220));
    if(!replay || !logic || LabPriorityWord(replay)!=NativeVA(0x1133f40)
        || LabPriorityWord(logic)!=NativeVA(0x1141534))return 0;
    const auto main=LabPriorityWord(logic+0x14c);
    if(!main || LabPriorityWord(main)!=NativeVA(0x1147f40))return 0;
    const auto priority=LabPriorityWord(main+4);
    if(!priority || LabPriorityWord(priority)!=NativeVA(0x1148328))return 0;
    turn=LabPriorityWord(replay+0x2c);
    if(transfers)*transfers=LabPriorityWord(main+8);
    return priority;
}
bool NeoFifoOwner() {
#ifdef S7_NEO_LOGISTICS
    return initialization==2 && local_connect_probe
        && GetCurrentThreadId()==bootstrap_thread.load(std::memory_order_acquire);
#else
    return !steam_history && initialization==2 && local_connect_probe && lab_priority_audit_enabled
        && GetCurrentThreadId()==bootstrap_thread.load(std::memory_order_acquire);
#endif
}
void NeoFifoEpoch(uintptr_t priority,uint32_t turn) {
#ifdef S7_NEO_LOGISTICS
    NeoLogisticsEpoch(priority,turn);
#else
    if(priority!=neo_fifo_priority || turn<neo_fifo_last_turn) {
        memset(neo_fifo_marks,0,sizeof(neo_fifo_marks));memset(neo_fifo_group_deliberate,0,sizeof(neo_fifo_group_deliberate));
        neo_fifo_priority=priority;neo_fifo_fresh=turn<=1000;
    }
    neo_fifo_last_turn=turn;
#endif
}
bool NeoFifoKnownIntent(uint32_t caller) {
    switch(caller) {
    case 0x77abc1:case 0x7bbb9c:case 0x9e77f6:
    case 0xa145c9:case 0xa146d9:case 0xa147a5:case 0xa147de:return true;
    default:return false;
    }
}
int NeoFifoPosition(uintptr_t priority,uintptr_t task,uint32_t player,uint32_t group) {
    const auto slot=priority+player*0xacu+group*8u;
    const auto head=LabPriorityWord(slot+0x1c),size=LabPriorityWord(slot+0x20);
    if(head<0x10000 || size>8192)return -1;
    auto node=LabPriorityWord(head);
    for(uint32_t i=0;i<size && node!=head;++i) {
        if(node<0x10000)return -1;
        if(LabPriorityWord(node+8)==task)return static_cast<int>(i);
        node=LabPriorityWord(node);
    }
    return -1;
}
void TrackNeoFifoIntent(const uint32_t* frame,uint32_t kind) {
    if(!NeoFifoOwner())return;
    __try {
        uint32_t turn=0;const auto priority=NeoFifoWorld(turn);
        if(!priority || priority!=frame[6])return;
        NeoFifoEpoch(priority,turn);if(!neo_fifo_fresh)return;
        const auto caller=NeoLogisticsCaller(frame[9]);
        if(kind) {
            if(caller!=0xa13f39 && caller!=0x7bc59f)return;
            const uint32_t player=frame[11]&255u;
            if(player>=25)return;
            const auto b=LabPriorityWord(frame[10]),e=LabPriorityWord(frame[10]+4);
            if(b<0x10000 || e-b!=32)return;
            uint32_t mask=0;
            for(unsigned i=0;i<8;++i){const auto value=LabPriorityWord(b+i*4);if(value>=8 || (mask&(1u<<value)))return;mask|=1u<<value;}
            const auto old=LabPriorityWord(priority+8+player*0xacu);
            if(old>=0x10000 && memcmp(reinterpret_cast<const void*>(b),reinterpret_cast<const void*>(old),32))neo_fifo_group_deliberate[player]=true;
            return;
        }
        const auto id=frame[10],relative=frame[11];
        const auto task=LabPriorityResolve(priority,id);
        if(!task || LabPriorityWord(task+8)!=id || id==relative)return;
        const uint32_t player=*reinterpret_cast<const unsigned char*>(task+0x34),group=LabPriorityWord(task+0x1c);
        if(player>=25 || group>=8)return;
        auto mode=neo_fifo::Intent::Ordinary;
        bool explicit_call=NeoFifoKnownIntent(caller);
        if(!explicit_call && caller==0x7830ed) {
            // Native construction priority propagation recursively reorders its
            // supply tasks. Inherit the already tracked outer explicit intent.
            const auto outer=frame[2];
            if(outer>=0x10000 && NeoFifoKnownIntent(NeoLogisticsCaller(LabPriorityWord(outer+4)))) {
                const auto outer_id=LabPriorityWord(outer+8),outer_task=LabPriorityResolve(priority,outer_id);
                if(outer_task) {
                    const auto& mark=neo_fifo_marks[outer_id&65535u];
                    if(mark.Matches(outer_id,LabPriorityWord(outer_task+0x2c),outer_task)){mode=mark.intent;explicit_call=true;}
                }
            }
        }
        if(!explicit_call)return;
        const int position=NeoFifoPosition(priority,task,player,group);
        if(position<0)return;
        if(mode==neo_fifo::Intent::Ordinary) {
            if(!relative)mode=neo_fifo::Intent::Defer; // native zero means END
            else {
                const auto target=LabPriorityResolve(priority,relative);
                if(!target || LabPriorityWord(target+0x1c)!=group || *reinterpret_cast<const unsigned char*>(target+0x34)!=player)return;
                const int target_position=NeoFifoPosition(priority,target,player,group);
                if(target_position<0)return;
                mode=position>target_position?neo_fifo::Intent::Prefer:neo_fifo::Intent::Defer;
            }
        }
        auto& mark=neo_fifo_marks[id&65535u];const auto birth=LabPriorityWord(task+0x2c);
        if(!mark.Matches(id,birth,task) || mark.intent!=mode) {
            mark={id,birth,task,mode};++neo_fifo_mark_count;
            char line[192]{};const int n=std::snprintf(line,sizeof(line),"NEO1|fifo-mark|%u|%u|%d|%u|%08x|%u",turn,id,static_cast<int>(mode),birth,caller,group);NeoFifoLog(line,n);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {++neo_fifo_intent_errors;}
}
uintptr_t NeoFifoTask(uintptr_t priority,uintptr_t record) {
    return record>=0x10000?LabPriorityResolve(priority,LabPriorityWord(record+8)):0;
}
neo_fifo::Key NeoFifoKey(uintptr_t priority,uintptr_t transfers,uintptr_t record,uintptr_t task) {
    neo_fifo::Key key{neo_fifo::Intent::Prefer,static_cast<int32_t>(LabPriorityWord(task+0x14)),static_cast<int32_t>(LabPriorityWord(task+0x2c)),LabPriorityWord(task+8),LabPriorityWord(record)};
    if(LabPriorityWord(task)!=NativeVA(0x1148a68))return key; // preserve unsupported transfer kinds
    const auto b=LabPriorityWord(transfers+0x148),e=LabPriorityWord(transfers+0x14c),i=key.reservation&65535u;
    if(b<0x10000 || e<b || (e-b)%8 || i>=(e-b)/8 || LabPriorityWord(b+i*8)!=key.reservation || LabPriorityWord(b+i*8+4)!=record)return key;
    auto node=task;unsigned depth=0;int group=-1;auto intent=neo_fifo::Intent::Ordinary;
    const auto player=*reinterpret_cast<const unsigned char*>(task+0x34);
    if(player>=25)return key;
    while(node && depth++<32) {
        const auto id=LabPriorityWord(node+8),birth=LabPriorityWord(node+0x2c);
        if(LabPriorityResolve(priority,id)!=node)return key;
        const auto& mark=neo_fifo_marks[id&65535u];
        if(intent==neo_fifo::Intent::Ordinary && mark.Matches(id,birth,node))intent=mark.intent;
        const auto bucket=LabPriorityWord(node+0x20),parent=LabPriorityWord(node+0x24);
        if(group<0 && bucket==4 && LabPriorityWord(node)==NativeVA(0x1148b6c))group=4;
        if(!parent){if(group<0 && bucket<8)group=static_cast<int>(bucket);node=0;break;}
        const auto next=LabPriorityResolve(priority,parent);
        if(!next || LabPriorityWord(node+0x38)!=next)return key;
        node=next;
    }
    if(node || group<0)return key;
    key.group_order=group>=6?group-8:group;
#ifdef S7_NEO_LOGISTICS
    key.group_order=0; // no default group precedence, including explicit bands
#endif
    if(neo_fifo_groups) {
        const auto v=priority+8+player*0xacu,beg=LabPriorityWord(v),end=LabPriorityWord(v+4);
        if(beg>=0x10000 && end-beg==32) {
            uint32_t order[8]{};memcpy(order,reinterpret_cast<const void*>(beg),32);
            const int order_position=neo_fifo::GroupOrder(static_cast<unsigned>(group),order,neo_fifo_group_deliberate[player]);
            if(order_position>=0){key.grouped=true;key.group_order=order_position;}
        }
    }
    key.intent=intent;return key;
}
uint32_t __cdecl CompareNeoFifoTransfers(uintptr_t left,uintptr_t right) {
#ifdef S7_NEO_LOGISTICS
    const DWORD error=GetLastError();uint32_t decision=2; // execute original comparator
#else
    const DWORD error=GetLastError();uint32_t decision=0;
#endif
    __try {
        uint32_t turn=0;uintptr_t transfers=0;const auto priority=NeoFifoWorld(turn,&transfers);
        if(priority) {
            const auto a=NeoFifoTask(priority,left),b=NeoFifoTask(priority,right);
            if(a && b) {
#ifdef S7_NEO_LOGISTICS
                if(!NeoFifoOwner()) {SetLastError(error);return 2;}
                NeoFifoEpoch(priority,turn);
                if(!neo_fifo_enabled || !neo_fifo_fresh) {SetLastError(error);return 2;}
#endif
                const bool original=static_cast<int32_t>(LabPriorityWord(a+0x14))<static_cast<int32_t>(LabPriorityWord(b+0x14));decision=original?1u:0u;
                if(NeoFifoOwner()) {
                    NeoFifoEpoch(priority,turn);++neo_fifo_comparisons;
                    if(neo_fifo_enabled && neo_fifo_fresh && turn>=NeoFifoStartTurn) {
                        const auto ka=NeoFifoKey(priority,transfers,left,a),kb=NeoFifoKey(priority,transfers,right,b);
                        decision=neo_fifo::Before(ka,kb)?1u:0u;
                        if(ka.intent!=neo_fifo::Intent::Ordinary || kb.intent!=neo_fifo::Intent::Ordinary || ka.grouped || kb.grouped)++neo_fifo_explicit;
                        if((decision!=0)!=original) {
                            ++neo_fifo_changed;
#ifndef S7_NEO_LOGISTICS
                            if(neo_fifo_trace_count<128 && LabPriorityWord(left+0xc)==14 && LabPriorityWord(left+0x10)==65547
                                && LabPriorityWord(right+0xc)==14 && LabPriorityWord(right+0x10)==65547) {
                                ++neo_fifo_trace_count;char line[256]{};
                                const int n=std::snprintf(line,sizeof(line),"NEO1|fifo-choice|%u|%u|%u|%d|%d|%d|%d|%u|%u",turn,ka.request,kb.request,ka.created,kb.created,static_cast<int>(ka.intent),static_cast<int>(kb.intent),original?1u:0u,decision);NeoFifoLog(line,n);
                            }
#endif
                        }
                    }
                    if(turn>=neo_fifo_stats_turn+1000) {
                        neo_fifo_stats_turn=turn;char line[256]{};
                        const int n=std::snprintf(line,sizeof(line),"NEO1|fifo-stats|%u|%u|%u|%llu|%llu|%llu|%llu|%llu|%llu",turn,neo_fifo_enabled?1u:0u,neo_fifo_groups?1u:0u,neo_fifo_comparisons,neo_fifo_changed,neo_fifo_explicit,neo_fifo_mark_count,neo_fifo_intent_errors,neo_fifo_compare_errors);NeoFifoLog(line,n);
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ++neo_fifo_compare_errors;
#ifdef S7_NEO_LOGISTICS
        decision=2;
#else
        decision=0;
#endif
    }
    SetLastError(error);return decision;
}
#include "lab_fifo_compare_bridge.inc"
