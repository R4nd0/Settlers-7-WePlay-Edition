#include "crossplay_gold_building_math.h"

#include <cstdio>
namespace {
alignas(16) unsigned char object[0x190]{},main_object[0x200]{},manager[8]{},property[0xf0]{};
uintptr_t property_table[1]{};float input_a[6]={113.7f,-219.4f,17.3f,25.9f,7.25f,0},input_b[6]={215.9f,-13.7f,25.3f,9.7f,7.25f,0},output_a[6]{},output_b[6]{};
bool enabled=false;unsigned calls=0;DWORD original_error=0;
bool Ready(){return enabled;}
void __cdecl Capture(void* self){if(self==object)++calls;original_error=GetLastError();SetLastError(0x12345678);}
__declspec(naked) void Original(){__asm {
    push ecx
    call Capture
    add esp,4
    mov eax,0x13572468
    mov edx,0x24681357
    movss xmm0,dword ptr [input_a]
    fld1
    stc
    ret
}}
void Word(void* bytes,size_t offset,uintptr_t value){memcpy(static_cast<unsigned char*>(bytes)+offset,&value,4);}
bool Run() {
    using GoldBuildingMath::Bridge;DWORD prior=0;uintptr_t before=0,after=0,result_ax=0,result_dx=0,flags=0,result_bx=0,result_si=0,result_di=0;float stack_float=0,xmm=0;
    unsigned short cw_before=0,cw_after=0;const auto sse=_mm_getcsr();__asm fnstcw cw_before
    SetLastError(0x76543210);prior=calls;
    __asm {
        push ebx
        push esi
        push edi
        mov before,esp
        mov ebx,0x11112222
        mov esi,0x33334444
        mov edi,0x55556666
        lea ecx,object
        call Bridge
        mov result_ax,eax
        mov result_dx,edx
        pushfd
        pop flags
        mov after,esp
        mov result_bx,ebx
        mov result_si,esi
        mov result_di,edi
        fstp stack_float
        movss xmm,xmm0
        pop edi
        pop esi
        pop ebx
        fnstcw cw_after
    }
    return before==after && result_ax==0x13572468 && result_dx==0x24681357 && (flags&1) && result_bx==0x11112222 && result_si==0x33334444 && result_di==0x55556666
        && stack_float==1.0f && xmm==input_a[0] && cw_before==cw_after && sse==_mm_getcsr()
        && calls==prior+1 && original_error==0x76543210 && GetLastError()==0x12345678;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayBuildingGuardOracle(uint32_t edition) {
    return edition==1 && GoldBuildingMath::Guard()?1u:0u;
}
int main() {
    using namespace GoldBuildingMath;
    uintptr_t main_pointer=reinterpret_cast<uintptr_t>(main_object);main_address=reinterpret_cast<uintptr_t>(&main_pointer);
    Word(main_object,0x18c,reinterpret_cast<uintptr_t>(manager));Word(manager,4,reinterpret_cast<uintptr_t>(property_table));property_table[0]=reinterpret_cast<uintptr_t>(property);
    Word(object,0,tables[0]);float position[3]={86285.25f,42440.75f,137.13f};memcpy(object+0x28,position,12);
    const Vector va{reinterpret_cast<uintptr_t>(output_a),reinterpret_cast<uintptr_t>(output_a)+24,reinterpret_cast<uintptr_t>(output_a)+24};memcpy(object+0x140,&va,12);
    const Vector vb{reinterpret_cast<uintptr_t>(output_b),reinterpret_cast<uintptr_t>(output_b)+24,reinterpret_cast<uintptr_t>(output_b)+24};memcpy(object+0x150,&vb,12);
    Word(property,0x60,reinterpret_cast<uintptr_t>(input_a));Word(property,0x64,reinterpret_cast<uintptr_t>(input_a)+24);
    Word(property,0x70,reinterpret_cast<uintptr_t>(input_b));Word(property,0x74,reinterpret_cast<uintptr_t>(input_b)+24);
    const BuildingPointMath::Point singles[4]={{217.33f,-112.7f},{0,0},{37.13f,19.7f},{0,0}};memcpy(property+0xd0,singles,sizeof(singles));
    owner=GetCurrentThreadId();ready=&Ready;original=reinterpret_cast<uintptr_t>(&Original);
    if(!Run() || corrected || excluded)return 3;
    enabled=true;if(!Run() || corrected!=1 || excluded)return 4;
    const auto expected=BuildingPointMath::Transform({input_a[0],input_a[1]},{position[0],position[1]},BuildingPointMath::RotateDegrees(position[2]));
    if(memcmp(&expected,output_a,8) || memcmp(object+0x17c,object+0x184,8))return 5;
    owner^=1;if(!Run() || corrected!=1 || excluded)return 6;owner=GetCurrentThreadId();
    const auto old=output_a[0];Word(object,0x144,reinterpret_cast<uintptr_t>(output_a)+23);
    if(!Run() || corrected!=1 || excluded!=1 || old!=output_a[0])return 7;
    Word(object,0x144,reinterpret_cast<uintptr_t>(output_a)+24);float infinity=INFINITY;memcpy(object+0x30,&infinity,4);
    if(!Run() || corrected!=1 || excluded!=2)return 8;
    puts("PASS actual compiled building bridge: original call, ECX, native EAX/EDX, flags, ESP/nonvolatile registers, x87 return, XMM result, FP controls and LastError preserved; guarded valid correction, disabled/foreign-owner/invalid-vector/nonfinite paths qualified. No game started.");return 0;
}
