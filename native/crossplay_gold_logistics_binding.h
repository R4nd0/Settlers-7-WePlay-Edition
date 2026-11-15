#pragma once
#include "crossplay_load_progress_codec.h"
// Experimental read-only Gold logistics binding. No hooks or native calls.
// Caller must bind the exact supported executable, its authenticated owned world
// and the simulation owner thread. CaptureWorld alone grants no session rights.
// See docs/GOLD-HISTORY-NEO-BINDING.md for actual qualification and limits.
namespace GoldLogisticsBinding {
using namespace CrossplayNativeCodec;
static_assert(sizeof(void*)==4,"Gold logistics binding requires x86");
#ifdef S7_GOLD_LOGISTICS_BINDING_TESTING
inline uintptr_t logic_global=0x10f8354;
#else
inline constexpr uintptr_t logic_global=0x10f8354;
#endif
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
 if(!Word(logic_global,row.logic) || !Word(row.logic,table) || table!=0xf317e0
  || !Word(row.logic+0x14c,row.main) || !Word(row.main,table) || table!=0xf4bb0c
  || !Word(row.main+4,row.priority) || !Word(row.priority,table) || table!=0xf4beb4
  || !Word(row.main+8,row.transfers) || !Word(row.transfers+0x1a8,wrapper) || wrapper!=row.transfers+0x194)return false;
 output=row;return true;
}
inline bool Task(const World& world,uint32_t id,uint32_t& output) {
 uint32_t task=0,identity=0;
 if(!Registry(world.priority+0x164c,world.priority+0x1650,id,task) || !Word(task+8,identity) || identity!=id)return false;
 output=task;return true;
}
inline bool CaptureRequest(const World& world,uint32_t reservation,Request& output) {
 ErrorScope error;Request row{};row.reservation=reservation;uint32_t identity=0,table=0,bound=0,player_word=0;
 if(!Registry(world.transfers+0x19c,world.transfers+0x1a0,reservation,row.record)
  || !Word(row.record,identity) || identity!=reservation || !Word(row.record+8,row.id)
  || !Task(world,row.id,row.task) || !Word(row.task,table) || (table!=0xf4c684 && table!=0xf4c74c)
  || (table==0xf4c684 && (!Word(row.task+0x70,bound) || bound!=reservation))
  || !Word(row.task+0x14,row.priority) || !Word(row.task+0x2c,row.birth)
  || !Word(row.task+0x34,player_word) || !Word(row.task+0x1c,row.group) || !Word(row.task+0x20,row.bucket)
  || !Word(row.task+0x24,row.parent_id) || !Word(row.task+0x38,row.parent_pointer))return false;
 row.ordinary_request=table==0xf4c684;row.player=player_word&255u;if(row.player>=25 || row.group>10 || row.bucket>10)return false;
 output=row;return true;
}
inline bool CaptureSort(uintptr_t comparator,uintptr_t left_id,uintptr_t right_id,World& world,Request& left,Request& right) {
 ErrorScope error;World candidate{};Request a{},b{};uint32_t wrapper=0,first_id=0,second_id=0;
 if(!CaptureWorld(candidate) || !Word(comparator,wrapper) || wrapper!=candidate.transfers+0x194
  || !Word(left_id,first_id) || !Word(right_id,second_id)
  || !CaptureRequest(candidate,first_id,a) || !CaptureRequest(candidate,second_id,b))return false;
 world=candidate;left=a;right=b;return true;
}
}

namespace GoldLogisticsBinding {
inline bool OriginalGuards() {
 ErrorScope error;
 struct Region {uintptr_t address;unsigned size;uint64_t expected;};
 static constexpr Region regions[]={
  {0x91fe00,204,0x9333d9ae3bba87c4ull},
  {0x947d80,453,0x9248d8db35cffa49ull},
  {0xf4c74c,8,0x9072d3ecc45e4809ull},
  {0x946890,175,0x4ba6b3ea3d1b32d4ull},
  {0x946990,137,0x3afc134793772ff0ull},
  {0x9516e0,68,0x44eddc6941ed31b6ull},
  {0xf317e0,8,0x3dee5dbb30aed89ull},
  {0xf4bb0c,8,0x841afd756dc6f69dull},
  {0xf4beb4,8,0x7427e3a6bffa8fc4ull},
  {0xf4c684,8,0x52fdfffce53cde18ull},
 };
 for(const auto& row:regions) {
  if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(row.address),row.size))return false;
  uint64_t value=14695981039346656037ull;
  for(unsigned i=0;i<row.size;++i)value=(value^reinterpret_cast<const unsigned char*>(row.address)[i])*1099511628211ull;
  if(value!=row.expected)return false;
 }
 return true;
}
}
