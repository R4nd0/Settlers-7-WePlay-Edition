#include "neo_distance_policy.h"

uintptr_t neo_distance_original=0,neo_distance_accept=0,neo_distance_reject=0;
uint64_t neo_distance_choices=0,neo_distance_changes=0,neo_distance_errors=0;
#ifdef S7_LOCAL_PAIR_LAB
// The control is laboratory-only; production uses the saved NEO map choice.
bool neo_distance_control=false;
#endif

// The native pathfinder modifies shared work buffers and graph nodes. Read
// the compressed road graph and reuse only the native read-only access tests.
uint32_t __cdecl NeoRoadSector(uint32_t sector,uint32_t) { return sector; }

bool NeoRoadDistance(uint32_t start,uint32_t end,uint32_t player,float& distance) {
    if (!start || !end || player>=25) return false;
    const auto logic=LabPriorityWord(NativeVA(0x13a5220));
    const auto search=LabPriorityWord(logic+0x144);
    if (search<0x10000 || LabPriorityWord(search+0xc3550)<0x10000 ||
        !LabPriorityWord(search+0xc3558)) return false;
    const auto graph=LabPriorityWord(search+0xc3560);
    if (graph<0x10000 || LabPriorityWord(graph)!=NativeVA(0x1141564)) return false;

    const auto node_begin=LabPriorityWord(graph+0x44),node_end=LabPriorityWord(graph+0x48);
    const auto road_begin=LabPriorityWord(graph+0x54),road_end=LabPriorityWord(graph+0x58);
    const auto valid=[](uintptr_t begin,uintptr_t end) {
        return begin>=0x10000 && end>=begin && (end-begin)%8==0 && (end-begin)/8<=65536;
    };
    if (!valid(node_begin,node_end) || !valid(road_begin,road_end)) return false;
    const auto lookup=[](uintptr_t begin,uintptr_t end,uint32_t id) -> uintptr_t {
        const auto index=id&0xffffu;
        if (!id || index>=(end-begin)/8 || LabPriorityWord(begin+index*8)!=id) return 0;
        return LabPriorityWord(begin+index*8+4);
    };
    const auto compressed=[](uintptr_t node) {
        return node>=0x10000 && LabPriorityWord(node)==NativeVA(0x1141600);
    };
    if (!compressed(lookup(node_begin,node_end,start)) || !compressed(lookup(node_begin,node_end,end))) return false;

    // The sector reader calls the supplied callback and returns its EAX.
    // Give it a private context so the game never changes its access table.
    uintptr_t context[0x8c/4]{};
    context[0x88/4]=reinterpret_cast<uintptr_t>(&NeoRoadSector);
    const auto sector_of=Native<uint32_t(__thiscall*)(void*,const void*,const void*,const void*)>(0x9a0cd0);
    const auto has_access=Native<bool(__fastcall*)(uint32_t,uint32_t)>(0x6ff010);
    bool accessible[64]{};
    for (uint32_t sector=0;sector<64;++sector) accessible[sector]=has_access(player,sector);

    struct Step {
        double distance;
        uint32_t node;
        bool operator>(const Step& other) const {
            return distance!=other.distance ? distance>other.distance : node>other.node;
        }
    };
    std::vector<double> distances((node_end-node_begin)/8,std::numeric_limits<double>::infinity());
    std::priority_queue<Step,std::vector<Step>,std::greater<Step>> frontier;
    distances[start&0xffffu]=0;
    frontier.push({0,start});
    while (!frontier.empty()) {
        const auto step=frontier.top();
        frontier.pop();
        if (step.distance!=distances[step.node&0xffffu]) continue;
        if (step.node==end) {
            distance=static_cast<float>(step.distance);
            return std::isfinite(distance);
        }
        const auto node=lookup(node_begin,node_end,step.node);
        if (!compressed(node)) return false;
        const auto edges_begin=LabPriorityWord(node+0x28),edges_end=LabPriorityWord(node+0x2c);
        if (edges_begin==edges_end) continue;
        if (edges_begin<0x10000 || edges_end<edges_begin || (edges_end-edges_begin)%16 || edges_end-edges_begin>65536) return false;
        for (auto edge=edges_begin;edge<edges_end;edge+=16) {
            const auto target=LabPriorityWord(edge+8);
            const auto next=lookup(node_begin,node_end,target);
            const auto street=lookup(road_begin,road_end,LabPriorityWord(edge+4));
            const auto length=*reinterpret_cast<const float*>(edge+12);
            if (!compressed(next) || street<0x10000 || LabPriorityWord(street)!=NativeVA(0x1142e00) ||
                !std::isfinite(length) || length<0) return false;
            const auto sector=sector_of(reinterpret_cast<void*>(search),context,
                reinterpret_cast<const void*>(street),reinterpret_cast<const void*>(next+8));
            if (sector>=64) return false;
            if (!accessible[sector]) continue;
            const auto alternative=step.distance+length;
            auto& target_distance=distances[target&0xffffu];
            if (alternative<target_distance) {
                target_distance=alternative;
                frontier.push({alternative,target});
            }
        }
    }
    return false;
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
                    } else ++neo_distance_errors;
                } else ++neo_distance_errors;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ++neo_distance_errors;
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
