#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}
int main() {
    unsigned checks=0, failures=0;
    auto check=[&](bool x){++checks;if(!x)++failures;};
    auto* mem=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1106000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!mem)return 2;
    image_base=reinterpret_cast<uintptr_t>(mem);
    uint32_t self[0x1140/4]{}, replay[16]{}, task[32]{}, relative[32]{};
    uint32_t table[]={0x10000,reinterpret_cast<uintptr_t>(task),0x10001,reinterpret_cast<uintptr_t>(relative)};
    const auto put=[](void* p,uint32_t v){memcpy(p,&v,4);};
    put(mem+0x13a5200-0x400000,reinterpret_cast<uintptr_t>(replay));replay[0]=NativeVA(0x1133f40);replay[0x2c/4]=123;
    self[0]=NativeVA(0x1148328);self[0x1134/4]=reinterpret_cast<uintptr_t>(table);self[0x1138/4]=reinterpret_cast<uintptr_t>(table+4);
    task[0x34/4]=4;task[0x2c/4]=100;task[0x1c/4]=4;task[0xc/4]=20;task[0x14/4]=81;task[0x24/4]=77;
    relative[0x2c/4]=110;relative[0x1c/4]=1;relative[0x14/4]=15;
    uint32_t frame[12]{};frame[6]=reinterpret_cast<uintptr_t>(self);frame[9]=static_cast<uint32_t>(image_base+0xa145c9-0x400000);frame[10]=0x10000;frame[11]=0x10001;
    initialization=2;local_connect_probe=true;bootstrap_thread=GetCurrentThreadId();
    SetLastError(4444);ObserveLabPriorityEntry(frame,0);
    check(GetLastError()==4444 && lab_lua_console_count==1 && lab_priority_sequence==1);
    check(strcmp(lab_lua_console_records[0].bytes,"NEO1|priority-task|123|1|00a145c9|65536|65537|4|100|4|20|81|77|110|1|15")==0);
    for(int guard=0;guard<4;++guard){
        initialization=guard==0?1:2;local_connect_probe=guard!=1;bootstrap_thread=guard==2?0:GetCurrentThreadId();steam_history=guard==3;
        ObserveLabPriorityEntry(frame,0);check(lab_lua_console_count==1 && lab_priority_sequence==1);
    }
    initialization=2;local_connect_probe=true;bootstrap_thread=GetCurrentThreadId();steam_history=false;
    frame[10]=0x20000;ObserveLabPriorityEntry(frame,0);check(lab_lua_console_count==2);
    check(strstr(lab_lua_console_records[1].bytes,"|131072|65537|255|0|255|0|0|0|")!=nullptr);
    frame[6]=0;SetLastError(4444);ObserveLabPriorityEntry(frame,0);check(lab_lua_console_count==2 && GetLastError()==4444);
    frame[6]=reinterpret_cast<uintptr_t>(self);
    uint32_t old_order[]={7,6,0,1,2,3,4,5}, new_order[]={1,0,2,3,4,5,6,7};
    uint32_t nextvec[]={reinterpret_cast<uintptr_t>(new_order),reinterpret_cast<uintptr_t>(new_order+8),reinterpret_cast<uintptr_t>(new_order+8)};
    uint32_t priorvec[]={reinterpret_cast<uintptr_t>(old_order),reinterpret_cast<uintptr_t>(old_order+8),reinterpret_cast<uintptr_t>(old_order+8)};
    memcpy(reinterpret_cast<unsigned char*>(self)+8+4*0xac,priorvec,12);
    frame[10]=reinterpret_cast<uintptr_t>(nextvec);frame[11]=0x3f7a1204;ObserveLabPriorityEntry(frame,1);
    check(lab_lua_console_count==3);
    check(strstr(lab_lua_console_records[2].bytes,"|4|1|1,0,2,3,4,5,6,7|7,6,0,1,2,3,4,5")!=nullptr);
    check(memcmp(reinterpret_cast<unsigned char*>(self)+8+4*0xac,priorvec,12)==0);
    lab_lua_console_count=_countof(lab_lua_console_records);SetLastError(4444);ObserveLabPriorityEntry(frame,1);
    check(lab_lua_console_count==_countof(lab_lua_console_records) && GetLastError()==4444);
    VirtualFree(mem,0,MEM_RELEASE);
    std::printf("%s %u priority capture checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
