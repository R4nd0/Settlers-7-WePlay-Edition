#define S7_GOLD_LOGISTICS_BINDING_TESTING
#include "crossplay_gold_logistics_inline.h"
#include <cstdio>
#include <xmmintrin.h>
namespace {
uint32_t logic[0x200/4]{},main_object[8]{},priority[0x1660/4]{},transfers[0x1b0/4]{},tasks[2][0x100/4]{},records[2][8]{},task_registry[4]{},record_registry[4]{};
uintptr_t logic_pointer=0,comparator_pointer=0,left_pointer=0,right_pointer=0,invoke_pointer=0;
uint32_t captured_eax=0,captured_edx=0,captured_right=0,captured_flags=0,captured_error=0,original_calls=0,policy_calls=0,decision=2;
uint32_t out_eax=0,out_edx=0,out_ecx=0,out_ebx=0,out_esi=0,out_edi=0,out_ebp=0,out_flags=0,before_sp=0,after_sp=0,wanted_ebp=0;
bool authenticated=true;
__declspec(align(16)) unsigned char out_fp[512]{};
__declspec(align(16)) unsigned char original_fp[512]{};
__declspec(align(16)) float vector[4]={1.25f,2.5f,3.75f,5.f};
double fp_seed=3.14159;
void Set(uint32_t* object,unsigned offset,uintptr_t value){object[offset/4]=static_cast<uint32_t>(value);}
bool Ready(){return authenticated;}
uint32_t Policy(const GoldLogisticsBinding::World&,const GoldLogisticsBinding::Request& left,const GoldLogisticsBinding::Request& right,bool){
 ++policy_calls;if(left.id!=65536 || right.id!=65537)abort();SetLastError(0xdeadbeef);return decision;
}
void __cdecl Capture(){++original_calls;captured_error=GetLastError();SetLastError(0x12345678);}
__declspec(naked) void Original(){__asm {
 mov captured_eax,eax
 mov captured_edx,edx
 mov ecx,[esp+4]
 mov captured_right,ecx
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
 ret 4
}}
__declspec(naked) void Invoke(){__asm {
 push ebp
 push ebx
 push esi
 push edi
 mov before_sp,esp
 mov wanted_ebp,ebp
 movups xmm0,vector
 fld qword ptr [fp_seed]
 mov eax,offset Returned
 mov GoldLogisticsSort::caller,eax
 mov eax,comparator_pointer
 mov edx,left_pointer
 mov ecx,0x11223344
 mov ebx,0x22334455
 mov esi,0x33445566
 mov edi,0x44556677
 push right_pointer
 clc
 call dword ptr [invoke_pointer]
 Returned:
 mov out_eax,eax
 mov out_edx,edx
 mov out_ecx,ecx
 mov out_ebx,ebx
 mov out_esi,esi
 mov out_edi,edi
 mov out_ebp,ebp
 mov after_sp,esp
 pushfd
 pop out_flags
 fxsave out_fp
 fstp st(0)
 pop edi
 pop esi
 pop ebx
 pop ebp
 ret
}}
bool Run(uint32_t expected_al,bool accepted){
 const auto calls=original_calls,policies=policy_calls,observations=GoldLogisticsSort::observed;
 SetLastError(0x76543210);_mm_setcsr(0x1f80);
 unsigned short cw=0;__asm {fnstcw cw}
 Invoke();
 unsigned short after_cw=0;__asm {fnstcw after_cw}
 bool ok=out_eax==(0x87654300u|expected_al) && out_ecx==0xabcdef12 && out_edx==0x1234abcd
  && out_ebx==0x22334455 && out_esi==0x33445566 && out_edi==0x44556677 && out_ebp==wanted_ebp && before_sp==after_sp
  && captured_eax==comparator_pointer && captured_edx==left_pointer && captured_right==right_pointer
  && (captured_flags&1)==0 && (out_flags&1)==1 && original_calls==calls+1
  && captured_error==0x76543210 && GetLastError()==0x12345678 && cw==after_cw && _mm_getcsr()==0x1f80
  && GoldLogisticsSort::observed==observations+static_cast<unsigned>(accepted)
  && policy_calls==policies+static_cast<unsigned>(accepted)
  && memcmp(out_fp+32,original_fp+32,256)==0;
 if(!ok)printf("FAIL AL%x SP%u/%u error%x/%x observations%u policies%u original%u flags%x/%x\n",out_eax,before_sp,after_sp,captured_error,GetLastError(),GoldLogisticsSort::observed,policy_calls,original_calls,captured_flags,out_flags);
 return ok;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldNeoSortGuardOracle(uint32_t edition){return edition==1 && GoldLogisticsSort::Guard()?1u:0u;}
int main(){
 using namespace GoldLogisticsBinding;
 logic_pointer=reinterpret_cast<uintptr_t>(logic);logic_global=reinterpret_cast<uintptr_t>(&logic_pointer);
 logic[0]=0xf317e0;Set(logic,0x14c,reinterpret_cast<uintptr_t>(main_object));main_object[0]=0xf4bb0c;Set(main_object,4,reinterpret_cast<uintptr_t>(priority));Set(main_object,8,reinterpret_cast<uintptr_t>(transfers));priority[0]=0xf4beb4;
 Set(priority,0x164c,reinterpret_cast<uintptr_t>(task_registry));Set(priority,0x1650,reinterpret_cast<uintptr_t>(task_registry+4));
 Set(transfers,0x19c,reinterpret_cast<uintptr_t>(record_registry));Set(transfers,0x1a0,reinterpret_cast<uintptr_t>(record_registry+4));Set(transfers,0x1a8,reinterpret_cast<uintptr_t>(transfers)+0x194);
 for(unsigned i=0;i<2;++i){
  tasks[i][0]=0xf4c684;Set(tasks[i],8,65536+i);Set(tasks[i],0x70,196608+i);Set(tasks[i],0x4c,23+i);records[i][1]=23+i;Set(tasks[i],0x14,100+i);Set(tasks[i],0x2c,200+i);Set(tasks[i],0x34,1);Set(tasks[i],0x1c,9);Set(tasks[i],0x20,4);
  records[i][0]=196608+i;records[i][2]=65536+i;task_registry[i*2]=65536+i;task_registry[i*2+1]=reinterpret_cast<uintptr_t>(tasks[i]);record_registry[i*2]=196608+i;record_registry[i*2+1]=reinterpret_cast<uintptr_t>(records[i]);
 }
 uint32_t comparator=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(transfers)+0x194),left=196608,right=196609;
 comparator_pointer=reinterpret_cast<uintptr_t>(&comparator);left_pointer=reinterpret_cast<uintptr_t>(&left);right_pointer=reinterpret_cast<uintptr_t>(&right);
 GoldLogisticsSort::owner=GetCurrentThreadId();GoldLogisticsSort::ready=Ready;GoldLogisticsSort::policy=Policy;
 GoldLogisticsSort::original=reinterpret_cast<uintptr_t>(&Original);invoke_pointer=reinterpret_cast<uintptr_t>(&GoldLogisticsSort::Bridge);
 for(uint32_t value:{0u,1u,2u,3u,UINT32_MAX}){decision=value;if(!Run(value<=1?value:1,true))return 1;}
 authenticated=false;if(!Run(1,false))return 2;authenticated=true;
 GoldLogisticsSort::owner=0;if(!Run(1,false))return 3;GoldLogisticsSort::owner=GetCurrentThreadId();
 puts("PASS compiled Gold sort bridge: original forwarding, AL-only override/fallback and native ownership exclusions; synthetic only.");return 0;
}
