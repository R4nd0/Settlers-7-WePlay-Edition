#define S7_GOLD_LOGISTICS_BINDING_TESTING
#define S7_CROSSPLAY_LOGISTICS_TESTING
#include "crossplay_logistics_intent.h"
#include <cstdio>
namespace {
uint32_t logic[0x200/4]{},main_object[8]{},priority[0x1660/4]{},transfers[0x1b0/4]{},tasks[2][0x100/4]{},registry[4]{},head[3]{},nodes[2][3]{};
uintptr_t global=0;unsigned checks=0;bool authenticated=true;uint32_t replay_clock=400;
void Set(uint32_t* object,unsigned offset,uintptr_t value){object[offset/4]=static_cast<uint32_t>(value);}
bool Ready(){return authenticated;}
bool Clock(CrossplayNativeClock::Snapshot& value){SetLastError(0xdeadbeef);value.turn=replay_clock;return true;}
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL line %u case %u\n",__LINE__,checks);return 1;}}while(0)
}
extern "C" __declspec(dllexport) uint32_t __cdecl SharedNeoIntentGuardOracle(uint32_t value){return CrossplayLogisticsIntent::OriginalGuards(static_cast<Crossplay::Edition>(value))?1u:0u;}
int main(){
 using namespace CrossplayLogisticsBinding;std::string identical;
 REQUIRE(!CrossplayLogisticsActive::Install(Crossplay::Edition::Gold,GetCurrentThreadId()+1,Ready));
 REQUIRE(!CrossplayLogisticsActive::installed);
 CrossplayLogisticsActive::installed=true;CrossplayLogisticsActive::edition=Crossplay::Edition::Gold;
 REQUIRE(!CrossplayLogisticsIntent::Install(Crossplay::Edition::SteamHistory,GetCurrentThreadId()));
 REQUIRE(!CrossplayLogisticsIntent::installed);CrossplayLogisticsActive::installed=false;
 for(unsigned profile=1;profile<=3;++profile){
  pin=Pin(static_cast<Crossplay::Edition>(profile));CrossplayLogisticsIntent::edition=static_cast<Crossplay::Edition>(profile);CrossplayLogisticsIntent::pin=CrossplayLogisticsIntent::Pin(CrossplayLogisticsIntent::edition);
  global=reinterpret_cast<uintptr_t>(logic);pin.global=reinterpret_cast<uintptr_t>(&global);
  memset(priority,0,sizeof(priority));memset(tasks,0,sizeof(tasks));
  logic[0]=pin.logic;Set(logic,0x14c,reinterpret_cast<uintptr_t>(main_object));main_object[0]=pin.main;
  Set(main_object,4,reinterpret_cast<uintptr_t>(priority));Set(main_object,8,reinterpret_cast<uintptr_t>(transfers));priority[0]=pin.priority;
  Set(priority,pin.task_registry,reinterpret_cast<uintptr_t>(registry));Set(priority,pin.task_registry+4,reinterpret_cast<uintptr_t>(registry+4));
  if(pin.wrapper)Set(transfers,pin.wrapper,reinterpret_cast<uintptr_t>(transfers)+pin.wrapper_value);
  for(unsigned i=0;i<2;++i){tasks[i][0]=pin.production;Set(tasks[i],8,65536+i);Set(tasks[i],0x2c,200+i);Set(tasks[i],0x34,1);registry[2*i]=65536+i;registry[2*i+1]=reinterpret_cast<uintptr_t>(tasks[i]);nodes[i][2]=reinterpret_cast<uintptr_t>(tasks[i]);}
  head[0]=reinterpret_cast<uintptr_t>(nodes[0]);head[1]=reinterpret_cast<uintptr_t>(nodes[1]);nodes[0][0]=reinterpret_cast<uintptr_t>(nodes[1]);nodes[0][1]=reinterpret_cast<uintptr_t>(head);nodes[1][0]=reinterpret_cast<uintptr_t>(head);nodes[1][1]=reinterpret_cast<uintptr_t>(nodes[0]);
  const auto p=CrossplayLogisticsIntent::pin;Set(priority,p.stride+p.head,reinterpret_cast<uintptr_t>(head));Set(priority,p.stride+p.size,2);
  World world{};REQUIRE(CaptureWorld(world));CrossplayLogisticsActive::ledger.Clear();REQUIRE(CrossplayLogisticsActive::ledger.Begin(world,1,true));REQUIRE(CrossplayLogisticsActive::ledger.Advance(world,400));CrossplayLogisticsActive::begun=true;
  CrossplayLogisticsActive::edition=static_cast<Crossplay::Edition>(profile);CrossplayLogisticsActive::owner=GetCurrentThreadId();CrossplayLogisticsActive::ready=Ready;CrossplayLogisticsActive::test_clock=Clock;
  SetLastError(0xabcdef12);REQUIRE(CrossplayLogisticsActive::Advance()&&GetLastError()==0xabcdef12);
  const uintptr_t caller=profile==1?0x9002e3:profile==2?0x77abc1:0x77a991;
  REQUIRE(CrossplayLogisticsIntent::Position(world,reinterpret_cast<uint32_t>(tasks[0]),1,0)==0);REQUIRE(CrossplayLogisticsIntent::Position(world,reinterpret_cast<uint32_t>(tasks[1]),1,0)==1);
  REQUIRE(CrossplayLogisticsIntent::Track(world,65536,65537,caller,0));REQUIRE(CrossplayLogisticsActive::ledger.marks[0].intent==neo_fifo::Intent::Defer);
  REQUIRE(CrossplayLogisticsIntent::Track(world,65537,65536,caller,0));REQUIRE(CrossplayLogisticsActive::ledger.marks[1].intent==neo_fifo::Intent::Prefer);
  REQUIRE(!CrossplayLogisticsIntent::Track(world,65537,65536,0x12345678,0));REQUIRE(!CrossplayLogisticsIntent::Track(world,65536,65536,caller,0));
  REQUIRE(!CrossplayLogisticsIntent::Track(world,65536^65536,0,caller,0));
  registry[0]^=65536;REQUIRE(!CrossplayLogisticsIntent::Track(world,65536,0,caller,0));registry[0]^=65536;
  tasks[0][0x34/4]=25;REQUIRE(!CrossplayLogisticsIntent::Track(world,65536,0,caller,0));tasks[0][0x34/4]=1;
  tasks[1][0x34/4]=2;REQUIRE(!CrossplayLogisticsIntent::Track(world,65536,65537,caller,0));tasks[1][0x34/4]=1;
  tasks[0][0x1c/4]=9;REQUIRE(!CrossplayLogisticsIntent::Track(world,65536,0,caller,0));tasks[0][0x1c/4]=0;
  REQUIRE(CrossplayLogisticsIntent::Track(world,65536,0,caller,0));REQUIRE(CrossplayLogisticsActive::ledger.marks[0].intent==neo_fifo::Intent::Defer);
  const auto saved_size=priority[(p.stride+p.size)/4];priority[(p.stride+p.size)/4]=8193;REQUIRE(!CrossplayLogisticsIntent::Track(world,65536,0,caller,0));priority[(p.stride+p.size)/4]=saved_size;
  const auto old_node=nodes[1][0];nodes[1][0]=reinterpret_cast<uintptr_t>(nodes[1]);REQUIRE(CrossplayLogisticsIntent::Position(world,1234,1,0)==-1);nodes[1][0]=old_node;
  neo_logistics::SavedState saved{};REQUIRE(CrossplayLogisticsActive::ledger.Snapshot(world,400,saved));REQUIRE(saved.marks.size()==2);saved.logic_turn=399;const auto payload=neo_logistics::Encode(saved);REQUIRE(!payload.empty());
  if(identical.empty())identical=payload;else REQUIRE(identical==payload);
  CrossplayLogisticsActive::ledger.Clear();REQUIRE(CrossplayLogisticsActive::ledger.Restore(world,400,payload));REQUIRE(CrossplayLogisticsActive::ledger.marks[1].intent==neo_fifo::Intent::Prefer);
 }
 // The active adapter restores the complete staged ledger only on its genuine
 // owned replay turn. Pending/wrong-owner clocks must not activate it.
 CrossplayLogisticsActive::ledger.Clear();CrossplayLogisticsActive::begun=false;
 CrossplayLogisticsActive::installed=true;CrossplayLogisticsActive::resume_armed=false;
 REQUIRE(!CrossplayLogisticsActive::ArmResume("NEO0\n",399,400,true));
 REQUIRE(!CrossplayLogisticsActive::ArmResume(identical,398,399,true));
 REQUIRE(CrossplayLogisticsActive::ArmResume(identical,399,400,true));
 REQUIRE(!CrossplayLogisticsActive::ArmResume(identical,399,400,true));
 authenticated=false;REQUIRE(!CrossplayLogisticsActive::Advance());
 REQUIRE(!CrossplayLogisticsActive::begun && !CrossplayLogisticsActive::resume_restored);
 authenticated=true;++CrossplayLogisticsActive::owner;REQUIRE(!CrossplayLogisticsActive::Advance());--CrossplayLogisticsActive::owner;
 REQUIRE(CrossplayLogisticsActive::Advance());REQUIRE(CrossplayLogisticsActive::begun && CrossplayLogisticsActive::resume_restored);
 REQUIRE(CrossplayLogisticsActive::ledger.marks[1].intent==neo_fifo::Intent::Prefer);
 REQUIRE(CrossplayLogisticsActive::resume_unrestored_comparisons==0);
 CrossplayLogisticsActive::ledger.Clear();CrossplayLogisticsActive::begun=false;
 CrossplayLogisticsActive::resume_armed=false;CrossplayLogisticsActive::resume_restored=false;
 REQUIRE(!CrossplayLogisticsActive::ArmResume(identical,399,400,false));
 REQUIRE(!CrossplayLogisticsActive::ArmResume("NEO0\n",399,401,false));
 REQUIRE(CrossplayLogisticsActive::ArmResume("NEO0\n",399,400,false));
 replay_clock=399;REQUIRE(!CrossplayLogisticsActive::Advance());
 REQUIRE(!CrossplayLogisticsActive::begun && !CrossplayLogisticsActive::resume_restored);
 replay_clock=400;REQUIRE(CrossplayLogisticsActive::Advance());
 REQUIRE(CrossplayLogisticsActive::begun && CrossplayLogisticsActive::resume_restored);
 neo_logistics::SavedState disabled{};
 REQUIRE(CrossplayLogisticsActive::ledger.Snapshot(CrossplayLogisticsActive::ledger.world,400,disabled));
 REQUIRE(!disabled.enabled && disabled.marks.empty());
 printf("PASS %u native-layout priority and saved replay cases, including enabled NEO2 and disabled NEO0. Synthetic only.\n",checks);return 0;
}
