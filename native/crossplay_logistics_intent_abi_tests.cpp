#define S7_GOLD_LOGISTICS_BINDING_TESTING
#define S7_CROSSPLAY_LOGISTICS_TESTING
#include "crossplay_logistics_intent.h"
#include <xmmintrin.h>
#include <cstdio>
namespace {
uintptr_t invoke=0;uint32_t self=0,left=0x12345678,right=0x23456789;
uint32_t captured_self=0,captured_left=0,captured_right=0,captured_flags=0,captured_error=0,calls=0;
uint32_t result_eax=0,result_ecx=0,result_edx=0,result_ebx=0,result_esi=0,result_edi=0,result_ebp=0,result_flags=0,before_sp=0,after_sp=0,wanted_ebp=0;
alignas(16) unsigned char original_fp[512]{},after_fp[512]{};alignas(16) float vector[4]={1.25f,2.5f,3.75f,5.f};double number=3.14159;
bool Ready(){SetLastError(0xdeadbeef);return false;}
void Capture(){++calls;captured_error=GetLastError();SetLastError(0x12345678);}
__declspec(naked) void Tail(){__asm{
 mov captured_self,ecx
 mov eax,[ebp+8]
 mov captured_left,eax
 mov eax,[ebp+12]
 mov captured_right,eax
 pushfd
 pop captured_flags
 pushad
 call Capture
 popad
 fxsave original_fp
 mov eax,0x87654301
 mov ecx,0xabcdef12
 mov edx,0x1234abcd
 stc
 mov esp,ebp
 pop ebp
 ret 8
}}
__declspec(naked) void Invoke(){__asm{
 push ebp
 push ebx
 push esi
 push edi
 mov before_sp,esp
 mov wanted_ebp,ebp
 movups xmm0,vector
 fld qword ptr [number]
 mov eax,0x11223344
 mov ecx,self
 mov edx,0x22334455
 mov ebx,0x33445566
 mov esi,0x44556677
 mov edi,0x55667788
 push right
 push left
 clc
 call dword ptr [invoke]
 mov result_eax,eax
 mov result_ecx,ecx
 mov result_edx,edx
 mov result_ebx,ebx
 mov result_esi,esi
 mov result_edi,edi
 mov result_ebp,ebp
 mov after_sp,esp
 pushfd
 pop result_flags
 fxsave after_fp
 fstp st(0)
 pop edi
 pop esi
 pop ebx
 pop ebp
 ret
}}
}
int main(){
 CrossplayLogisticsIntent::owner=GetCurrentThreadId();CrossplayLogisticsActive::begun=true;CrossplayLogisticsActive::ready=Ready;
 CrossplayLogisticsIntent::resume=reinterpret_cast<uintptr_t>(&Tail);self=0x12340000;
 for(unsigned profile=1;profile<=3;++profile){
  invoke=reinterpret_cast<uintptr_t>(profile==1?&CrossplayLogisticsIntent::GoldBridge:&CrossplayLogisticsIntent::HistoryBridge);
  const auto before=calls;SetLastError(0x76543210);_mm_setcsr(0x1f80);unsigned short cw=0;__asm{fnstcw cw}
  Invoke();unsigned short next_cw=0;__asm{fnstcw next_cw}
  if(calls!=before+1 || captured_self!=self || captured_left!=left || captured_right!=right || (captured_flags&1) || !(result_flags&1)
   || captured_error!=0x76543210 || GetLastError()!=0x12345678 || before_sp!=after_sp || result_eax!=0x87654301 || result_ecx!=0xabcdef12 || result_edx!=0x1234abcd
   || result_ebx!=0x33445566 || result_esi!=0x44556677 || result_edi!=0x55667788 || result_ebp!=wanted_ebp || cw!=next_cw || _mm_getcsr()!=0x1f80 || memcmp(original_fp+32,after_fp+32,256)){
   printf("FAIL intent ABI profile%u error%x/%x sp%u/%u\n",profile,captured_error,GetLastError(),before_sp,after_sp);return 1;
  }
 }
 puts("PASS all3 original priority-entry bridge ABIs: original arguments/return/GP/flags/FX/LastError, exactly one original call and native stack cleanup; no game hooks installed.");return 0;
}
