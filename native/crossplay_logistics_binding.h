#pragma once
#include "crossplay_load_progress_codec.h"
// Read-only cross-edition logistics binding. No hooks or native calls.
// Caller must bind the exact supported executable, its authenticated owned world
// and the simulation owner thread. CaptureWorld alone grants no session rights.
// See docs/GOLD-HISTORY-NEO-BINDING.md for actual qualification and limits.
namespace CrossplayLogisticsBinding {
using namespace CrossplayNativeCodec;
static_assert(sizeof(void*)==4,"Cross-edition logistics binding requires x86");
struct Profile {uintptr_t global=0;uint32_t logic=0,main=0,priority=0,request=0,production=0,task_registry=0,record_registry=0,inverse=0,wrapper=0,wrapper_value=0;uintptr_t compare=0;};
inline Profile pin{};
struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}};
struct World {uint32_t logic=0,main=0,priority=0,transfers=0;};
struct Request {uint32_t record=0,reservation=0,task=0,id=0,birth=0,priority=0,player=0,group=0,bucket=0,parent_id=0,parent_pointer=0;bool ordinary_request=false;};
inline bool Word(uintptr_t at,uint32_t& value) {
 if(at<0x10000 || at>UINT32_MAX-3 || !Detail::Readable(reinterpret_cast<const void*>(at),4))return false;
 memcpy(&value,reinterpret_cast<const void*>(at),4);return true;
}
inline bool Registry(uintptr_t begin_at,uintptr_t end_at,uint32_t id,uint32_t& object) {
 uint32_t begin=0,end=0,identity=0;object=0;
 if(!Word(begin_at,begin) || !Word(end_at,end) || begin<0x10000 || begin>end || (end-begin)%8 || (end-begin)/8>65536 || (id&65535u)>=(end-begin)/8)return false;
 return Word(begin+(id&65535u)*8,identity) && identity==id && Word(begin+(id&65535u)*8+4,object) && object;
}
inline bool CaptureWorld(World& output) {
 ErrorScope error;World row{};uint32_t table=0,wrapper=0;
 if(!Word(pin.global,row.logic) || !Word(row.logic,table) || table!=pin.logic
  || !Word(row.logic+0x14c,row.main) || !Word(row.main,table) || table!=pin.main
  || !Word(row.main+4,row.priority) || !Word(row.priority,table) || table!=pin.priority
  || !Word(row.main+8,row.transfers) || (pin.wrapper && (!Word(row.transfers+pin.wrapper,wrapper) || wrapper!=row.transfers+pin.wrapper_value)))return false;
 output=row;return true;
}
inline bool Task(const World& world,uint32_t id,uint32_t& output) {
 uint32_t task=0,identity=0;
 if(!Registry(world.priority+pin.task_registry,world.priority+pin.task_registry+4,id,task) || !Word(task+8,identity) || identity!=id)return false;
 output=task;return true;
}
inline bool CaptureRequest(const World& world,uint32_t reservation,Request& output) {
 ErrorScope error;Request row{};row.reservation=reservation;uint32_t identity=0,table=0,bound=0,player_word=0;
 if(!Registry(world.transfers+pin.record_registry,world.transfers+pin.record_registry+4,reservation,row.record)
  || !Word(row.record,identity) || identity!=reservation || !Word(row.record+8,row.id)
  || !Task(world,row.id,row.task) || !Word(row.task,table) || (table!=pin.request && table!=pin.production)
  || (table==pin.request && pin.inverse && (!Word(row.task+pin.inverse,bound) || bound!=reservation))
  || !Word(row.task+0x14,row.priority) || !Word(row.task+0x2c,row.birth)
  || !Word(row.task+0x34,player_word) || !Word(row.task+0x1c,row.group) || !Word(row.task+0x20,row.bucket)
  || !Word(row.task+0x24,row.parent_id) || !Word(row.task+0x38,row.parent_pointer))return false;
 row.ordinary_request=table==pin.request;row.player=player_word&255u;if(row.player>=25 || row.group>10 || row.bucket>10)return false;
 output=row;return true;
}
inline bool CaptureRecord(const World& world,uint32_t record,Request& out) {
 uint32_t id=0;return Word(record,id) && CaptureRequest(world,id,out) && out.record==record;
}
}
namespace CrossplayLogisticsBinding {
inline Profile Pin(Crossplay::Edition edition){
 if(edition==Crossplay::Edition::Gold)return {0x10f8354,0xf317e0,0xf4bb0c,0xf4beb4,0xf4c684,0xf4c74c,0x164c,0x19c,0x70,0x1a8,0x194,0x0};
 if(edition==Crossplay::Edition::UbisoftHistory)return {0x13a5220,0x1141534,0x1147f40,0x1148328,0x1148a68,0x1148b6c,0x1134,0x148,0x0,0x0,0x0,0x78f380};
 if(edition==Crossplay::Edition::SteamHistory)return {0x13a2314,0x113fa54,0x11464d0,0x1146850,0x1146f90,0x1147094,0x1134,0x148,0x0,0x0,0x0,0x78f150};
 return {};
}
}

#include "crossplay_logistics_guards.h"
