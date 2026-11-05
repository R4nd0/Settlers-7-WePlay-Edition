#define S7_WORKSHOP_ANGLES_TESTING
#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}

#ifdef S7_LOCAL_NETWORK_PUMP
namespace {
alignas(16) unsigned char original_fx[512]{}, expected_fx[512]{}, actual_fx[512]{};
uint32_t expected_gpr[8]{}, actual_gpr[8]{};
uint32_t expected_flags=0,actual_flags=0,constructor_calls=0,canary=0;
uintptr_t object_address=0,object_table=0,saved_stack=0,returned_stack=0;
__declspec(naked) void FakeWorkshopConstructor() {
    __asm {
        mov eax, object_table
        mov [ecx], eax
        inc constructor_calls
        mov eax, ecx
        pxor xmm1, xmm1
        fldpi
        stc
        pushfd
        pushad
        fxsave expected_fx
        mov esi, esp
        lea edi, expected_gpr
        mov ecx, 8
        rep movsd
        mov eax, [esp + 32]
        mov expected_flags, eax
        popad
        popfd
        ret
    }
}
__declspec(naked) void ExerciseWorkshopConstructor() {
    __asm {
        pushfd
        pushad
        mov saved_stack, esp
        fxsave original_fx
        fld1
        push 0xa55aa55a
        mov eax, 0x11111111
        mov ecx, object_address
        mov edx, 0x33333333
        mov ebx, 0x44444444
        mov ebp, 0x55555555
        mov esi, 0x66666666
        mov edi, 0x77777777
        call ConstructLocalWorkshopPlan
        mov returned_stack, esp
        pushfd
        pushad
        fxsave actual_fx
        mov esi, esp
        lea edi, actual_gpr
        mov ecx, 8
        rep movsd
        mov eax, [esp + 32]
        mov actual_flags, eax
        popad
        popfd
        mov eax, [esp]
        mov canary, eax
        fxrstor original_fx
        mov esp, saved_stack
        popad
        popfd
        ret
    }
}
}
#endif

int main() {
#ifdef S7_LOCAL_NETWORK_PUMP
    unsigned checks=0,failures=0;
    auto check=[&](bool okay,int line) {++checks;if(!okay){++failures;std::printf("Failed workshop check %u line %d\n",checks,line);}};
#define check(x) check((x),__LINE__)
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1106000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    check(image!=nullptr);if(!image)return 1;
    image_base=reinterpret_cast<uintptr_t>(image);
    for(bool steam:{false,true}) {
        steam_history=steam;
        alignas(16) uint32_t object[66];
        object_address=reinterpret_cast<uintptr_t>(object+1);
        object_table=NativeVA(0x1159F00);
        local_workshop_constructor=reinterpret_cast<uintptr_t>(&FakeWorkshopConstructor);
        initialization=2;local_connect_probe=true;bootstrap_thread=GetCurrentThreadId();
        for(bool clobber:{false,true}) {
            for(auto& word:object)word=0x6e1cbbda;
            workshop_angles_test_clobber=clobber;
            constructor_calls=0;SetLastError(3456);
            ExerciseWorkshopConstructor();
            check(constructor_calls==1 && actual_gpr[7]==object_address);
            check(returned_stack+4==saved_stack && canary==0xa55aa55a);
            check(actual_flags==expected_flags && GetLastError()==3456);
            for(unsigned i=0;i<8;++i)if(i!=3)check(expected_gpr[i]==actual_gpr[i]);
            check(memcmp(expected_fx,actual_fx,16)==0 && memcmp(expected_fx+24,actual_fx+24,8)==0);
            check(memcmp(expected_fx+32,actual_fx+32,384)==0);
            for(unsigned i=0;i<66;++i) {
                if(i==1)check(object[i]==object_table);
                else if(i>=25 && i<32) {
                    const float angle=30.0f*static_cast<float>(i-25);uint32_t bits=0;memcpy(&bits,&angle,4);
                    check(object[i]==bits);
                } else check(object[i]==0x6e1cbbda);
            }
        }
        workshop_angles_test_clobber=false;
        for(unsigned invalid=0;invalid<4;++invalid) {
            for(auto& word:object)word=0xa55aa55a;
            object[1]=static_cast<uint32_t>(object_table);
            initialization=invalid==0?1:2;local_connect_probe=invalid!=1;
            bootstrap_thread=GetCurrentThreadId()+(invalid==2?1:0);
            if(invalid==3)object[1]^=4;
            uint32_t before[66];memcpy(before,object,sizeof(object));SetLastError(789);
            InitializeLocalWorkshopAngles(object+1);
            check(memcmp(before,object,sizeof(object))==0 && GetLastError()==789);
        }
        initialization=2;local_connect_probe=true;bootstrap_thread=GetCurrentThreadId();
        InitializeLocalWorkshopAngles(nullptr);check(GetLastError()==789);
        // Exercise the actual guarded factory-call patch and rollback.
        const Patch* selected=nullptr;
        for(const auto& patch:ConnectPatches)if(patch.va==0x9BF23C)selected=&patch;
        check(selected!=nullptr);
        if(selected) {
            auto* at=PatchAddress(*selected);memcpy(at,OriginalBytes(*selected),5);
            check(WritePatch(*selected,false) && at[0]==0xe8);
            int32_t relative=0;memcpy(&relative,at+1,4);
            check(reinterpret_cast<uintptr_t>(at+5)+relative==reinterpret_cast<uintptr_t>(&ConstructLocalWorkshopPlan));
            check(WritePatch(*selected,true) && memcmp(at,OriginalBytes(*selected),5)==0);
        }
    }
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%s %u workshop rotation checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
#else
    std::puts("SKIP workshop rotation bridge (native network pump not built)");
    return 0;
#endif
}
