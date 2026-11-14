#include "crossplay_gold_animal_movement_time.h"
#include <cstdio>
namespace {
alignas(16) unsigned char animal[0x200]{},logic[0x120]{},manager[0x40]{};
uint32_t registry[2]{};uintptr_t logic_pointer=0,movement=0;
bool enabled=false;unsigned calls=0;DWORD native_error=0;
uintptr_t before=0,after=0,result=0,result_si=0,result_bp=0,expected_bp=0,result_bx=0,result_di=0,flags=0;
uintptr_t native_ax=0,native_dx=0,native_cx=0;
float distance=9.0f,multiplier=0.2f,observed_distance=9.0f,observed_multiplier=0.2f;
float xmm_value=123.5f,returned_xmm=0,middle=0,bottom=0;double converted=0;
bool Ready(){return enabled;}
void __cdecl Capture(){++calls;native_error=GetLastError();SetLastError(0x12345678);}
__declspec(naked) void Original(){__asm {
 mov native_ax,eax
 mov native_dx,edx
 mov native_cx,ecx
 fstp qword ptr [converted]
 pushad
 call Capture
 popad
 cvttsd2si eax,qword ptr [converted]
 stc
 ret
}}
__declspec(naked) void Thunk(){
 using GoldAnimalMovementTime::Bridge;using GoldAnimalMovementTime::site;
 __asm {
  push ebp
  mov ebp,esp
  and esp,0xfffffff8
  sub esp,0x44
  push ebx
  push esi
  push edi
  mov expected_bp,ebp
  mov eax,observed_distance
  mov [esp+0x18],eax
  mov eax,observed_multiplier
  mov [esp+0x14],eax
  lea eax,ReturnPoint
  sub eax,5
  mov site,eax
  mov before,esp
  mov esi,movement
  mov ebx,0x11112222
  mov edi,100
  movss xmm0,xmm_value
  fld distance
  fld multiplier
  fld distance
  fdiv multiplier
  mov eax,0x88889999
  mov edx,0xaaaabbbb
  mov ecx,0xccccdddd
  stc
  call Bridge
 ReturnPoint:
  mov result,eax
  mov result_si,esi
  mov result_bp,ebp
  mov result_bx,ebx
  mov result_di,edi
  pushfd
  pop flags
  mov after,esp
  movss returned_xmm,xmm0
  fstp middle
  fstp bottom
  pop edi
  pop esi
  pop ebx
  mov esp,ebp
  pop ebp
  ret
 }
}
void W(void* p,unsigned offset,uint32_t value){memcpy(static_cast<unsigned char*>(p)+offset,&value,4);}
bool Run(unsigned expected){
 const auto previous=calls;const auto mxcsr=_mm_getcsr();_mm_setcsr(mxcsr&~0x3fu);unsigned short cw_before=0,cw_after=0;
 __asm fnstcw cw_before
 SetLastError(0x76543210);Thunk();
 __asm fnstcw cw_after
 const auto native_mxcsr=_mm_getcsr(),expected_mxcsr=(mxcsr&~0x3fu)|(expected==44?0x20u:0u);
 bool ok=result==expected && before==after && result_si==movement && result_bp==expected_bp
  && result_bx==0x11112222 && result_di==100 && (flags&1)
  && native_ax==0x88889999 && native_dx==0xaaaabbbb && native_cx==0xccccdddd
  && middle==multiplier && bottom==distance && returned_xmm==xmm_value && cw_before==cw_after
  && native_mxcsr==expected_mxcsr && calls==previous+1 && native_error==0x76543210 && GetLastError()==0x12345678;
 if(!ok)printf("FAIL result%u expected%u SP%u/%u SI%u BP%u BX%u DI%u CF%u FP%g/%g xmm%g CW%u/%u MXCSR%u/%u calls%u error%x/%x\n",static_cast<unsigned>(result),expected,static_cast<unsigned>(before),static_cast<unsigned>(after),result_si==movement,result_bp==expected_bp,result_bx==0x11112222,result_di==100,static_cast<unsigned>(flags&1),middle,bottom,returned_xmm,cw_before,cw_after,_mm_getcsr(),mxcsr,calls,native_error,GetLastError());
 _mm_setcsr(mxcsr);return ok;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldAnimalMovementTimeGuardOracle(uint32_t edition){return edition==1 && GoldAnimalMovementTime::Guard()?1u:0u;}
int main(){
 using namespace GoldAnimalMovementTime;LoadProgressDetail::FpScope fp;
 W(animal,0,0xf3f454);W(animal,0x28,65536);W(animal,0x2c,0xf404b0);movement=reinterpret_cast<uintptr_t>(animal+0x2c);
 logic_pointer=reinterpret_cast<uintptr_t>(logic);logic_global=reinterpret_cast<uintptr_t>(&logic_pointer);
 W(logic,0,0xf317e0);W(logic,0xfc,reinterpret_cast<uintptr_t>(manager));W(manager,0,0xf3f784);
 registry[0]=65536;registry[1]=reinterpret_cast<uintptr_t>(animal);W(manager,0x24,reinterpret_cast<uintptr_t>(registry));W(manager,0x28,reinterpret_cast<uintptr_t>(registry+2));
 const auto original_animal=animal;unsigned char initial[sizeof(animal)]{};memcpy(initial,animal,sizeof(animal));
 original=reinterpret_cast<uintptr_t>(&Original);owner=GetCurrentThreadId();ready=&Ready;
 if(!Run(44) || applied || excluded)return 1;
 char report_path[128]{};sprintf_s(report_path,"movement-cost-abi-%lu.private.jsonl",GetCurrentProcessId());
 report=CreateFileA(report_path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(report==INVALID_HANDLE_VALUE)return 11;
 enabled=true;if(!Run(45) || applied!=1 || excluded || records!=1 || memcmp(animal,initial,sizeof(animal)))return 2;
 owner^=1;if(!Run(44) || applied!=1 || excluded)return 3;owner=GetCurrentThreadId();
 W(animal,0,0xf3f458);if(!Run(44) || applied!=1 || excluded!=1)return 4;W(animal,0,0xf3f454);
 registry[0]^=1;if(!Run(44) || applied!=1 || excluded!=2)return 5;registry[0]^=1;
 registry[1]+=4;if(!Run(44) || applied!=1 || excluded!=3)return 6;registry[1]-=4;
 observed_multiplier=0;if(!Run(44) || applied!=1 || excluded!=4)return 7;observed_multiplier=multiplier;
 observed_distance=INFINITY;if(!Run(44) || applied!=1 || excluded!=5)return 8;observed_distance=distance;
 W(animal,0x2c,0xf404b4);if(!Run(44) || applied!=1 || excluded!=6)return 9;W(animal,0x2c,0xf404b0);
 if(memcmp(original_animal,initial,sizeof(animal)))return 10;
 for(unsigned i=0;i<40;++i)if(!Run(45))return 12;
 if(applied!=41 || records!=32 || excluded!=6 || memcmp(animal,initial,sizeof(animal)))return 13;
 CloseHandle(report);report=INVALID_HANDLE_VALUE;
 puts("PASS compiled movement quotient bridge: 44->45 only for owned authenticated typed registered animal; native original conversion forwarding and EAX/ECX/EDX inputs, ESP, nonvolatile registers, flags, remaining x87 stack, XMM result, FP controls and LastError preserved; disabled/foreign-owner/class/registry/nonfinite/zero-speed exclusions; no animal/RNG/state writes.");return 0;
}
