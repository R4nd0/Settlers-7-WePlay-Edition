#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}
int main() {
    unsigned checks=0,failures=0;auto check=[&](bool v){++checks;if(!v)++failures;};
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1106000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!image)return 2;
    image_base=reinterpret_cast<uintptr_t>(image);steam_history=false;
    initialization=2;local_connect_probe=true;lab_priority_audit_enabled=true;bootstrap_thread=GetCurrentThreadId();
    uint32_t replay[16]{},logic[0x160/4]{},main[4]{},priority[0x1140/4]{},transfers[0x160/4]{};
    uint32_t req_a[0xdc/4]{},req_b[0xdc/4]{},root_a[0x48/4]{},root_b[0x48/4]{};
    uint32_t rec_a[8]{},rec_b[8]{},tasks[8]{},records[4]{},order[8]={0,1,2,3,4,5,6,7};
    auto address=[](const void* p){return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));};
    auto put=[](void* p,uint32_t v){memcpy(p,&v,4);};
    put(image+0x13a5200-0x400000,address(replay));put(image+0x13a5220-0x400000,address(logic));
    replay[0]=NativeVA(0x1133f40);replay[11]=100;
    logic[0]=NativeVA(0x1141534);logic[0x14c/4]=address(main);
    main[0]=NativeVA(0x1147f40);main[1]=address(priority);main[2]=address(transfers);
    priority[0]=NativeVA(0x1148328);priority[0x1134/4]=address(tasks);priority[0x1138/4]=address(tasks+8);
    transfers[0x148/4]=address(records);transfers[0x14c/4]=address(records+4);
    auto task=[&](uint32_t* p,uint32_t id,uint32_t birth,uint32_t rank,uint32_t group,bool request){
        p[0]=NativeVA(request?0x1148a68:0x1148b6c);p[1]=3;p[2]=id;p[0x14/4]=rank;p[0x2c/4]=birth;p[0x34/4]=4;p[0x1c/4]=group;p[0x20/4]=group;
        const auto i=id&65535u;tasks[i*2]=id;tasks[i*2+1]=address(p);
    };
    task(req_a,0x10000,100,50,9,true);task(req_b,0x10001,200,1,9,true);
    task(root_a,0x10002,90,49,4,false);task(root_b,0x10003,190,0,4,false);
    req_a[0x24/4]=root_a[2];req_a[0x38/4]=address(root_a);req_b[0x24/4]=root_b[2];req_b[0x38/4]=address(root_b);
    rec_a[0]=0x20000;rec_a[2]=req_a[2];rec_b[0]=0x20001;rec_b[2]=req_b[2];
    records[0]=rec_a[0];records[1]=address(rec_a);records[2]=rec_b[0];records[3]=address(rec_b);
    const auto vec=address(priority)+8+4*0xac;put(reinterpret_cast<void*>(vec),address(order));put(reinterpret_cast<void*>(vec+4),address(order+8));
    neo_fifo_enabled=true;neo_fifo_groups=false;
    check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==0); // unchanged warmup
    replay[11]=60000;SetLastError(4444);
    check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==1);check(GetLastError()==4444);
    check(CompareNeoFifoTransfers(address(rec_b),address(rec_a))==0);
    uint32_t head[3]{},node_a[3]{},node_b[3]{};
    head[0]=address(node_a);head[1]=address(node_b);node_a[0]=address(node_b);node_a[1]=address(head);node_a[2]=address(root_a);node_b[0]=address(head);node_b[1]=address(node_a);node_b[2]=address(root_b);
    const auto list=address(priority)+4*0xac+4*8;put(reinterpret_cast<void*>(list+0x1c),address(head));put(reinterpret_cast<void*>(list+0x20),2);
    uint32_t frame[12]{};frame[6]=address(priority);frame[9]=NativeVA(0x7bbb9c);frame[10]=root_b[2];frame[11]=root_a[2];
    TrackNeoFifoIntent(frame,0);check(neo_fifo_marks[3].intent==neo_fifo::Intent::Prefer);
    check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==0); // human priority preserved
    frame[9]=NativeVA(0x9e77f6);frame[11]=0;TrackNeoFifoIntent(frame,0);
    check(neo_fifo_marks[3].intent==neo_fifo::Intent::Defer);check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==1);
    frame[9]=NativeVA(0x78164a);frame[11]=root_a[2];TrackNeoFifoIntent(frame,0);
    check(neo_fifo_marks[3].intent==neo_fifo::Intent::Defer); // automatic default does not become explicit
    root_b[2]=0x20003;tasks[6]=root_b[2];req_b[0x24/4]=root_b[2];
    check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==1); // stale generation cannot retain preference
    root_b[0x1c/4]=root_b[0x20/4]=1;
    uint32_t changed[8]={1,0,2,3,4,5,6,7},newvec[3]={address(changed),address(changed+8),address(changed+8)};
    frame[9]=NativeVA(0xa13f39);frame[10]=address(newvec);frame[11]=0x3f000004;
    TrackNeoFifoIntent(frame,1);check(neo_fifo_group_deliberate[4]);memcpy(order,changed,32);
    neo_fifo_groups=true;check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==0); // AI group priority preserved
    neo_fifo_groups=false;check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==1); // requested experiment without group priorities
    neo_fifo_enabled=false;check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==0);
    neo_fifo_enabled=true;bootstrap_thread=GetCurrentThreadId()+1;check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==0);
    bootstrap_thread=GetCurrentThreadId();neo_fifo_priority=0;check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==0); // late loaded world excluded
    check(!neo_fifo_fresh);
    neo_fifo_priority=0;replay[11]=100;CompareNeoFifoTransfers(address(rec_a),address(rec_b));replay[11]=60000;
    records[0]^=0x10000;check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==1); // unsupported reservation protected/native rank
    records[0]^=0x10000;req_a[0x24/4]=0x90002;check(CompareNeoFifoTransfers(address(rec_a),address(rec_b))==1); // incomplete lineage protected/native rank
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%s %u FIFO integration checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
