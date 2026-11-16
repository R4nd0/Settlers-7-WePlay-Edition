#define S7_GOLD_LOGISTICS_BINDING_TESTING
#include "crossplay_logistics_intent.h"
#include "crossplay_logistics_guards.h"
#include <cstdio>
namespace {
uint32_t logic[0x200/4]{},main_object[8]{},priority[0x1660/4]{},transfers[0x1b0/4]{};
uint32_t tasks[4][0x100/4]{},records[2][8]{},task_registry[8]{},record_registry[4]{};
uintptr_t logic_pointer=0;CrossplayLogistics::Ledger ledger{},restored{};unsigned checks=0;
void Set(uint32_t* object,unsigned offset,uintptr_t value){object[offset/4]=static_cast<uint32_t>(value);}
#define REQUIRE(value) do {++checks;if(!(value)){printf("FAIL line %u check %u\n",__LINE__,checks);return 1;}}while(0)
}
extern "C" __declspec(dllexport) uint32_t __cdecl CrossplayLogisticsBindingGuardOracle(uint32_t edition){return CrossplayLogisticsBinding::OriginalGuards(static_cast<Crossplay::Edition>(edition))?1u:0u;}
// Negative control for the offline verifier's optional error-state assertion.
extern "C" __declspec(dllexport) uint32_t __cdecl CrossplayLogisticsBindingLastErrorControl(uint32_t edition){MEMORY_BASIC_INFORMATION info{};VirtualQuery(nullptr,&info,sizeof(info));return edition==1?1u:0u;}
int Scenario(Crossplay::Edition edition) {
 using namespace CrossplayLogisticsBinding;ledger.Clear();restored.Clear();memset(tasks,0,sizeof(tasks));memset(records,0,sizeof(records));
 pin=Pin(edition);logic_pointer=reinterpret_cast<uintptr_t>(logic);pin.global=reinterpret_cast<uintptr_t>(&logic_pointer);
 logic[0]=pin.logic;Set(logic,0x14c,reinterpret_cast<uintptr_t>(main_object));main_object[0]=pin.main;
 Set(main_object,4,reinterpret_cast<uintptr_t>(priority));Set(main_object,8,reinterpret_cast<uintptr_t>(transfers));priority[0]=pin.priority;
 Set(priority,pin.task_registry,reinterpret_cast<uintptr_t>(task_registry));Set(priority,pin.task_registry+4,reinterpret_cast<uintptr_t>(task_registry+8));
 Set(transfers,pin.record_registry,reinterpret_cast<uintptr_t>(record_registry));Set(transfers,pin.record_registry+4,reinterpret_cast<uintptr_t>(record_registry+4));Set(transfers,0x1a8,reinterpret_cast<uintptr_t>(transfers)+0x194);
 for(unsigned i=0;i<4;++i) {
  tasks[i][0]=i<2?pin.request:pin.production;Set(tasks[i],8,65536+i);Set(tasks[i],0x14,100-i);Set(tasks[i],0x2c,200+i);
  Set(tasks[i],0x34,1);Set(tasks[i],0x1c,i<2?9:i);Set(tasks[i],0x20,i);task_registry[i*2]=65536+i;task_registry[i*2+1]=reinterpret_cast<uintptr_t>(tasks[i]);
  if(i<2){Set(tasks[i],0x70,196608+i);Set(tasks[i],0x4c,23+i);records[i][1]=23+i;records[i][0]=196608+i;records[i][2]=65536+i;record_registry[i*2]=196608+i;record_registry[i*2+1]=reinterpret_cast<uintptr_t>(records[i]);}
 }
 World world{};Request left{},right{};REQUIRE(CaptureWorld(world));REQUIRE(CaptureRequest(world,196608,left));REQUIRE(CaptureRequest(world,196609,right));
 auto capture=[&](){return CaptureRequest(world,196608,left)&&CaptureRequest(world,196609,right);};
 REQUIRE(!ledger.Begin(world,2,true));REQUIRE(ledger.Begin(world,0,false));REQUIRE(ledger.Compare(world,left,right)==2);
 REQUIRE(ledger.Begin(world,1,true));REQUIRE(ledger.Advance(world,400));REQUIRE(!ledger.Advance(world,399));
 SetLastError(0xabcdef12);REQUIRE(ledger.Compare(world,left,right)==1 && GetLastError()==0xabcdef12);REQUIRE(ledger.Compare(world,right,left)==0);
 // Same public History comparator: ordinary FIFO ignores native ranks/groups.
 neo_fifo::Key a{},b{};REQUIRE(ledger.Key(left,a)&&ledger.Key(right,b));REQUIRE(a.group_order==0&&!a.grouped&&neo_fifo::Before(a,b));
 tasks[1][0x2c/4]=200;REQUIRE(capture());REQUIRE(ledger.Compare(world,left,right)==0&&ledger.Compare(world,right,left)==0);tasks[1][0x2c/4]=201;REQUIRE(capture());
 REQUIRE(ledger.Mark(right.id,neo_fifo::Intent::Prefer));REQUIRE(ledger.Compare(world,left,right)==0&&ledger.Compare(world,right,left)==1);
 REQUIRE(ledger.Mark(right.id,neo_fifo::Intent::Defer));REQUIRE(ledger.Compare(world,left,right)==1);
 REQUIRE(!ledger.Mark(right.id,neo_fifo::Intent::Ordinary));REQUIRE(!ledger.Mark(right.id^65536,neo_fifo::Intent::Prefer));
 // Inherit explicit parent intent, while a direct child intent takes precedence.
 Set(tasks[0],0x24,65538);Set(tasks[0],0x38,reinterpret_cast<uintptr_t>(tasks[2]));REQUIRE(ledger.Mark(65538,neo_fifo::Intent::Prefer));REQUIRE(capture());
 REQUIRE(ledger.Key(left,a)&&a.intent==neo_fifo::Intent::Prefer);REQUIRE(ledger.Mark(left.id,neo_fifo::Intent::Defer));REQUIRE(ledger.Key(left,a)&&a.intent==neo_fifo::Intent::Defer);
 Set(tasks[2],0x38,4);REQUIRE(ledger.Key(left,a)&&a.intent==neo_fifo::Intent::Defer);Set(tasks[2],0x38,0);
 // Reject stale parent identity, wrong ownership, reciprocal links and cycles.
 task_registry[4]^=65536;REQUIRE(ledger.Compare(world,left,right)==2);task_registry[4]^=65536;
 tasks[2][0x34/4]=2;REQUIRE(ledger.Compare(world,left,right)==2);tasks[2][0x34/4]=1;
 tasks[0][0x38/4]+=4;REQUIRE(ledger.Compare(world,left,right)==2);tasks[0][0x38/4]-=4;
 Set(tasks[2],0x24,65536);Set(tasks[2],0x38,reinterpret_cast<uintptr_t>(tasks[0]));REQUIRE(ledger.Compare(world,left,right)==2);Set(tasks[2],0x24,0);Set(tasks[2],0x38,0);
 // Round-trip the SAME NEO2 codec as History and bind original task identities.
 neo_logistics::SavedState saved{};REQUIRE(ledger.Snapshot(world,400,saved));saved.logic_turn=399;const auto payload=neo_logistics::Encode(saved);REQUIRE(!payload.empty());
 REQUIRE(restored.Restore(world,400,payload));REQUIRE(restored.Compare(world,left,right)==ledger.Compare(world,left,right));
 neo_logistics::SavedState after{};REQUIRE(restored.Snapshot(world,400,after));after.logic_turn=399;REQUIRE(neo_logistics::Encode(after)==payload);
 const auto old_mark=restored.marks[0];REQUIRE(!restored.Restore(world,401,payload));REQUIRE(restored.marks[0].Matches(old_mark.id,old_mark.created,old_mark.object));
 tasks[2][0x2c/4]++;REQUIRE(!restored.Restore(world,400,payload));tasks[2][0x2c/4]--;REQUIRE(restored.enabled&&restored.turn==400&&restored.marks[0].intent==old_mark.intent);
 REQUIRE(!restored.Restore(world,400,payload+"extra"));REQUIRE(!restored.Restore(world,400,"NEO2\n400\n399\n2\n65536,200,1\n65536,200,-1\n"));
 // Reused slot cannot inherit its old mark; a changed native world is excluded.
 tasks[0][0x2c/4]++;REQUIRE(capture());REQUIRE(restored.Key(left,a)&&a.intent==neo_fifo::Intent::Prefer);tasks[0][0x2c/4]--;REQUIRE(capture());
 const auto original_logic=logic[0];logic[0]^=4;REQUIRE(restored.Compare(world,left,right)==2);REQUIRE(!restored.Restore(world,400,payload));logic[0]=original_logic;
 // Snapshot prunes a stale mark; all helpers leave native task data untouched.
 unsigned char before[sizeof(tasks)]{};memcpy(before,tasks,sizeof(tasks));REQUIRE(ledger.Snapshot(world,400,after));REQUIRE(!memcmp(before,tasks,sizeof(tasks)));
 REQUIRE(restored.Restore(world,400,"NEO0\n"));REQUIRE(!restored.enabled&&restored.Compare(world,left,right)==2);
 // Actual Gold request group9 and reservation+70, not the goods field+4c.
 REQUIRE(capture()&&left.ordinary_request&&left.group==9);tasks[0][0x70/4]^=65536;REQUIRE(CaptureRequest(world,196608,left)==(pin.inverse==0));tasks[0][0x70/4]^=65536;REQUIRE(capture());
 tasks[0][0x1c/4]=11;REQUIRE(!CaptureRequest(world,196608,left));tasks[0][0x1c/4]=9;
 tasks[0][0x20/4]=11;REQUIRE(!CaptureRequest(world,196608,left));tasks[0][0x20/4]=0;REQUIRE(capture());
 // A registered production task can legitimately own multiple transfer records.
 // As in History it retains the native-Prefer key instead of ordinary FIFO.
 tasks[0][0]=pin.production;tasks[0][0x1c/4]=1;tasks[0][0x4c/4]=15;REQUIRE(capture()&&!left.ordinary_request);
 REQUIRE(ledger.Key(left,a)&&a.intent==neo_fifo::Intent::Prefer);REQUIRE(ledger.Compare(world,left,right)==1);
 records[1][2]=65536;REQUIRE(capture()&&left.task==right.task&&left.reservation!=right.reservation);
 REQUIRE(ledger.Compare(world,left,right)==0&&ledger.Compare(world,right,left)==0);records[1][2]=65537;
 tasks[0][0]=pin.request;tasks[0][0x1c/4]=9;tasks[0][0x4c/4]=23;REQUIRE(capture());
 // An ungrouped/unset native request preserves History's native-Prefer fallback.
 Set(tasks[0],0x24,0);Set(tasks[0],0x38,0);Set(tasks[0],0x20,9);REQUIRE(capture());REQUIRE(ledger.Key(left,a)&&a.intent==neo_fifo::Intent::Prefer);
 Set(tasks[0],0x20,10);REQUIRE(capture());REQUIRE(ledger.Key(left,a)&&a.intent==neo_fifo::Intent::Prefer);
 SetLastError(0xdecafbad);REQUIRE(!CrossplayLogisticsBinding::OriginalGuards(Crossplay::Edition::Gold)&&GetLastError()==0xdecafbad);
 printf("PASS %u cumulative cross-edition NEO policy/binding checks: shared FIFO and NEO2 codec, explicit parent intent, stable ties, generational/ownership/cycle rejection, atomic reload binding. Synthetic objects; no native hooks/game or NEO gameplay qualification.\n",checks);return 0;
}

int main(){for(unsigned i=1;i<=3;++i){const auto result=Scenario(static_cast<Crossplay::Edition>(i));if(result)return result;printf("Validated typed profile %u\n",i);}return 0;}

extern "C" __declspec(dllexport) uint32_t __cdecl CrossplayLogisticsIntentGuardOracle(uint32_t edition){return CrossplayLogisticsIntent::OriginalGuards(static_cast<Crossplay::Edition>(edition))?1u:0u;}
extern "C" __declspec(dllexport) uint32_t __cdecl CrossplayNativeClockGuardOracle(uint32_t edition){CrossplayLogisticsBinding::ErrorScope error;return CrossplayNativeClock::Guard(CrossplayNativeClock::Pin(static_cast<Crossplay::Edition>(edition)))?1u:0u;}
extern "C" __declspec(dllexport) uint32_t __cdecl CrossplayGoldSortGuardOracle(uint32_t edition){return edition==1 && GoldLogisticsSort::Guard()?1u:0u;}
extern "C" __declspec(dllexport) uint32_t __cdecl CrossplayGoldInlineGuardOracle(uint32_t edition){return edition==1 && GoldLogisticsInline::Guard()?1u:0u;}
