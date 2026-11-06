#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}
namespace {
uint32_t fixture_selection=0,calls=0;
uint32_t* __fastcall SelectionFixture(void*,void*,uint32_t* out,uint32_t) {
    ++calls;*out=fixture_selection;SetLastError(777);return out;
}
}
int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool value){++checks;if(!value){++failures;std::printf("FAIL %u\n",checks);}};
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1106000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    image_base=reinterpret_cast<uintptr_t>(image);
    auto address=[](const void* p){return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));};
    auto put=[](uintptr_t p,uint32_t value){memcpy(reinterpret_cast<void*>(p),&value,4);};
    for(bool steam:{false,true}) {
        steam_history=steam;initialization=2;local_connect_probe=true;bootstrap_thread=GetCurrentThreadId();
        uint32_t replay[16]{},logic[0x160/4]{},main[4]{},priority[0x1140/4]{},slots[8]{};
        uint32_t task[0x78/4]{},child[0xdc/4]{},second[0xdc/4]{},children[2]{};
        put(NativeVA(0x13a5200),address(replay));put(NativeVA(0x13a5220),address(logic));
        replay[0]=NativeVA(0x1133f40);replay[11]=100;
        logic[0]=NativeVA(0x1141534);logic[0x14c/4]=address(main);
        main[0]=NativeVA(0x1147f40);main[1]=address(priority);priority[0]=NativeVA(0x1148328);
        priority[0x1134/4]=address(slots);priority[0x1138/4]=address(slots+8);
        slots[2]=0x10001;slots[3]=address(task);slots[4]=0x10002;slots[5]=address(child);
        task[0]=NativeVA(0x1148b6c);task[1]=4;task[2]=fixture_selection=0x10001;
        task[0x48/4]=13;task[0x4c/4]=65564;task[0x3c/4]=address(children);task[0x40/4]=address(children+1);
        child[0]=NativeVA(0x1148a68);child[2]=0x10002;child[0x38/4]=address(task);children[0]=address(child);
        auto* completed=reinterpret_cast<unsigned char*>(child)+0x85;
        check(!NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));
        *completed=1;check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));*completed=0;
        slots[6]=0x10003;slots[7]=address(second);memcpy(second,child,sizeof(child));second[2]=0x10003;
        children[1]=address(second);task[0x40/4]=address(children+2);*completed=1;
        check(!NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));
        reinterpret_cast<unsigned char*>(second)[0x85]=1;
        check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));
        task[0x40/4]=address(children+1);*completed=0;
        slots[4]^=0x10000;check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));slots[4]^=0x10000;
        child[0x38/4]=0;check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));child[0x38/4]=address(task);
        task[0x48/4]=11;check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));task[0x48/4]=13;
        task[0x4c/4]=65565;check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));task[0x4c/4]=65564;
        task[1]=3;check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));task[1]=4;
        task[0x54/4]=1;check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));task[0x54/4]=0;
        task[0x40/4]=address(children);check(NeoWorkshopPickupBlocked(address(priority),fixture_selection,65564));task[0x40/4]=address(children+1);
        auto* code=Native<unsigned char*>(0x79d6e0);code[0]=0xe9;
        const uint32_t jump=address(&SelectionFixture)-address(code+5);memcpy(code+1,&jump,4);
        neo_fifo_priority=address(priority);neo_fifo_last_turn=100;neo_fifo_enabled=false;neo_fifo_fresh=false;
        uint32_t out=0;check(SelectNeoWorkshopProduction(nullptr,nullptr,&out,65564)==&out && out==fixture_selection && GetLastError()==777);
        neo_fifo_enabled=true;neo_fifo_fresh=true;
        check(SelectNeoWorkshopProduction(nullptr,nullptr,&out,65564)==&out && !out && GetLastError()==777);
        *completed=1;check(SelectNeoWorkshopProduction(nullptr,nullptr,&out,65564)==&out && out==fixture_selection);*completed=0;
        // Only the temporary pickup veto changes: the saved order, stock,
        // request age, state, and explicit-priority ledger remain untouched.
        uint32_t task_before[_countof(task)],child_before[_countof(child)];
        memcpy(task_before,task,sizeof(task));memcpy(child_before,child,sizeof(child));
        for(unsigned i=0;i<100;++i)check(SelectNeoWorkshopProduction(nullptr,nullptr,&out,65564)==&out && !out);
        check(!memcmp(task_before,task,sizeof(task)) && !memcmp(child_before,child,sizeof(child)));
        const auto owner=bootstrap_thread.load();bootstrap_thread=owner+1;
        check(SelectNeoWorkshopProduction(nullptr,nullptr,&out,65564)==&out && out==fixture_selection);bootstrap_thread=owner;
        fixture_selection=0;check(SelectNeoWorkshopProduction(nullptr,nullptr,&out,65564)==&out && !out);
    }
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%s %u workshop pickup checks (%u failures, %u native selections)\n",failures?"FAIL":"PASS",checks,failures,calls);
    return failures?1:0;
}
