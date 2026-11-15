#define S7_GOLD_MOVEMENT_PRECISION_TESTING
#include "crossplay_gold_movement_precision.h"
#include <cstdio>
namespace {
uint32_t units[1][0x400/4]{},logic[0x200/4]{},registry[8]{},members[256]{};
uint32_t logic_pointer=0;
void Put(uint32_t* p,unsigned offset,uintptr_t value){p[offset/4]=static_cast<uint32_t>(value);}
}
namespace {
uint32_t precision_calls=0,precision_vector=0,precision_grid=0,precision_args[4]{},precision_cw=0,precision_throw=0,crt_cw=0;
uint32_t precision_grid_words[40]{},precision_nodes[16*16*5]{},precision_seed_vector[4]{},precision_seeds[2]={12,12},precision_filter=0x01000000;
unsigned char precision_mask[256]{};bool precision_allowed=true;
bool PrecisionReady(){return precision_allowed;}
void __stdcall PrecisionRaise(){RaiseException(0xe1234578,0,0,nullptr);}
__declspec(naked) void PrecisionNativePath(){__asm {
 inc precision_calls
 mov precision_vector,ecx
 mov precision_grid,esi
 mov eax,[esp+4]
 mov precision_args,eax
 mov eax,[esp+8]
 mov precision_args+4,eax
 mov eax,[esp+12]
 mov precision_args+8,eax
 mov eax,[esp+16]
 mov precision_args+12,eax
 fnstcw word ptr precision_cw
 cmp precision_throw,0
 jne Throw
 push 7654h
 call SetLastError
 mov eax,1234abcdh
 mov ecx,2345bcdeh
 mov edx,3456cdefh
 push 246h
 popfd
 ret 10h
 Throw:
 call PrecisionRaise
 ret 10h
}}
__declspec(naked) void PrecisionCRT(){__asm {
 fnstcw word ptr crt_cw
 faddp st(1),st(0)
 ret
}}
struct PrecisionResult {uint32_t eax=0,ecx=0,edx=0,flags=0,delta=0;};
void CallPrecision(PrecisionResult& result) {
 const uintptr_t vector=reinterpret_cast<uintptr_t>(units[0])+0xd8+0x1c,grid=reinterpret_cast<uintptr_t>(precision_grid_words),seeds=reinterpret_cast<uintptr_t>(precision_seed_vector),filter=reinterpret_cast<uintptr_t>(&precision_filter);
 uint32_t a=0,b=0,c=0,flags=0,delta=0;
 __asm {
  mov esi,grid
  mov ecx,vector
  mov edi,esp
  push 2
  push 2
  push filter
  push seeds
  call GoldMovementPrecision::Path
  mov a,eax
  mov b,ecx
  mov c,edx
  pushfd
  pop flags
  mov eax,esp
  sub eax,edi
  mov delta,eax
 }
 result={a,b,c,flags,delta};
}
int TestPrecisionException(){
 precision_throw=1;bool caught=false;
 __try {PrecisionResult result{};CallPrecision(result);}
 __except(GetExceptionCode()==0xe1234578?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=true;}
 precision_throw=0;unsigned short cw=0;__asm fnstcw cw
 return caught && cw==0x27f && GoldMovementPrecision::path_depth==0?0:48;
}
int TestPrecision() {
 using namespace GoldMovementPrecision;
 const auto original_owner=owner;const auto original_ready=ready;const auto original_installed=installed;const auto original_logic=logic_global;
 owner=GetCurrentThreadId();ready=&PrecisionReady;installed=true;logic_global=reinterpret_cast<uintptr_t>(&logic_pointer);original_path=reinterpret_cast<uintptr_t>(&PrecisionNativePath);
 Put(precision_grid_words,0x1c,16);Put(precision_grid_words,0x20,16);Put(precision_grid_words,0x48,reinterpret_cast<uintptr_t>(precision_mask));Put(precision_grid_words,0x68,reinterpret_cast<uintptr_t>(precision_nodes));
 Put(precision_seed_vector,4,reinterpret_cast<uintptr_t>(precision_seeds));Put(precision_seed_vector,8,reinterpret_cast<uintptr_t>(precision_seeds)+8);Put(precision_seed_vector,12,reinterpret_cast<uintptr_t>(precision_seeds)+8);
 unsigned short previous=0;__asm fnstcw previous
 unsigned short normal=0x27f;__asm fldcw normal
 PrecisionResult value{};CallPrecision(value);unsigned short after=0;__asm fnstcw after
 if(precision_calls!=1 || precision_cw!=0x7f || path_applied!=1 || path_depth || after!=normal || value.eax!=0x1234abcd || value.ecx!=0x2345bcde || value.edx!=0x3456cdef || value.flags!=0x246 || value.delta || GetLastError()!=0x7654 || precision_vector!=reinterpret_cast<uintptr_t>(units[0])+0xf4 || precision_grid!=reinterpret_cast<uintptr_t>(precision_grid_words) || precision_args[0]!=reinterpret_cast<uintptr_t>(precision_seed_vector) || precision_args[1]!=reinterpret_cast<uintptr_t>(&precision_filter) || precision_args[2]!=2 || precision_args[3]!=2)return 40;
 // The output component exists before it becomes selected: still a valid path
 // construction context, but not yet an eligible movement-execution context.
 Put(units[0],0x274,reinterpret_cast<uintptr_t>(units[0])+0x88);CallPrecision(value);if(path_applied!=2 || precision_cw!=0x7f)return 41;
 MovementScope rejected{};BeginMovement(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(units[0])+0xd8),rejected);if(rejected.active)return 42;
 Put(units[0],0x274,reinterpret_cast<uintptr_t>(units[0])+0xd8);
 MovementScope outer{},inner{};BeginMovement(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(units[0])+0xd8),outer);BeginMovement(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(units[0])+0xd8),inner);
 __asm fnstcw after
 if(!outer.active || !inner.active || after!=0x7f || movement_depth!=2)return 43;EndMovement(inner);EndMovement(outer);__asm fnstcw after
 if(after!=normal || movement_depth)return 44;
 const auto applied=path_applied;members[(65551u&65535u)*2]^=1;CallPrecision(value);members[(65551u&65535u)*2]^=1;
 if(path_applied!=applied || precision_cw!=normal || precision_calls!=3)return 45;
 precision_allowed=false;CallPrecision(value);precision_allowed=true;if(path_applied!=applied || precision_cw!=normal)return 46;
 Put(precision_grid_words,0x1c,5000);CallPrecision(value);Put(precision_grid_words,0x1c,16);if(path_applied!=applied || precision_cw!=normal)return 47;
 if(const int exception=TestPrecisionException())return exception;
 // Retain original double precision in the unchanged CRT atan2 call while the
 // owning movement scope uses single intermediates, then restore the scope.
 crt=reinterpret_cast<uintptr_t>(&PrecisionCRT);MovementScope movement{};BeginMovement(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(units[0])+0xd8),movement);double sum=0;
 __asm {fld1
 fld1
 call GoldMovementPrecision::Atan2
 fstp qword ptr sum
 fnstcw after
 }
 if(sum!=2.0 || crt_cw!=normal || after!=0x7f)return 49;EndMovement(movement);crt=0xdc5418;
 __asm fldcw previous
 installed=original_installed;owner=original_owner;ready=original_ready;logic_global=original_logic;original_path=0;
 puts("PASS combined path/movement scoped precision: custom ESI+ECX/four native arguments once, raw GP/flags/stack/LastError, typed construction vs execution, nested scope restoration, genuine SEH unwind and CRT53 precision. Synthetic objects only.");return 0;
}
}

extern "C" __declspec(dllexport) uint32_t __cdecl GoldMovementPrecisionGuardOracle(uint32_t edition){return edition==1 && GoldMovementPrecision::Guard()?1u:0u;}
namespace {
DWORD WINAPI ForeignPrecision(void* argument) {
 unsigned short cw=0x37f;__asm fldcw cw
 GoldMovementPrecision::MovementScope scope{};
 GoldMovementPrecision::BeginMovement(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(units[0])+0xd8),scope);
 PrecisionResult value{};CallPrecision(value);unsigned short after=0;__asm fnstcw after
 *static_cast<DWORD*>(argument)=!scope.active && after==cw && precision_cw==cw && value.eax==0x1234abcd && !value.delta?1u:0u;
 return 0;
}
bool HostGuardReject(){return false;}
bool HostGuardAccept(){return true;}
int TestForeignAndImageGate() {
 using namespace GoldMovementPrecision;
 owner=GetCurrentThreadId();ready=&PrecisionReady;installed=true;original_path=reinterpret_cast<uintptr_t>(&PrecisionNativePath);
 const auto paths=path_applied,movements=movement_applied;DWORD okay=0;HANDLE thread=CreateThread(nullptr,0,&ForeignPrecision,&okay,0,nullptr);if(!thread)return 60;
 if(WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0)return 61;CloseHandle(thread);
 if(!okay || paths!=path_applied || movements!=movement_applied || path_depth || movement_depth)return 62;
 installed=false;owner=0;ready=nullptr;original_path=0;
 const auto entry=reinterpret_cast<uintptr_t>(&PrecisionNativePath);
 if(Install(GetCurrentThreadId(),&PrecisionReady,entry,nullptr) || Install(GetCurrentThreadId(),&PrecisionReady,entry,&HostGuardReject)
  || Install(GetCurrentThreadId(),&PrecisionReady,entry,&HostGuardAccept) || installed || owner || original_path)return 63;
 puts("PASS foreign thread retains its native precision/original call; mandatory image guards reject an ordinary host before patching.");return 0;
}
}
int main() {
 logic_pointer=reinterpret_cast<uintptr_t>(logic);logic[0]=0xf317e0;Put(logic,0xec,reinterpret_cast<uintptr_t>(registry));
 registry[0]=0xf3dbd0;Put(registry,0xc,reinterpret_cast<uintptr_t>(members));Put(registry,0x10,reinterpret_cast<uintptr_t>(members)+sizeof(members));
 members[15*2]=65551;members[15*2+1]=reinterpret_cast<uintptr_t>(units[0]);units[0][0]=0xf3f084;
 Put(units[0],0xd8,0xf3ed90);Put(units[0],0x274,reinterpret_cast<uintptr_t>(units[0])+0xd8);
 if(const int precision=TestPrecision())return precision;
 return TestForeignAndImageGate();
}
