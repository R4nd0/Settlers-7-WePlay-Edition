#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}
namespace {
bool test_host=false;int test_session=1;unsigned original_visits=0;
uintptr_t tail_left=0,tail_right=0,tail_eax=0;
unsigned group_calls=0;uintptr_t group_self=0;uint32_t group_array=1,group_player=0;
void __fastcall OriginalGroup(void* self,void*,uint32_t array,uint32_t player) {
    ++group_calls;group_self=reinterpret_cast<uintptr_t>(self);group_array=array;group_player=player;SetLastError(7654);
}
__declspec(naked) void OriginalCompareTail() {
    __asm { mov tail_left,ecx
        mov tail_right,edx
        mov tail_eax,eax
        mov al,1
        ret }
}
struct TestStream { std::vector<uint32_t> words;size_t at=0;bool writing=true;int32_t result=0; };
bool __fastcall WriteWord(void* raw,void*,uint32_t v) {static_cast<TestStream*>(raw)->words.push_back(v);return true;}
bool __fastcall ReadWord(void* raw,void*,uint32_t* v) {
    auto& s=*static_cast<TestStream*>(raw);if(s.at==s.words.size())return false;*v=s.words[s.at++];return true;
}
void __fastcall VisitField(void* raw,void*,void* visitor) {
    auto* field=static_cast<NeoWireField*>(raw);auto& s=*static_cast<TestStream*>(visitor);NeoWireResult result{};
    using Callback=NeoWireResult* (__fastcall*)(NeoWireField*,void*,NeoWireResult*,void*);
    reinterpret_cast<Callback>(field->type[s.writing?4:5])(field,nullptr,&result,&s);s.result=result.error;
}
void __fastcall VisitOriginal(void*,void*,void*) {++original_visits;SetLastError(5678);}
void* __fastcall ConstructOriginal(void* self,void*) {return self;}
void* __cdecl GetSession() {return &test_session;}
bool __fastcall IsHost(void*,void*) {return test_host;}
void Stub(uintptr_t at,const void* target) {
    auto* p=reinterpret_cast<unsigned char*>(at);p[0]=0xe9;
    const auto displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-(at+5));memcpy(p+1,&displacement,4);
}
void CallGroup(uintptr_t returned,uintptr_t self) {
    auto* code=reinterpret_cast<unsigned char*>(returned-14);
    code[0]=0x6a;code[1]=4;code[2]=0x6a;code[3]=0;code[4]=0xb9;memcpy(code+5,&self,4);
    code[9]=0xe8;const auto offset=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&NeoGroupBridge)-returned);
    memcpy(code+10,&offset,4);code[14]=0xc3;
    reinterpret_cast<void (__cdecl*)()>(code)();
}
}
int main() {
    unsigned checks=0,failures=0;auto verify=[&](bool v,int line){++checks;if(!v){++failures;std::printf("FAIL line %d\n",line);}};
#define check(...) verify((__VA_ARGS__),__LINE__)
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1106000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    image_base=reinterpret_cast<uintptr_t>(image);initialization=2;local_connect_probe=true;bootstrap_thread=GetCurrentThreadId();neo_map_installed=true;
    auto addr=[](const void* p){return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));};
    auto put=[](uintptr_t p,uint32_t v){memcpy(reinterpret_cast<void*>(p),&v,4);};
    for(bool steam:{false,true}) {
        steam_history=steam;
        Stub(NativeVA(0x6679e0),&WriteWord);Stub(NativeVA(0x667a10),&ReadWord);Stub(NativeVA(0xcb16d0),&VisitField);
        Stub(NativeVA(0x668060),&VisitOriginal);Stub(NativeVA(0x668ac0),&ConstructOriginal);
        Stub(NativeVA(0xed2b30),&GetSession);Stub(NativeVA(0xed2af0),&IsHost);
        uint32_t replay[16]{},logic[0x160/4]{},main[4]{},priority[0x1140/4]{},transfers[0x160/4]{};
        uint32_t req_a[0xdc/4]{},req_b[0xdc/4]{},root_a[0x48/4]{},root_b[0x48/4]{},moved[0x48/4]{};
        uint32_t records[4]{},tasks[8]{},rec_a[8]{},rec_b[8]{},manager[8]{},network_state[12]{};
        put(NativeVA(0x13a5200),addr(replay));put(NativeVA(0x13a5220),addr(logic));put(NativeVA(0x13a5210),addr(manager));
        manager[0x14/4]=addr(network_state);network_state[0x28/4]=1;
        replay[0]=NativeVA(0x1133f40);logic[0]=NativeVA(0x1141534);logic[0x14c/4]=addr(main);
        main[0]=NativeVA(0x1147f40);main[1]=addr(priority);main[2]=addr(transfers);
        priority[0]=NativeVA(0x1148328);priority[0x1134/4]=addr(tasks);priority[0x1138/4]=addr(tasks+8);
        transfers[0x148/4]=addr(records);transfers[0x14c/4]=addr(records+4);
        auto task=[&](uint32_t* p,uint32_t id,uint32_t birth,uint32_t rank,bool request) {
            p[0]=NativeVA(request?0x1148a68:0x1148b6c);p[1]=3;p[2]=id;p[0x14/4]=rank;p[0x2c/4]=birth;p[0x34/4]=4;p[0x1c/4]=p[0x20/4]=4;
            tasks[(id&65535u)*2]=id;tasks[(id&65535u)*2+1]=addr(p);
        };
        task(req_a,0x10000,10,50,true);task(req_b,0x10001,20,1,true);
        task(root_a,0x10002,1,49,false);task(root_b,0x10003,2,0,false);
        req_a[0x24/4]=root_a[2];req_a[0x38/4]=addr(root_a);req_b[0x24/4]=root_b[2];req_b[0x38/4]=addr(root_b);
        rec_a[0]=0x20000;rec_a[2]=req_a[2];rec_b[0]=0x20001;rec_b[2]=req_b[2];
        records[0]=rec_a[0];records[1]=addr(rec_a);records[2]=rec_b[0];records[3]=addr(rec_b);
        auto command=[&](const std::string& text,std::string* value=nullptr) {
            std::string out;bool is_string=false;const auto result=NeoLogisticsExecuteGuarded(text,out,is_string);if(value)*value=out;return result;
        };
        command("end");check(command("begin:0"));check(command("ready"));replay[11]=100;
        check(CompareNeoFifoTransfers(addr(rec_a),addr(rec_b))==2); // original native comparator for opt-out
        check(!command("begin:1")); // no mid-match toggle
        replay[11]=0;check(command("begin:1"));check(command("ready"));replay[11]=100;
        check(CompareNeoFifoTransfers(addr(rec_a),addr(rec_b))==1); // no laboratory warmup
        uint32_t head[3]{},na[3]{},nb[3]{};
        head[0]=addr(na);head[1]=addr(nb);na[0]=addr(nb);na[1]=addr(head);na[2]=addr(root_a);nb[0]=addr(head);nb[1]=addr(na);nb[2]=addr(root_b);
        const auto list=addr(priority)+4*0xac+4*8;put(list+0x1c,addr(head));put(list+0x20,2);
        uint32_t frame[12]{};frame[6]=addr(priority);frame[9]=image_base+(steam?0x7bb96c:0x7bbb9c)-0x400000;frame[10]=root_b[2];frame[11]=root_a[2];
        ObserveNeoLogisticsPriority(frame,0);check(neo_fifo_marks[3].intent==neo_fifo::Intent::Prefer);
        check(CompareNeoFifoTransfers(addr(rec_a),addr(rec_b))==0);
        std::string snapshot;check(command("snapshot",&snapshot));check(snapshot.find(std::to_string(addr(root_b)))==std::string::npos);
        std::string spSaved;check(command("save-snapshot:99",&spSaved) && spSaved==snapshot);check(!command("save-snapshot:100"));
        memcpy(moved,root_b,sizeof(moved));tasks[7]=addr(moved);req_b[0x38/4]=addr(moved);
        check(command("load"));check(!neo_fifo_enabled);check(command("restore:"+snapshot));
        check(neo_fifo_marks[3].object==addr(moved));check(CompareNeoFifoTransfers(addr(rec_a),addr(rec_b))==0);
        check(command("load"));moved[0x2c/4]++;check(!command("restore:"+snapshot));check(!neo_fifo_enabled&&neo_map_loading);
        moved[0x2c/4]--;check(command("restore:"+snapshot));
        check(command("load"));check(command("legacy"));check(!neo_fifo_enabled);
        network_state[0x28/4]=0;check(command("stage:1"));check(!neo_fifo_enabled); // menu staging is independent of stale world clocks
#ifdef S7_LOCAL_PAIR_LAB
        std::string instance;
        local_pair_bind=0x7f000002u;check(command("probe-instance",&instance)&&instance=="A");
        local_pair_bind=0x7f000003u;check(command("probe-instance",&instance)&&instance=="B");
        local_pair_bind=0;check(!command("probe-instance",&instance));
#endif
        std::string menu_choice;check(!command("selection",&menu_choice));check(!command("begin:1"));
        network_state[0x28/4]=1;replay[11]=0;
        check(command("selection",&menu_choice)&&menu_choice=="1");check(command("begin:1"));check(command("ready"));check(neo_fifo_enabled);
        command("end");check(command("stage:1"));
        network_state[0x28/4]=2;replay[11]=0;test_host=false;neo_wire_received=2;
        check(!command("begin:1")); // no unsynchronized guest activation
        uint32_t message[4]{};
        check(!command("start-event")); // unknown lobby choice cannot lock
        TestStream wire;VisitNeoWire(&wire,true);
        check(wire.words==std::vector<uint32_t>({NeoWireVersion,1,0}) && wire.result==1);
        wire.writing=false;wire.at=0;VisitNeoWire(&wire,true);
        check(neo_wire_received==1 && wire.result==1 && !neo_wire_confirmed);
        check(!command("begin:1")); // lobby receipt alone is not a start event
        check(command("start-event") && neo_wire_confirmed && neo_wire_start_choice==1);
        std::string state;check(command("network-state",&state)&&state=="1");
        check(command("begin:1"));check(command("ready"));check(neo_fifo_enabled);
        TestStream invalid{{NeoWireVersion,2},0,false};VisitNeoWire(&invalid,true);
        check(invalid.result<0 && neo_wire_received==2 && !neo_wire_confirmed && neo_wire_start_choice==2);
        TestStream missing{{},0,false};VisitNeoWire(&missing,false);check(missing.result<0);
        TestStream wrong{{0x314f454e},0,false};VisitNeoWire(&wrong,false);check(wrong.result<0);
        TestStream compatible{{NeoWireVersion},0,false};VisitNeoWire(&compatible,false);check(compatible.result==1);
        TestStream previous{{0x324f454e},0,false};VisitNeoWire(&previous,false);check(previous.result<0);
        neo_wire_received=1;check(ConstructNeoJoin(message,nullptr)==message && neo_wire_received==2);
        test_host=true;check(command("network-state",&state)&&state=="1");
        check(!command("begin:1"));check(command("start-event"));check(command("begin:1"));
        check(command("stage:0"));check(!command("begin:0")); // cannot change the locked start choice
        command("end");check(command("stage:0"));check(command("start-event"));
        check(command("begin:0"));check(command("ready"));check(!neo_fifo_enabled);
        command("end");test_host=false;check(!command("begin:0"));
        TestStream off{{NeoWireVersion,0,0},0,false};VisitNeoWire(&off,true);
        check(!command("begin:0"));check(command("start-event"));check(command("begin:0"));check(command("ready"));
        network_state[0x28/4]=1;command("end");
        // Resume carries the complete immutable state, not only its enabled bit.
        network_state[0x28/4]=2;test_host=true;check(command("stage:1"));
        neo_wire_local_state=snapshot;TestStream resumeWire;VisitNeoWire(&resumeWire,true);
        check(resumeWire.result==1 && resumeWire.words[2]==snapshot.size());
        test_host=false;ResetNeoWireReceived();neo_wire_received=2;neo_wire_confirmed=false;
        resumeWire.writing=false;VisitNeoWire(&resumeWire,true);
        check(resumeWire.result==1 && neo_wire_received_state==snapshot);
        check(command("start-event"));check(command("load"));replay[11]=100;
        check(command("restore-network"));check(neo_fifo_marks[3].intent==neo_fifo::Intent::Prefer && neo_fifo_marks[3].object==addr(moved));
        std::string savedClock;replay[11]=101;check(command("save-snapshot:100",&savedClock));
        neo_logistics::SavedState decoded;check(neo_logistics::Decode(savedClock,decoded) && decoded.turn==101 && decoded.logic_turn==100);
        check(!command("save-snapshot:99"));check(!command("save-snapshot:102"));
        check(command("load"));replay[11]=100;check(!command("restore:"+savedClock));replay[11]=101;check(command("restore:"+savedClock));
        // Native tasks may already belong to the next replay turn. Keep them.
        moved[0x2c/4]=101;neo_fifo_marks[3]={root_b[2],101,addr(moved),neo_fifo::Intent::Defer};
        check(command("save-snapshot:100",&savedClock));check(neo_logistics::Decode(savedClock,decoded) && decoded.turn==101 && decoded.marks[0].created==101);
        check(command("load"));check(command("restore:"+savedClock));check(neo_fifo_marks[3].intent==neo_fifo::Intent::Defer);
        moved[0x2c/4]=2;command("end");
        TestStream excessive{{NeoWireVersion,1,static_cast<uint32_t>(neo_logistics::MaximumStateBytes+1)},0,false};
        VisitNeoWire(&excessive,true);check(excessive.result<0 && neo_wire_received==2);
        // Resolve only numbered MP saves inside the already active native root.
        uint32_t fileRoot[0x174/4]{};const char folder[]="C:\\owned\\Savegames";
        put(NativeVA(0x13789a8),addr(fileRoot));fileRoot[0]=NativeVA(0x11337c4);
        fileRoot[0x74/4]=NativeVA(0x113652c);fileRoot[0x7c/4]=addr(folder);fileRoot[0x84/4]=sizeof(folder)-1;
        std::string path;check(NeoSavePath("s1002.s7s",path) && path=="C:\\owned\\Savegames\\s1002.s7s");
        check(NeoSavePath("s1002",path) && path=="C:\\owned\\Savegames\\s1002.s7s");
        check(!NeoSavePath("../s1002.s7s",path));check(!NeoSavePath("s1000.s7s",path));check(!NeoSavePath("Options.ini",path));
        fileRoot[0x74/4]=0;check(!NeoSavePath("s1002.s7s",path));put(NativeVA(0x13789a8),0);
        network_state[0x28/4]=1;command("end");
        const unsigned char task_bytes[][10]={{0x55,0x8b,0xec,0x6a,0xff,0x68,0x70,0x15,0xf9,0},{0x55,0x8b,0xec,0x6a,0xff,0x68,0xe0,0x07,0xf9,0}};
        memcpy(Native<void*>(0x782ee0),task_bytes[steam?1:0],10);
        memcpy(Native<void*>(0x782280),steam?NeoMapGroupSteamGuard:NeoMapGroupUbiGuard,10);
        memcpy(Native<void*>(0x53f160),NeoMapClockGuard,sizeof(NeoMapClockGuard));
        const unsigned char tail[]={0x56,0x57,0x8b,0xfa,0x8b,0x80,0x4c,0x01,0x00,0x00};
        memcpy(reinterpret_cast<void*>(NativeVA(0x78f380)+5),tail,sizeof(tail));check(InitializeNeoMapPatches());
        *reinterpret_cast<unsigned char*>(NativeVA(0x78f380)+5)=0;check(!InitializeNeoMapPatches());
        Stub(neo_compare_resume,&OriginalCompareTail);
        using Compare=uint32_t (__fastcall*)(uintptr_t,uintptr_t);
        const auto comparator=reinterpret_cast<Compare>(&CompareNeoFifoTransfersBridge);
        SetLastError(3456);check((comparator(addr(rec_a),addr(rec_b))&255u)==1);
        check(tail_left==addr(rec_a) && tail_right==addr(rec_b) && tail_eax==addr(logic) && GetLastError()==3456);
        auto* group_tail=reinterpret_cast<unsigned char*>(neo_group_resume);
        group_tail[0]=0x8b;group_tail[1]=0xe5;group_tail[2]=0x5d;Stub(neo_group_resume+3,&OriginalGroup);
        replay[11]=0;command("stage:1");check(command("begin:1"));check(command("ready"));
        group_calls=0;SetLastError(4567);
        CallGroup(image_base+(steam?0xa133b9:0xa13f39)-0x400000,addr(priority));
        check(group_calls==0 && GetLastError()==4567);
        CallGroup(image_base+(steam?0x7bc36f:0x7bc59f)-0x400000,addr(priority));check(group_calls==0);
        CallGroup(image_base+0x100020,addr(priority));check(group_calls==1); // default/native initialization is retained
        check(group_self==addr(priority) && !group_array && group_player==4 && GetLastError()==7654);
        command("end");CallGroup(image_base+(steam?0xa133b9:0xa13f39)-0x400000,addr(priority));check(group_calls==2);
        check(!neo_fifo_intent_errors&&!neo_fifo_compare_errors);
    }
    VirtualFree(image,0,MEM_RELEASE);std::printf("%s %u NEO map-option checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
