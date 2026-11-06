#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}
namespace {
unsigned query_calls=0,bad_profiles=0;
bool query_reachable=true;
float movement_profile[27]{};
const float* __fastcall ProfileFixture(void*,void*) { return movement_profile; }
uint32_t bridge_stage=0;
uint32_t xmm_input[4]={0x3f800000,0x12345678,0x87654321,0x40e00000},xmm_output[4]{};
uint32_t saved_bx=0,saved_si=0,saved_di=0,flags_before=0,flags_after=0,mx_before=0,mx_after=0;
unsigned short cw_before=0,cw_after=0;
int __cdecl RoadFixture(float* output,const float* profile,uint32_t start,uint32_t end,uint32_t player) {
    ++query_calls;
    for(unsigned i=0;i<27;++i)if(profile[i]!=1)++bad_profiles;
    if(player!=2)++bad_profiles;
    SetLastError(7777);
    if(!query_reachable)return 0;
    if(start==10 && end==20)*output=2;
    else if(start==20 && end==40)*output=98;
    else if(start==10 && end==30)*output=20;
    else if(start==30 && end==40)*output=30;
    else return 0;
    return 1;
}
__declspec(naked) void QueryBridge() {
    __asm {
        push ebp
        mov ebp,esp
        push dword ptr [ebp+16]
        push dword ptr [ebp+12]
        push dword ptr [ebp+8]
        push edx
        push ecx
        call RoadFixture
        add esp,20
        pop ebp
        ret
    }
}
__declspec(naked) void Accepted() {__asm {mov bridge_stage,1
    ret}}
__declspec(naked) void Rejected() {__asm {mov bridge_stage,0
    ret}}
__declspec(naked) void Original() {__asm {mov bridge_stage,2
    ret}}
__declspec(naked) void CallBridge(uintptr_t,uintptr_t) {
    __asm {
        push ebp
        mov eax,dword ptr [esp+8]
        push edi
        mov edi,dword ptr [esp+16]
        push ebx
        push esi
        mov ebx,0x87654321
        mov esi,0x12345678
        mov ebp,eax
        movups xmm2,xmmword ptr [xmm_input]
        stmxcsr mx_before
        fnstcw cw_before
        pushfd
        pop flags_before
        call NeoDistanceBridge
        pushfd
        pop flags_after
        movups xmmword ptr [xmm_output],xmm2
        stmxcsr mx_after
        fnstcw cw_after
        mov saved_bx,ebx
        mov saved_si,esi
        mov saved_di,edi
        pop esi
        pop ebx
        pop edi
        pop ebp
        ret
    }
}
}
int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool v){++checks;if(!v){++failures;std::printf("FAIL check %u\n",checks);}};
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1106000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    image_base=reinterpret_cast<uintptr_t>(image);
    auto address=[](const void* p){return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));};
    auto put=[](uintptr_t p,uint32_t v){memcpy(reinterpret_cast<void*>(p),&v,4);};
    for(bool steam:{false,true}) {
        steam_history=steam;initialization=2;local_connect_probe=true;bootstrap_thread=GetCurrentThreadId();
        uint32_t replay[16]{},logic[0x160/4]{},main[4]{},priority[0x1140/4]{},manager[16]{},slots[8]{};
        uint32_t a[0x300/4]{},b[0x300/4]{},c[0x300/4]{},d[0x300/4]{},carrier[16]{},frame_words[64]{};
        const auto frame=address(frame_words+32);
        put(NativeVA(0x13a5200),address(replay));put(NativeVA(0x13a5220),address(logic));
        replay[0]=NativeVA(0x1133f40);replay[11]=100;
        logic[0]=NativeVA(0x1141534);logic[0x14c/4]=address(main);logic[0x3c/4]=address(manager);
        main[0]=NativeVA(0x1147f40);main[1]=address(priority);priority[0]=NativeVA(0x1148328);
        manager[8]=address(slots);manager[9]=address(slots+8);
        uint32_t* objects[]={a,b,c,d};
        for(unsigned i=0;i<4;++i) {
            auto* obj=objects[i];obj[0]=NativeVA(0x1143264);obj[2]=2;obj[9]=(i+1)*10;obj[0x208/4]=0x10000+i;
            slots[i*2]=0x10000+i;slots[i*2+1]=address(obj);
        }
        carrier[2]=2;put(frame+8,address(a));put(frame+12,address(d));put(frame+16,address(carrier));
        auto* code=Native<unsigned char*>(0x787bb0);code[0]=0xe9;
        const uint32_t jump=address(&QueryBridge)-address(code+5);memcpy(code+1,&jump,4);
        auto* getter=Native<unsigned char*>(0x6f6c20);getter[0]=0xe9;
        const uint32_t getter_jump=address(&ProfileFixture)-address(getter+5);memcpy(getter+1,&getter_jump,4);
        for(auto& value:movement_profile)value=1;
        neo_fifo_priority=address(priority);neo_fifo_last_turn=100;neo_fifo_enabled=false;neo_fifo_fresh=false;
        neo_distance_original=address(&Original);neo_distance_accept=address(&Accepted);neo_distance_reject=address(&Rejected);
        check(ChooseNeoDistance(frame,address(c))==2 && !query_calls);
        neo_fifo_enabled=true;neo_fifo_fresh=true;put(frame-0x30,0);
        SetLastError(4567);check(ChooseNeoDistance(frame,address(c))==1);
        check(GetLastError()==4567 && query_calls==2 && !bad_profiles);
        for(auto& value:movement_profile) {
            value=0;const auto before=query_calls;
            check(ChooseNeoDistance(frame,address(c))==2 && query_calls==before);
            value=1;
        }
        put(frame-0x30,address(c));
        check(ChooseNeoDistance(frame,address(b))==0); // short first leg is a longer complete route
        put(frame-0x30,address(b));
        check(ChooseNeoDistance(frame,address(c))==1);
        query_reachable=false;check(ChooseNeoDistance(frame,address(c))==0);query_reachable=true;
        c[2]=3;check(ChooseNeoDistance(frame,address(c))==2);c[2]=2;
        slots[4]^=0x10000;check(ChooseNeoDistance(frame,address(c))==2);slots[4]^=0x10000;
        const auto owner=bootstrap_thread.load();bootstrap_thread=owner+1;
        check(ChooseNeoDistance(frame,address(c))==2);bootstrap_thread=owner;
        // Repeated calls exercise the real register/FXSAVE bridge and its stack ABI.
        for(unsigned i=0;i<100;++i) {
            SetLastError(1234);CallBridge(frame,address(c));check(bridge_stage==1 && GetLastError()==1234);
            check(saved_bx==0x87654321 && saved_si==0x12345678 && saved_di==address(c));
            check(!memcmp(xmm_input,xmm_output,sizeof(xmm_input)) && mx_before==mx_after && cw_before==cw_after && flags_before==flags_after);
            CallBridge(frame,address(b));check(bridge_stage==0);
            neo_fifo_enabled=false;CallBridge(frame,address(c));check(bridge_stage==2);neo_fifo_enabled=true;
        }
        check(!bad_profiles);query_calls=0;
    }
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%s %u native distance checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
