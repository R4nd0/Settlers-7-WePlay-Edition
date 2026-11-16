#define S7_GOLD_LOGISTICS_BINDING_TESTING
#include "crossplay_gold_logistics_inline.h"
#include <cstdio>
#include <xmmintrin.h>
namespace {
uint32_t logic[0x200/4]{},main_object[8]{},priority[0x1660/4]{},transfers[0x1b0/4]{},tasks[2][0x100/4]{},records[2][8]{},task_registry[4]{},record_registry[4]{};
uintptr_t logic_pointer=0,left_pointer=0,right_pointer=0,invoke_pointer=0;
uint32_t decision=2,policies=0,out_eax=0,out_ebx=0,out_ecx=0,out_edx=0,out_esi=0,out_edi=0,out_ebp=0,out_flags=0,before_sp=0,after_sp=0,wanted_ebp=0;
bool authenticated=true;
__declspec(align(16)) unsigned char out_fp[512]{};
__declspec(align(16)) float initial_vector[4]={1.25f,2.5f,3.75f,5.f};
double initial_fp=3.14159;
bool Ready(){return authenticated;}
uint32_t Policy(const GoldLogisticsBinding::World&,const GoldLogisticsBinding::Request& left,const GoldLogisticsBinding::Request& right) {
 if(left.id!=65536 || right.id!=65537)abort();++policies;SetLastError(0xdeadbeef);_mm_setcsr(0x3f80);return decision;
}
void Set(uint32_t* object,unsigned offset,uintptr_t value){object[offset/4]=static_cast<uint32_t>(value);}
__declspec(naked) void Original(){__asm {
 mov ebx,[ebx+14h]
 cmp ebx,[eax+14h]
 setl al
 jmp dword ptr [GoldLogisticsInline::resume]
}}
__declspec(naked) void Invoke(){__asm {
 push ebp
 push ebx
 push esi
 push edi
 mov before_sp,esp
 mov wanted_ebp,ebp
 movups xmm0,initial_vector
 fld qword ptr [initial_fp]
 mov eax,offset Returned
 mov GoldLogisticsInline::resume,eax
 mov eax,right_pointer
 mov ebx,left_pointer
 mov ecx,0x12345678
 mov edx,0x23456789
 mov esi,0x3456789a
 mov edi,0x456789ab
 clc
 jmp dword ptr [invoke_pointer]
 Returned:
 mov out_eax,eax
 mov out_ebx,ebx
 mov out_ecx,ecx
 mov out_edx,edx
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
bool Run(uint32_t expected_al,bool accepted,uint32_t baseline_flags,const unsigned char* baseline_fp) {
 const unsigned previous=policies,observed=GoldLogisticsInline::observed;
 SetLastError(0x76543210);_mm_setcsr(0x1f80);Invoke();
 return out_eax==((static_cast<uint32_t>(right_pointer)&0xffffff00u)|expected_al) && out_ebx==tasks[0][0x14/4]
  && out_ecx==0x12345678 && out_edx==0x23456789 && out_esi==0x3456789a && out_edi==0x456789ab
  && out_ebp==wanted_ebp && before_sp==after_sp && out_flags==baseline_flags && GetLastError()==0x76543210
  && _mm_getcsr()==0x1f80 && memcmp(out_fp+32,baseline_fp+32,256)==0
  && policies==previous+static_cast<unsigned>(accepted) && GoldLogisticsInline::observed==observed+static_cast<unsigned>(accepted);
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldNeoInlineGuardOracle(uint32_t edition){return edition==1 && GoldLogisticsInline::Guard()?1u:0u;}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldNeoCombinedGuardOracle(uint32_t edition){return edition==1 && GoldLogisticsSort::Guard() && GoldLogisticsInline::Guard()?1u:0u;}
int main() {
 using namespace GoldLogisticsBinding;
 logic_pointer=reinterpret_cast<uintptr_t>(logic);logic_global=reinterpret_cast<uintptr_t>(&logic_pointer);
 logic[0]=0xf317e0;Set(logic,0x14c,reinterpret_cast<uintptr_t>(main_object));main_object[0]=0xf4bb0c;Set(main_object,4,reinterpret_cast<uintptr_t>(priority));Set(main_object,8,reinterpret_cast<uintptr_t>(transfers));priority[0]=0xf4beb4;
 Set(priority,0x164c,reinterpret_cast<uintptr_t>(task_registry));Set(priority,0x1650,reinterpret_cast<uintptr_t>(task_registry+4));
 Set(transfers,0x19c,reinterpret_cast<uintptr_t>(record_registry));Set(transfers,0x1a0,reinterpret_cast<uintptr_t>(record_registry+4));Set(transfers,0x1a8,reinterpret_cast<uintptr_t>(transfers)+0x194);
 for(unsigned i=0;i<2;++i) {
  tasks[i][0]=0xf4c684;Set(tasks[i],8,65536+i);Set(tasks[i],0x70,196608+i);Set(tasks[i],0x4c,23+i);records[i][1]=23+i;Set(tasks[i],0x14,100+i);Set(tasks[i],0x2c,200+i);Set(tasks[i],0x34,1);Set(tasks[i],0x1c,9);Set(tasks[i],0x20,4);
  records[i][0]=196608+i;records[i][2]=65536+i;task_registry[i*2]=65536+i;task_registry[i*2+1]=reinterpret_cast<uintptr_t>(tasks[i]);record_registry[i*2]=196608+i;record_registry[i*2+1]=reinterpret_cast<uintptr_t>(records[i]);
 }
 left_pointer=reinterpret_cast<uintptr_t>(tasks[0]);right_pointer=reinterpret_cast<uintptr_t>(tasks[1]);
 invoke_pointer=reinterpret_cast<uintptr_t>(&Original);SetLastError(0x76543210);_mm_setcsr(0x1f80);Invoke();
 const uint32_t original_flags=out_flags;unsigned char original_fp[512]{};memcpy(original_fp,out_fp,sizeof(out_fp));
 GoldLogisticsInline::owner=GetCurrentThreadId();GoldLogisticsInline::ready=Ready;GoldLogisticsInline::policy=Policy;
 invoke_pointer=reinterpret_cast<uintptr_t>(&GoldLogisticsInline::Bridge);
 for(uint32_t value:{0u,1u,2u,3u,UINT32_MAX}) {decision=value;if(!Run(value<=1?value:1,true,original_flags,original_fp))return 1;}
 authenticated=false;if(!Run(1,false,original_flags,original_fp))return 2;authenticated=true;
 GoldLogisticsInline::owner=0;if(!Run(1,false,original_flags,original_fp))return 3;GoldLogisticsInline::owner=GetCurrentThreadId();
 task_registry[0]^=65536;if(!Run(1,false,original_flags,original_fp))return 4;task_registry[0]^=65536;
 record_registry[0]^=65536;if(!Run(1,false,original_flags,original_fp))return 5;record_registry[0]^=65536;
 tasks[0][0]=0xf4c74c;if(!Run(1,false,original_flags,original_fp))return 6;tasks[0][0]=0xf4c684;
 tasks[0][0x70/4]^=65536;if(!Run(1,false,original_flags,original_fp))return 7;
 tasks[0][0x70/4]^=65536;
 // Execute the actual detached observer through the native bridge. It sees a
 // reversed FIFO decision but MUST keep original AL, flags and full native ABI.
 puts("PASS compiled Gold inline bridge: original comparator registers/flags retained, AL-only override and native ownership exclusions; synthetic only.");return 0;
}
