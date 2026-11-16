#pragma once
#include "crossplay_load_progress_codec.h"
#include <cmath>
// Bounded original replay-clock reader and explicit x20 diagnostic request.
// The request requires the current owner and an independently authenticated
// native world; a readable clock or successful method guard grants no authority.
namespace CrossplayNativeClock {
using namespace CrossplayNativeCodec;
struct Profile {uintptr_t root_global=0,table=0,setter=0;unsigned setter_bytes=0;uint64_t table_hash=0,setter_hash=0;};
struct Snapshot {uintptr_t root=0;uint32_t turn=0;double current=0,pending=0;};
struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}};
inline uint64_t Hash(uintptr_t at,unsigned size){
 if(!Detail::Readable(reinterpret_cast<const void*>(at),size))return 0;
 uint64_t value=14695981039346656037ull;
 for(unsigned i=0;i<size;++i)value=(value^reinterpret_cast<const unsigned char*>(at)[i])*1099511628211ull;
 return value;
}
inline Profile Pin(Crossplay::Edition edition);
inline bool Guard(const Profile& p){
 return p.root_global && p.table && p.setter && p.setter_bytes>=3 && p.setter_bytes<=64
  && Hash(p.table,28)==p.table_hash && Hash(p.setter,p.setter_bytes)==p.setter_hash;
}
inline bool Read(const Profile& p,Snapshot& output){
 ErrorScope error;Snapshot row{};uint32_t root=0,table=0,setter=0;
 if(!Detail::Readable(reinterpret_cast<const void*>(p.root_global),4))return false;
 memcpy(&root,reinterpret_cast<const void*>(p.root_global),4);
 if(root<0x10000 || !Detail::Readable(reinterpret_cast<const void*>(root),0x38))return false;
 memcpy(&table,reinterpret_cast<const void*>(root),4);
 if(table!=p.table || !Detail::Readable(reinterpret_cast<const void*>(table),28))return false;
 memcpy(&setter,reinterpret_cast<const void*>(table+0x18),4);if(setter!=p.setter)return false;
 row.root=root;memcpy(&row.turn,reinterpret_cast<const void*>(root+0x2c),4);
 memcpy(&row.current,reinterpret_cast<const void*>(root+0x18),8);memcpy(&row.pending,reinterpret_cast<const void*>(root+0x20),8);
 if(row.turn==UINT32_MAX || !std::isfinite(row.current) || !std::isfinite(row.pending)
  || (row.current!=100 && row.current!=5) || (row.pending!=100 && row.pending!=5))return false;
 output=row;return true;
}
inline bool Request(const Profile& p,DWORD owner,bool authenticated_world,Snapshot& output){
 ErrorScope error;LoadProgressDetail::FpScope fp;Snapshot before{},after{};
 if(!owner || owner!=GetCurrentThreadId() || !authenticated_world || !Guard(p) || !Read(p,before))return false;
 if(before.pending!=5){
  using Setter=void(__thiscall*)(void*,double);
  reinterpret_cast<Setter>(p.setter)(reinterpret_cast<void*>(before.root),5.0);
 }
 if(!Read(p,after) || after.root!=before.root || after.turn!=before.turn || after.pending!=5)return false;
 output=after;return true;
}
}
#include "crossplay_native_clock_pins.h"
