#include "neo_logistics_state.h"
#include <algorithm>

bool neo_map_pending=false, neo_map_pending_valid=false, neo_map_loading=false;
bool neo_map_installed=false;
uintptr_t neo_compare_resume=0,neo_compare_global=0;
void NeoLogisticsEpoch(uintptr_t priority,uint32_t turn);
uint32_t LabPriorityWord(uintptr_t p) { return *reinterpret_cast<const uint32_t*>(p); }
uintptr_t LabPriorityResolve(uintptr_t self,uint32_t id) {
    const auto b=LabPriorityWord(self+0x1134), e=LabPriorityWord(self+0x1138), i=id&65535u;
    if(b<0x10000 || e<b || e-b>0x80000 || (e-b)%8 || i>=(e-b)/8)return 0;
    return LabPriorityWord(b+i*8)==id?LabPriorityWord(b+i*8+4):0;
}
uint32_t NeoLogisticsCaller(uint32_t caller) {
    const auto va=caller-static_cast<uint32_t>(image_base)+0x400000u;
    if(!steam_history)return va;
    // Independently matched native callsites; automatic callers stay unmarked.
    static const uint32_t pairs[][2]={
        {0x77a991,0x77abc1},{0x7bb96c,0x7bbb9c},{0x9e71c6,0x9e77f6},
        {0xa13a49,0xa145c9},{0xa13b59,0xa146d9},{0xa13c25,0xa147a5},
        {0xa13c5e,0xa147de},{0x782ebd,0x7830ed},
        {0xa133b9,0xa13f39},{0x7bc36f,0x7bc59f}};
    for(const auto& pair:pairs)if(va==pair[0])return pair[1];
    return 0;
}
