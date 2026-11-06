#define S7_MAP_TEMPLATE_TESTING
#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}

#ifdef S7_LOCAL_NETWORK_PUMP
namespace {
alignas(16) unsigned char original_fx[512]{}, expected_fx[512]{}, actual_fx[512]{};
uint32_t actual_gpr[8]{}, expected_flags=0, actual_flags=0;
uintptr_t proxy_address=0, saved_stack=0, returned_stack=0, returned_name=0;
uint32_t canary=0;
__declspec(naked) void ExerciseTemplateGetter() {
    __asm {
        pushfd
        pushad
        mov saved_stack, esp
        fxsave original_fx
        fld1
        push 0xa55aa55a
        mov eax, 0x11111111
        mov ecx, proxy_address
        mov edx, 0x33333333
        mov ebx, 0x44444444
        mov ebp, 0x55555555
        mov esi, 0x66666666
        mov edi, 0x77777777
        stc
        pushfd
        pop expected_flags
        fxsave expected_fx
        call SnapshotLocalTemplateName
        mov returned_name, eax
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
    auto check=[&](bool okay,int line) {++checks;if(!okay){++failures;std::printf("Failed map-template check %u line %d\n",checks,line);}};
#define check(x) check((x),__LINE__)
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1106000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    check(image!=nullptr);if(!image)return 1;
    image_base=reinterpret_cast<uintptr_t>(image);
    alignas(16) unsigned char proxy[0x108]{},network[0xa88]{},owner[0x300]{},facade[0x40]{};
    alignas(16) unsigned char handler[0x40]{},implementation[0x40]{},session[0x240]{};
    char heap[1026]{};
    auto pointer=[](void* at,const void* value) {*static_cast<uintptr_t*>(at)=reinterpret_cast<uintptr_t>(value);};
    auto word=[](void* at,uintptr_t value) {*static_cast<uintptr_t*>(at)=value;};
    proxy_address=reinterpret_cast<uintptr_t>(proxy);
    auto reset=[&]() {
        memset(proxy,0,sizeof(proxy));memset(network,0,sizeof(network));memset(owner,0,sizeof(owner));
        memset(facade,0,sizeof(facade));memset(handler,0,sizeof(handler));memset(implementation,0,sizeof(implementation));
        memset(session,0xa5,sizeof(session));memset(heap,0,sizeof(heap));
        initialization=2;local_connect_probe=true;bootstrap_thread=GetCurrentThreadId();map_template_test_clobber=false;
        *Native<void**>(0x1379E10)=proxy;*Native<void**>(0x1379E14)=network;
        pointer(network+0xa18,owner);word(owner,NativeVA(0x10919BC));word(owner+0x50,NativeVA(0x109C9D8));
        pointer(owner+0xa4,facade);owner[0x88]=1;
        pointer(network+0xa4c,handler);word(handler,NativeVA(0x113FE90));pointer(handler+0x1c,implementation);
        pointer(implementation+0x10,session);word(session,NativeVA(0x1140150));word(session+0x1ec,NativeVA(0x1086D3C));
    };
    auto set_name=[&](const std::string& text) {
        auto& name=*reinterpret_cast<NativeSmallString*>(session+0x1f8);name={};
        name.length=static_cast<uint32_t>(text.size());name.capacity=text.size()<16?15:static_cast<uint32_t>(text.size());
        if(text.size()<16)memcpy(name.inline_data,text.c_str(),text.size()+1);
        else {memcpy(heap,text.c_str(),text.size()+1);pointer(name.inline_data,heap);}
    };
    for(bool steam:{false,true}) {
        steam_history=steam;
        for(size_t length:{size_t(0),size_t(1),size_t(3),size_t(14),size_t(15),size_t(16),size_t(17),size_t(31),size_t(255),size_t(256),size_t(1024)}) {
            reset();const std::string text(length,'x');set_name(text);
            unsigned char before[sizeof(session)];memcpy(before,session,sizeof(session));
            SetLastError(3456);const auto* result=SnapshotLocalTemplateName(proxy,nullptr);
            check(GetLastError()==3456);check(memcmp(before,session,sizeof(session))==0);
            if(!length) {check(result==nullptr);continue;}
            check(result && strcmp(result,text.c_str())==0);
            auto& name=*reinterpret_cast<NativeSmallString*>(session+0x1f8);
            check(result!=SmallStringData(name));
            // Destroy/poison both native storage forms before the caller copies.
            memset(name.inline_data,0xcc,sizeof(name.inline_data));memset(heap,0xdd,sizeof(heap));
            volatile char stack_poison[2048];for(size_t i=0;i<sizeof(stack_poison);++i)stack_poison[i]=static_cast<char>(i);
            check(stack_poison[1]==1 && strcmp(result,text.c_str())==0);
        }
        reset();set_name("\xc3\x9c" "ber Mesa +3");
        check(strcmp(SnapshotLocalTemplateName(proxy,nullptr),"\xc3\x9c" "ber Mesa +3")==0);
        for(unsigned invalid=0;invalid<21;++invalid) {
            reset();set_name("WePlay QA Max25");
            auto& name=*reinterpret_cast<NativeSmallString*>(session+0x1f8);
            void* self=proxy;
            if(invalid==0)initialization=1;
            if(invalid==1)local_connect_probe=false;
            if(invalid==2)bootstrap_thread=GetCurrentThreadId()+1;
            if(invalid==3)self=nullptr;
            if(invalid==4)self=proxy+4;
            if(invalid==5)*Native<void**>(0x1379E14)=nullptr;
            if(invalid==6)pointer(network+0xa18,nullptr);
            if(invalid==7)word(owner,0);
            if(invalid==8)word(owner+0x50,0);
            if(invalid==9)owner[0x88]=0;
            if(invalid==10)owner[0x98]=1;
            if(invalid==11)pointer(facade+0xc,facade);
            if(invalid==12)word(handler,0);
            if(invalid==13)pointer(handler+0x1c,nullptr);
            if(invalid==14)pointer(implementation+0x10,nullptr);
            if(invalid==15)word(session,0);
            if(invalid==16)word(session+0x1ec,0);
            if(invalid==17)name.length=1025;
            if(invalid==18)name.capacity=14;
            if(invalid==19)name.inline_data[15]='x';
            if(invalid==20)name.inline_data[3]=0;
            SetLastError(3456);check(SnapshotLocalTemplateName(self,nullptr)==nullptr);check(GetLastError()==3456);
        }
        reset();set_name(std::string(16,'y'));
        auto& bad_name=*reinterpret_cast<NativeSmallString*>(session+0x1f8);
        bad_name.capacity=0x100001;check(SnapshotLocalTemplateName(proxy,nullptr)==nullptr);
        bad_name.capacity=16;pointer(bad_name.inline_data,reinterpret_cast<void*>(1));
        SetLastError(3456);check(SnapshotLocalTemplateName(proxy,nullptr)==nullptr && GetLastError()==3456);
        for(bool clobber:{false,true}) {
            reset();set_name("WePlay QA Max25");map_template_test_clobber=clobber;
            SetLastError(3456);ExerciseTemplateGetter();
            check(returned_stack+4==saved_stack && canary==0xa55aa55a);
            check(actual_flags==expected_flags && GetLastError()==3456);
            check(actual_gpr[0]==0x77777777 && actual_gpr[1]==0x66666666 && actual_gpr[2]==0x55555555);
            check(actual_gpr[4]==0x44444444 && actual_gpr[5]==0x33333333 && actual_gpr[6]==proxy_address);
            check(returned_name && strcmp(reinterpret_cast<const char*>(returned_name),"WePlay QA Max25")==0);
            check(memcmp(expected_fx,actual_fx,16)==0 && memcmp(expected_fx+24,actual_fx+24,8)==0);
            check(memcmp(expected_fx+32,actual_fx+32,384)==0);
        }
        map_template_test_clobber=false;
        const Patch* getter=nullptr;for(const auto& patch:ConnectPatches)if(patch.va==0xED7B97)getter=&patch;
        check(getter!=nullptr);
        if(getter) {
            auto* at=PatchAddress(*getter);memcpy(at,OriginalBytes(*getter),5);
            check(WritePatch(*getter,false));check(*at==0xe8);
            int32_t displacement=0;memcpy(&displacement,at+1,4);
            check(reinterpret_cast<uintptr_t>(at+5)+displacement==reinterpret_cast<uintptr_t>(&SnapshotLocalTemplateName));
            check(WritePatch(*getter,true));check(memcmp(at,OriginalBytes(*getter),5)==0);
        }
    }
    initialization=0;bootstrap_thread=0;
    check(VirtualFree(image,0,MEM_RELEASE)!=FALSE);
    std::printf("%u map-template lifetime/ABI/guard checks, %u failures\n",checks,failures);
    return failures?1:0;
#else
    std::puts("SKIP map-template getter requires the network pump");return 0;
#endif
}
