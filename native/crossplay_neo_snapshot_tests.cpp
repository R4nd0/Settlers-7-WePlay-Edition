#define S7_CROSSPLAY_LOGISTICS_TESTING
#include "crossplay_neo_snapshot.h"
#include <cstdio>
#include <cstdlib>
namespace {
uint32_t logic[0x200/4]{},main_object[8]{},priority[0x1660/4]{},transfers[0x1b0/4]{};
uint32_t tasks[2][0x100/4]{},registry[4]{};uintptr_t logic_pointer=0;
unsigned checks=0,clock_reads=0,ready_calls=0,mutation=0;bool authorized=true;
CrossplayNativeClock::Snapshot clock_value{0x12340000,400,5,5};
void Check(bool value){++checks;if(!value){printf("FAIL NEO snapshot check%u\n",checks);exit(1);}}
void Set(uint32_t* object,unsigned offset,uintptr_t value){object[offset/4]=static_cast<uint32_t>(value);}
bool Ready(){
    ++ready_calls;
    if(mutation==5 && ready_calls==3)return false;
    if(mutation==6 && ready_calls==3)CrossplayLogisticsActive::selected=false;
    if(mutation==7 && ready_calls==3)logic[0]^=4;
    return authorized;
}
bool Clock(CrossplayNativeClock::Snapshot& output){
    ++clock_reads;output=clock_value;
    if(clock_reads==3){
        if(mutation==1)++output.turn;
        if(mutation==2)output.root+=4;
        if(mutation==3)output.pending=100;
        if(mutation==4)return false;
    }
    return true;
}
void Reset(Crossplay::Edition edition){
    using namespace CrossplayLogisticsBinding;
    memset(logic,0,sizeof(logic));memset(main_object,0,sizeof(main_object));memset(priority,0,sizeof(priority));memset(transfers,0,sizeof(transfers));memset(tasks,0,sizeof(tasks));
    pin=Pin(edition);logic_pointer=reinterpret_cast<uintptr_t>(logic);pin.global=reinterpret_cast<uintptr_t>(&logic_pointer);
    logic[0]=pin.logic;Set(logic,0x14c,reinterpret_cast<uintptr_t>(main_object));main_object[0]=pin.main;
    Set(main_object,4,reinterpret_cast<uintptr_t>(priority));Set(main_object,8,reinterpret_cast<uintptr_t>(transfers));priority[0]=pin.priority;
    Set(priority,pin.task_registry,reinterpret_cast<uintptr_t>(registry));Set(priority,pin.task_registry+4,reinterpret_cast<uintptr_t>(registry+4));
    if(pin.wrapper)Set(transfers,pin.wrapper,reinterpret_cast<uintptr_t>(transfers)+pin.wrapper_value);
    for(unsigned i=0;i<2;++i){
        tasks[i][0]=pin.request;Set(tasks[i],8,65536+i);Set(tasks[i],0x2c,200+i);Set(tasks[i],0x34,1);
        registry[i*2]=65536+i;registry[i*2+1]=reinterpret_cast<uintptr_t>(tasks[i]);
    }
    CrossplayLogisticsBinding::World world{};Check(CaptureWorld(world));
    auto& ledger=CrossplayLogisticsActive::ledger;
    Check(ledger.Begin(world,1,true));Check(ledger.Advance(world,400));
    Check(ledger.Mark(65537,neo_fifo::Intent::Defer));Check(ledger.Mark(65536,neo_fifo::Intent::Prefer));
    using namespace CrossplayLogisticsActive;
    CrossplayLogisticsActive::edition=edition;owner=GetCurrentThreadId();installed=true;begun=true;selected=true;
    ready=&Ready;test_clock=&Clock;resume_armed=false;resume_restored=false;
    authorized=true;clock_value={0x12340000,400,5,5};mutation=0;clock_reads=ready_calls=0;
}
}
int main(){
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}){
        Reset(edition);std::string output;const auto owner=GetCurrentThreadId();
        unsigned char native_before[sizeof(tasks)]{};memcpy(native_before,tasks,sizeof(tasks));
        const auto mxcsr=_mm_getcsr();SetLastError(0xabcdef12);
        Check(CrossplayNeoSnapshot::Capture(edition,owner,399,output));Check(GetLastError()==0xabcdef12 && _mm_getcsr()==mxcsr);
        neo_logistics::SavedState saved;Check(neo_logistics::Decode(output,saved));
        Check(output.compare(0,5,"NEO2\n")==0 && saved.enabled && saved.logic_turn==399 && saved.turn==400 && saved.marks.size()==2);
        Check(saved.marks[0].id==65536 && saved.marks[0].intent==neo_fifo::Intent::Prefer && saved.marks[1].intent==neo_fifo::Intent::Defer);
        Check(!memcmp(native_before,tasks,sizeof(tasks)));
        for(uint32_t wrong:{0u,398u,400u,UINT32_MAX}){
            output="stale";Check(!CrossplayNeoSnapshot::Capture(edition,owner,wrong,output) && output.empty());
        }
        for(unsigned invalid=0;invalid<9;++invalid){
            Reset(edition);using namespace CrossplayLogisticsActive;
            if(invalid==0)installed=false;if(invalid==1)begun=false;if(invalid==2)CrossplayLogisticsActive::owner^=1;
            if(invalid==3)ready=nullptr;if(invalid==4)authorized=false;if(invalid==5)selected=false;
            if(invalid==6)resume_armed=true;if(invalid==7)CrossplayLogisticsActive::edition=static_cast<Crossplay::Edition>(0);
            output="stale";Check(!CrossplayNeoSnapshot::Capture(edition,invalid==8?0:owner,399,output) && output.empty());
        }
        for(unsigned changed=1;changed<=7;++changed){
            Reset(edition);mutation=changed;output="stale";
            Check(!CrossplayNeoSnapshot::Capture(edition,owner,399,output) && output.empty());
            Check(!memcmp(native_before,tasks,sizeof(tasks)));
        }
        Reset(edition);registry[0]^=65536;Check(CrossplayNeoSnapshot::Capture(edition,owner,399,output));
        Check(neo_logistics::Decode(output,saved) && saved.marks.size()==1 && saved.marks[0].id==65537);
        Reset(edition);CrossplayLogisticsBinding::World world{};Check(CrossplayLogisticsBinding::CaptureWorld(world));
        Check(CrossplayLogisticsActive::ledger.Begin(world,1,false));Check(CrossplayLogisticsActive::ledger.Advance(world,400));CrossplayLogisticsActive::selected=false;
        Check(CrossplayNeoSnapshot::Capture(edition,owner,399,output) && output=="NEO0\n");
        Reset(edition);CrossplayLogisticsActive::resume_armed=true;CrossplayLogisticsActive::resume_restored=true;
        Check(CrossplayNeoSnapshot::Capture(edition,owner,399,output));
    }
    printf("PASS %u native NEO snapshot checks: both History layouts and Gold, full generational intent, exact logic/replay boundary, stable native clock/world/authority, stale-state rejection, FP/LastError and unchanged task bytes. Synthetic only; no save or reload executed.\n",checks);return 0;
}
