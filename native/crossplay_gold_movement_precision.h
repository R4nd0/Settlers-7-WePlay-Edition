#pragma once
#include "crossplay_load_progress_codec.h"

// Experimental Gold gameplay arithmetic port. Caller must bind the exact supported
// executable, admitted fresh-world owner and its already-guarded movement wrapper.
// Ordinary adapters do not include or activate this module. See the accompanying
// qualification document; no NEO/resume/Internet or arbitrary-match claim is made.
namespace GoldMovementPrecision {
using namespace CrossplayNativeCodec;
static_assert(sizeof(void*)==4,"Gold native movement precision requires x86");
inline DWORD owner=0;inline bool (*ready)()=nullptr;inline bool installed=false;
inline uintptr_t original_path=0,crt=0xdc5418;
inline unsigned movement_depth=0,path_depth=0,movement_applied=0,path_applied=0,excluded=0;
#ifdef S7_GOLD_MOVEMENT_PRECISION_TESTING
inline uintptr_t logic_global=0x10f8354;
#else
inline constexpr uintptr_t logic_global=0x10f8354;
#endif
inline bool Guard();
struct Binding {uint32_t object=0,id=0,registry=0,begin=0;};inline Binding cache[512]{};inline unsigned next_cache=0;
inline bool Word(uintptr_t at,uint32_t& value) {
 if(at<0x10000 || at>UINT32_MAX-3 || !Detail::Readable(reinterpret_cast<const void*>(at),4))return false;
 memcpy(&value,reinterpret_cast<const void*>(at),4);return true;
}
inline uint64_t Hash(uintptr_t at,unsigned bytes) {
 if(!Detail::Readable(reinterpret_cast<const void*>(at),bytes))return 0;
 uint64_t value=14695981039346656037ull;
 for(unsigned i=0;i<bytes;++i)value=(value^reinterpret_cast<const unsigned char*>(at)[i])*1099511628211ull;
 return value;
}
inline bool Actor(uintptr_t component,bool selected,uint32_t& identity) {
 if(component<0x10000+0xd8)return false;
 const uintptr_t object=component-0xd8;uint32_t table=0,logic=0,registry=0,begin=0,end=0,chosen=0;
 if(!Word(component,table) || table!=0xf3ed90 || !Word(object,table))return false;
 unsigned offset=0;uint32_t manager=0;
 if(table==0xf3f084){offset=0xec;manager=0xf3dbd0;}
 else if(table==0xf3b64c){offset=0xf4;manager=0xf3b4b0;}
 else return false;
 // Path production can precede selecting the already-constructed component.
 if(selected && (!Word(object+0x274,chosen) || chosen!=component))return false;
 if(!Word(logic_global,logic) || !Word(logic,table) || table!=0xf317e0 || !Word(logic+offset,registry)
  || !Word(registry,table) || table!=manager || !Word(registry+0xc,begin) || !Word(registry+0x10,end)
  || begin<0x10000 || end<begin || (end-begin)%8 || (end-begin)/8>65536)return false;
 const unsigned count=(end-begin)/8;
 for(auto& row:cache)if(row.object==object && row.registry==registry && row.begin==begin) {
  uint32_t id=0,member=0;const unsigned index=row.id&65535u;
  if(index<count && Word(begin+index*8,id) && id==row.id && Word(begin+index*8+4,member) && member==object){identity=id;return true;}
  row={};
 }
 unsigned matches=0;Binding found{};
 for(unsigned i=0;i<count;++i) {
  uint32_t id=0,member=0;if(!Word(begin+i*8,id) || !Word(begin+i*8+4,member))return false;
  if(member==object){if((id&65535u)!=i || id==UINT32_MAX || ++matches!=1)return false;found={static_cast<uint32_t>(object),id,registry,begin};}
 }
 if(matches!=1)return false;cache[next_cache++%512]=found;identity=found.id;return true;
}
inline bool Environment(unsigned short previous,unsigned depth){return (previous&0xc00)==0 && ((previous&0x300)==0x200 || ((previous&0x300)==0 && depth>0)) && depth<32;}
struct MovementScope {unsigned short previous=0;bool active=false;};
inline void BeginMovement(void* self,MovementScope& scope) {
 const DWORD error=GetLastError();
 if(installed && owner==GetCurrentThreadId() && ready && ready()) {
  unsigned short cw=0;__asm fnstcw cw
  uint32_t identity=0;
  if(Environment(cw,movement_depth+path_depth) && Actor(reinterpret_cast<uintptr_t>(self),true,identity)) {
   scope.previous=cw;scope.active=true;const unsigned short single=static_cast<unsigned short>(cw&~0x300u);++movement_depth;++movement_applied;__asm fldcw single
  } else ++excluded;
 }
 SetLastError(error);
}
inline void EndMovement(const MovementScope& scope){if(scope.active){const unsigned short previous=scope.previous;__asm fldcw previous
 --movement_depth;}}
inline bool PathEligible(const uint32_t* frame,unsigned short previous) {
 if(!installed || owner!=GetCurrentThreadId() || !ready || !ready() || !Environment(previous,path_depth+movement_depth) || !frame)return false;
 ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);const uintptr_t at=reinterpret_cast<uintptr_t>(frame);
 if(at<low || at>high-56)return false;
 const uintptr_t grid=frame[1],vector=frame[6],seeds=frame[10],filter=frame[11];uint32_t identity=0,width=0,height=0,nodes=0,mask=0,begin=0,end=0;
 if(vector<0x10000+0x1c || !Actor(vector-0x1c,false,identity) || !Detail::Readable(reinterpret_cast<const void*>(grid),0x8c)
  || !Word(grid+0x1c,width) || !Word(grid+0x20,height) || width<3 || height<3 || width>4096 || height>4096 || static_cast<uint64_t>(width)*height>4194304
  || frame[12]<1 || frame[13]<1 || frame[12]>=width-1 || frame[13]>=height-1
  || !Word(grid+0x68,nodes) || !Word(grid+0x48,mask) || !Detail::Readable(reinterpret_cast<const void*>(nodes),width*height*20u)
  || !Detail::Readable(reinterpret_cast<const void*>(mask),width*height) || !Detail::Readable(reinterpret_cast<const void*>(filter),4)
  || !Word(seeds+4,begin) || !Word(seeds+8,end) || end<begin || (end-begin)%8 || (end-begin)/8>127
  || (end>begin && !Detail::Readable(reinterpret_cast<const void*>(begin),end-begin)))return false;
 return true;
}
inline void __cdecl InvokePath(uint32_t* frame,unsigned char* fp) {
 const DWORD incoming_error=GetLastError();unsigned short previous=0;memcpy(&previous,fp,2);
 const bool eligible=PathEligible(frame,previous);const unsigned short single=static_cast<unsigned short>(previous&~0x300u);
 if(eligible){++path_depth;++path_applied;}
 uint32_t returned_eax=0,returned_ecx=0,returned_edx=0,returned_flags=0;DWORD native_error=incoming_error;
 SetLastError(incoming_error);
 __try {
  __asm {mov eax,fp
   fxrstor [eax]
  }
  if(eligible){__asm fldcw single}
  __asm {
   mov edx,frame
   push dword ptr [edx+52]
   push dword ptr [edx+48]
   push dword ptr [edx+44]
   push dword ptr [edx+40]
   push dword ptr [edx+32]
   popfd
   mov esi,[edx+4]
   mov edi,[edx]
   mov ebx,[edx+16]
   mov ecx,[edx+24]
   mov eax,[edx+28]
   mov edx,[edx+20]
   call dword ptr [original_path]
   mov returned_eax,eax
   mov returned_ecx,ecx
   mov returned_edx,edx
   pushfd
   pop returned_flags
  }
  native_error=GetLastError();
 } __finally {
  if(eligible){__asm fldcw previous
   --path_depth;
  }
 }
 frame[7]=returned_eax;frame[6]=returned_ecx;frame[5]=returned_edx;frame[8]=returned_flags;SetLastError(native_error);
}
__declspec(naked) inline void Path() {
 __asm {
  pushfd
  pushad
  mov ebp,esp
  sub esp,528
  and esp,0xfffffff0
  fxsave [esp]
  push esp
  push ebp
  call InvokePath
  add esp,8
  mov esp,ebp
  popad
  popfd
  ret 10h
 }
}
__declspec(naked) inline void Atan2() {
 __asm {
  pushfd
  push eax
  mov eax,fs:[24h]
  cmp eax,owner
  jne Forward
  cmp movement_depth,0
  je Forward
  pop eax
  popfd
  lea esp,[esp-4]
  pushfd
  push eax
  fnstcw word ptr [esp+8]
  mov ax,word ptr [esp+8]
  and ax,0fcffh
  or ax,200h
  mov word ptr [esp+10],ax
  fldcw word ptr [esp+10]
  pop eax
  popfd
  call dword ptr [crt]
  fldcw word ptr [esp]
  lea esp,[esp+4]
  ret
 Forward:
  pop eax
  popfd
  jmp dword ptr [crt]
 }
}
inline bool Install(DWORD thread,bool (*predicate)(),uintptr_t owned_movement_entry,bool (*additional_guards)()) {
 const DWORD error=GetLastError();LoadProgressDetail::FpScope fp;
 if(installed || owner || original_path || crt!=0xdc5418 || logic_global!=0x10f8354 || thread!=GetCurrentThreadId() || !predicate || !owned_movement_entry || !additional_guards || !additional_guards() || !Guard())return false;
 int32_t movement_delta=0;memcpy(&movement_delta,reinterpret_cast<const void*>(0x70c761),4);
 if(*reinterpret_cast<const unsigned char*>(0x70c760)!=0xe9 || static_cast<uintptr_t>(0x70c765+movement_delta)!=owned_movement_entry || *reinterpret_cast<const unsigned char*>(0x70c765)!=0x90)return false;
 constexpr uintptr_t target=0x707e00;const unsigned char prefix[]={0x55,0x8b,0xec,0x83,0xec,0x30};if(memcmp(reinterpret_cast<const void*>(target),prefix,6))return false;
 for(uintptr_t at:{uintptr_t(0x70c69b),uintptr_t(0x70ca7b)}){int32_t delta=0;memcpy(&delta,reinterpret_cast<const void*>(at+1),4);if(*reinterpret_cast<const unsigned char*>(at)!=0xe8 || at+5+delta!=crt)return false;}
 auto* replay=static_cast<unsigned char*>(VirtualAlloc(nullptr,32,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!replay)return false;
 memcpy(replay,prefix,6);replay[6]=0xe9;int32_t delta=static_cast<int32_t>(target+6-(reinterpret_cast<uintptr_t>(replay)+11));memcpy(replay+7,&delta,4);
 DWORD old=0;if(!VirtualProtect(replay,32,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),replay,11)){VirtualFree(replay,0,MEM_RELEASE);return false;}
 struct Patch {uintptr_t at,target;unsigned bytes;unsigned char before[6]{};DWORD protection=0;};
 Patch patches[]={{target,reinterpret_cast<uintptr_t>(&Path),6},{0x70c69b,reinterpret_cast<uintptr_t>(&Atan2),5},{0x70ca7b,reinterpret_cast<uintptr_t>(&Atan2),5}};
 original_path=reinterpret_cast<uintptr_t>(replay);unsigned changed=0;bool okay=true;
 for(auto& patch:patches) {
  memcpy(patch.before,reinterpret_cast<const void*>(patch.at),patch.bytes);if(!VirtualProtect(reinterpret_cast<void*>(patch.at),patch.bytes,PAGE_EXECUTE_READWRITE,&patch.protection)){okay=false;break;}
  unsigned char bytes[6]={patch.at==target?static_cast<unsigned char>(0xe9):static_cast<unsigned char>(0xe8),0,0,0,0,0x90};delta=static_cast<int32_t>(patch.target-(patch.at+5));memcpy(bytes+1,&delta,4);memcpy(reinterpret_cast<void*>(patch.at),bytes,patch.bytes);++changed;
  const bool flush=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<const void*>(patch.at),patch.bytes)!=0;DWORD ignored=0;const bool protect=VirtualProtect(reinterpret_cast<void*>(patch.at),patch.bytes,patch.protection,&ignored)!=0;if(!flush || !protect){okay=false;break;}
 }
 if(!okay || !additional_guards()) {
  while(changed){auto& patch=patches[--changed];DWORD temporary=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(patch.at),patch.bytes,PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);memcpy(reinterpret_cast<void*>(patch.at),patch.before,patch.bytes);const bool flush=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(patch.at),patch.bytes)!=0;const bool protect=VirtualProtect(reinterpret_cast<void*>(patch.at),patch.bytes,patch.protection,&ignored)!=0;if(!flush || !protect)__fastfail(7);}
  original_path=0;VirtualFree(replay,0,MEM_RELEASE);SetLastError(error);return false;
 }
 owner=thread;ready=predicate;installed=true;SetLastError(error);return true;
}
}

#include "crossplay_gold_movement_precision_pins.h"
