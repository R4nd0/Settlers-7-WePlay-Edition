#include "crossplay_gold_animal_path_points.h"
#include <cstdio>
namespace {
alignas(16) unsigned char animal[0x200]{},logic[0x120]{},manager[0x40]{};
uint32_t registry[2]{};uintptr_t logic_pointer=0,vector_pointer=0,system_pointer=0x12345000;
bool enabled=false,plus=false,bad_return=false;unsigned calls=0;DWORD native_error=0;
uintptr_t before=0,after=0,result=0,result_si=0,result_bp=0,expected_bp=0,result_bx=0,result_di=0,flags=0;
uintptr_t native_ax=0,native_dx=0,native_cx=0,native_flags=0,point_pointer=0;
float ratio=1.f/3.f,cx=149600.f,cy=94300.f,px=89500.f,py=13100.f;
float xmm_value=123.5f,returned_xmm=0,middle=0,bottom=0,point[2]{};
bool Ready(){return enabled;}
void __cdecl Capture(){++calls;native_error=GetLastError();SetLastError(0x12345678);}
__declspec(naked) void Original(){__asm {
 mov native_ax,eax
 mov native_dx,edx
 mov native_cx,ecx
 pushfd
 pop native_flags
 mov eax,[edx]
 mov point,eax
 mov eax,[edx+4]
 mov [point+4],eax
 pushad
 call Capture
 popad
 mov eax,0x12345678
 stc
 ret
}}
__declspec(naked) void Thunk(uintptr_t,float){
 using GoldAnimalPathPoints::Bridge;using GoldAnimalPathPoints::first;using GoldAnimalPathPoints::second;
 __asm {
  push ebp
  mov ebp,esp
  sub esp,0x24
  push ebx
  push esi
  push edi
  mov expected_bp,ebp
  mov eax,ratio
  mov [ebp-0x10],eax
  mov eax,cy
  mov [ebp-8],eax
  mov eax,px
  mov [ebp-4],eax
  mov eax,py
  mov [ebp-0xc],eax
  mov dword ptr [ebp-0x24],0x3f800000
  mov dword ptr [ebp-0x20],0x40000000
  lea eax,[ebp-0x24]
  mov point_pointer,eax
  lea eax,ReturnPoint
  sub eax,5
  mov first,eax
  mov second,0
  cmp plus,0
  je CheckBad
  mov second,eax
  mov first,0
 CheckBad:
  cmp bad_return,0
  je Invoke
  mov first,0
  mov second,0
 Invoke:
  mov before,esp
  mov esi,0x11112222
  mov ebx,0x33334444
  mov edi,vector_pointer
  movss xmm0,xmm_value
  fld px
  fld py
  mov eax,0x88889999
  lea edx,[ebp-0x24]
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
bool Run(bool corrected){
 float expected[2]={1,2};if(corrected)HistoryPathPointMath::Point(ratio,cx,cy,px,py,plus,expected);
 const auto previous=calls,mxcsr=_mm_getcsr();_mm_setcsr(0x1f80);unsigned short cw_before=0,cw_after=0;
 __asm fnstcw cw_before
 SetLastError(0x76543210);Thunk(system_pointer,cx);
 __asm fnstcw cw_after
 bool ok=!memcmp(point,expected,8) && result==0x12345678 && before==after
  && result_si==0x11112222 && result_bp==expected_bp && result_bx==0x33334444 && result_di==vector_pointer && (flags&1) && (native_flags&1)
  && native_ax==0x88889999 && native_dx==point_pointer && native_cx==0xccccdddd
  && middle==py && bottom==px && returned_xmm==xmm_value && cw_before==cw_after && _mm_getcsr()==0x1f80
  && calls==previous+1 && native_error==0x76543210 && GetLastError()==0x12345678;
 if(!ok)printf("FAIL point%g/%g expected%g/%g SP%u/%u regs%u,%u,%u,%u CF%u/%u FP%g/%g xmm%g CW%u/%u MXCSR%x calls%u error%x/%x\n",point[0],point[1],expected[0],expected[1],static_cast<unsigned>(before),static_cast<unsigned>(after),result_si==0x11112222,result_bp==expected_bp,result_bx==0x33334444,result_di==vector_pointer,static_cast<unsigned>(flags&1),static_cast<unsigned>(native_flags&1),middle,bottom,returned_xmm,cw_before,cw_after,_mm_getcsr(),calls,native_error,GetLastError());
 _mm_setcsr(mxcsr);return ok;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldAnimalPathGuardOracle(uint32_t edition){return edition==1 && GoldAnimalPathPoints::Guard()?1u:0u;}
int main(){
 using namespace GoldAnimalPathPoints;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
 W(animal,0,0xf3f454);W(animal,0x28,65536);W(animal,0x2c,0xf404b0);vector_pointer=reinterpret_cast<uintptr_t>(animal+0x34);
 logic_pointer=reinterpret_cast<uintptr_t>(logic);GoldAnimalMovementTime::logic_global=reinterpret_cast<uintptr_t>(&logic_pointer);
 W(logic,0,0xf317e0);W(logic,0x24,system_pointer);W(logic,0xfc,reinterpret_cast<uintptr_t>(manager));W(manager,0,0xf3f784);
 registry[0]=65536;registry[1]=reinterpret_cast<uintptr_t>(animal);W(manager,0x24,reinterpret_cast<uintptr_t>(registry));W(manager,0x28,reinterpret_cast<uintptr_t>(registry+2));
 unsigned char initial[sizeof(animal)]{};memcpy(initial,animal,sizeof(animal));
 original=reinterpret_cast<uintptr_t>(&Original);owner=GetCurrentThreadId();ready=&Ready;
 if(!Run(false) || applied || excluded)return 1;
 char path[128]{};sprintf_s(path,"gold-animal-path-abi-%lu.private.jsonl",GetCurrentProcessId());
 report=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(report==INVALID_HANDLE_VALUE)return 11;
 enabled=true;if(!Run(true) || applied!=1 || excluded || records!=1 || memcmp(animal,initial,sizeof(animal)))return 2;
 plus=true;if(!Run(true) || applied!=2)return 3;plus=false;
 owner^=1;if(!Run(false) || applied!=2 || excluded)return 4;owner=GetCurrentThreadId();
 W(animal,0,0xf3f458);if(!Run(false) || excluded!=1)return 5;W(animal,0,0xf3f454);
 registry[0]^=1;if(!Run(false) || excluded!=2)return 6;registry[0]^=1;
 registry[1]+=4;if(!Run(false) || excluded!=3)return 7;registry[1]-=4;
 ratio=INFINITY;if(!Run(false) || excluded!=4)return 8;ratio=1.f/3.f;
 ratio=-1;if(!Run(false) || excluded!=5)return 9;ratio=1.f/3.f;
 W(animal,0x2c,0xf404b4);if(!Run(false) || excluded!=6)return 10;W(animal,0x2c,0xf404b0);
 W(logic,0x24,system_pointer+4);if(!Run(false) || excluded!=7)return 12;W(logic,0x24,system_pointer);
 bad_return=true;if(!Run(false) || excluded!=8)return 13;bad_return=false;
 for(unsigned i=0;i<40;++i){plus=(i%2)!=0;if(!Run(true))return 14;}
 if(applied!=42 || records!=32 || excluded!=8 || memcmp(animal,initial,sizeof(animal)))return 15;
 CloseHandle(report);report=INVALID_HANDLE_VALUE;
 puts("PASS compiled native path append bridge, both original CALL contexts, exact temporary stack point, original append forwarded once; EAX/ECX/EDX inputs, ESP/EBP/nonvolatiles, flags, x87, XMM, FP controls and LastError preserved. Authenticated owner, typed registry/class/Grid/root/caller and finite ratio exclusions,32diagnostic bound. No game object/RNG/CRC writes.");return 0;
}
