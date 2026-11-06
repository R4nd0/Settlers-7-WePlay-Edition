#include "neo_distance_policy.h"

uintptr_t neo_distance_original=0,neo_distance_accept=0,neo_distance_reject=0;
uint64_t neo_distance_choices=0,neo_distance_changes=0,neo_distance_errors=0;
#ifdef S7_LOCAL_PAIR_LAB
// The control is laboratory-only; production uses the saved NEO map choice.
bool neo_distance_control=false;
#endif

// 787BB0 uses ECX/EDX for output/profile and caller-cleans three stack words.
// A uniform native movement profile makes BOTH its edge cost and admissible
// straight-line heuristic measure road length. Native access checks still run.
bool NeoRoadDistance(uint32_t start,uint32_t end,uint32_t player,float& distance) {
    if (!start || !end || player>=25) return false;
    float profile[27];for(auto& value:profile)value=1.0f;
    const auto target=NativeVA(0x787bb0);
    const auto output=&distance;
    uint32_t result=0;distance=-1;
    __asm {
        push player
        push end
        push start
        lea edx,profile
        mov ecx,output
        call target
        add esp,12
        movzx eax,al
        mov result,eax
    }
    return result && std::isfinite(distance) && distance>=0;
}
bool NeoDistanceStore(uintptr_t logic,uintptr_t object,uint32_t player,uint32_t& id,uint32_t& node) {
    if(object<0x10000 || LabPriorityWord(object)!=NativeVA(0x1143264)
        || *reinterpret_cast<const unsigned char*>(object+8)!=player)return false;
    id=LabPriorityWord(object+0x208);node=LabPriorityWord(object+0x24);
    const auto manager=LabPriorityWord(logic+0x3c);
    const auto b=LabPriorityWord(manager+0x20),e=LabPriorityWord(manager+0x24),i=id&65535u;
    return id && node && b>=0x10000 && e>=b && (e-b)%8==0 && (e-b)/8<=65536
        && i<(e-b)/8 && LabPriorityWord(b+i*8)==id && LabPriorityWord(b+i*8+4)==object;
}
bool NeoDistanceRoute(uint32_t source,uint32_t destination,uint32_t via,uint32_t id,
    uint32_t player,neo_distance::Route& out) {
    out.store=id;
    return NeoRoadDistance(source,via,player,out.first)
        && NeoRoadDistance(via,destination,player,out.remaining) && out.Valid();
}
bool NeoDistanceProfileAllowsUniform(uintptr_t carrier) {
    // Preserve movement-class restrictions. A zero/invalid native coefficient
    // must never become a traversable class just because distance uses ones.
    const auto profile=Native<const float* (__thiscall*)(void*)>(0x6f6c20)(reinterpret_cast<void*>(carrier));
    if(reinterpret_cast<uintptr_t>(profile)<0x10000)return false;
    for(unsigned i=0;i<27;++i)if(!std::isfinite(profile[i]) || profile[i]<=0)return false;
    return true;
}
uint32_t __cdecl ChooseNeoDistance(uintptr_t frame,uintptr_t candidate) {
    const auto error=GetLastError();uint32_t decision=2; // native comparator
    bool queried=false;
    __try {
        uint32_t turn=0;const auto priority=NeoFifoOwner()?NeoFifoWorld(turn):0;
        if(priority) {
            NeoFifoEpoch(priority,turn);
            if(neo_fifo_enabled && neo_fifo_fresh
#ifdef S7_LOCAL_PAIR_LAB
                && !neo_distance_control
#endif
                ) {
                const auto logic=LabPriorityWord(NativeVA(0x13a5220));
                const auto source=LabPriorityWord(frame+8),destination=LabPriorityWord(frame+12);
                const auto carrier=LabPriorityWord(frame+16),best=LabPriorityWord(frame-0x30);
                const auto player=carrier>=0x10000?*reinterpret_cast<const unsigned char*>(carrier+8):255u;
                uint32_t source_id=0,destination_id=0,candidate_id=0,best_id=0;
                uint32_t source_node=0,destination_node=0,candidate_node=0,best_node=0;
                if(NeoDistanceStore(logic,source,player,source_id,source_node)
                    && NeoDistanceStore(logic,destination,player,destination_id,destination_node)
                    && NeoDistanceStore(logic,candidate,player,candidate_id,candidate_node)
                    && candidate!=source && candidate!=destination
                    && (!best || NeoDistanceStore(logic,best,player,best_id,best_node))
                    && NeoDistanceProfileAllowsUniform(carrier)) {
                    // This hook is AFTER all native candidate gates: finished,
                    // same owner, carriers present, reachable/progressing route,
                    // and the original total-cost/slack bound.
                    neo_distance::Route proposed{},previous{};
                    queried=true;
                    if(NeoDistanceRoute(source_node,destination_node,candidate_node,candidate_id,player,proposed)
                        && (!best || NeoDistanceRoute(source_node,destination_node,best_node,best_id,player,previous))) {
                        decision=neo_distance::Before(proposed,previous)?1u:0u;++neo_distance_choices;
                        const float native_best=*reinterpret_cast<const float*>(frame-0x2c);
                        const float native_first=*reinterpret_cast<const float*>(frame-0x28);
                        if((decision!=0)!=(native_best>native_first))++neo_distance_changes;
#ifdef S7_LOCAL_PAIR_LAB
                        char line[256]{};
                        const int n=std::snprintf(line,sizeof(line),"NEO1|distance|%u|%u|%u|%u|%u|%.3f|%.3f|%u",
                            turn,source_id,destination_id,candidate_id,best_id,proposed.Total(),previous.Total(),decision);
                        if(neo_distance_choices<=256 && n>0 && n<static_cast<int>(sizeof(line))
                            && lab_lua_console_count<_countof(lab_lua_console_records)) {
                            auto& record=lab_lua_console_records[lab_lua_console_count++];
                            memcpy(record.bytes,line,static_cast<size_t>(n));record.length=static_cast<DWORD>(n);
                        }
#endif
                    } else {++neo_distance_errors;decision=0;}
                } else ++neo_distance_errors;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ++neo_distance_errors;
        // A native search mutates its own scratch graph. Never continue a world
        // after an exceptional exit with possibly incomplete search cleanup.
        if(queried)Stop(NativeGameObjectMismatch);
        decision=2;
    }
    SetLastError(error);return decision;
}
__declspec(naked) void NeoDistanceBridge() {
    __asm {
        pushfd
        pushad
        mov edi,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        push dword ptr [edi]
        push dword ptr [edi+8]
        call ChooseNeoDistance
        add esp,8
        cmp eax,2
        je original_distance
        test eax,eax
        jz reject_distance
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        jmp dword ptr [neo_distance_accept]
reject_distance:
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        jmp dword ptr [neo_distance_reject]
original_distance:
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        movss xmm0,dword ptr [ebp-0x2c]
        jmp dword ptr [neo_distance_original]
    }
}
