// Calls no game code. The separate pinned-image oracle executes the originals.
#include "crossplay_participant.cpp"
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace CrossplayNative;
// Exported only by this test executable, so the bounded x86 oracle can run
// the exact compiled production thunks together with pinned original code.
extern "C" __declspec(dllexport) void* __cdecl WePlayGoldAddedOracle(uintptr_t function,
    void* output,const PlayerData* source,uint32_t subtype,uint32_t connection,uint32_t local) {
    return GoldAdded(function,output,source,subtype,connection,local);
}
extern "C" __declspec(dllexport) void* __cdecl WePlayGoldConfirmedOracle(uintptr_t function,
    void* output,const PlayerData* source,uint32_t connection,uint32_t local,const void* castle,uint32_t extra) {
    return GoldConfirmed(function,output,source,connection,local,castle,extra);
}
extern "C" __declspec(dllexport) void* __cdecl WePlayHistoryOracle(uintptr_t function,
    void* output,const PlayerData* source,uint32_t subtype,uint32_t connection,uint32_t local,
    const void* castle,uint32_t extra,uint32_t confirmed) {
    return HistoryConstruct(function,output,source,subtype,connection,local,castle,extra,confirmed!=0);
}
extern "C" __declspec(dllexport) void __cdecl WePlayLocalSendOracle(uintptr_t function,void* transport,const void* event) {
    LocalSend(function,transport,event);
}
namespace {
unsigned checks=0;
void Check(bool value,const char* reason) {
    ++checks;
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1); }
}
struct Audit { uintptr_t source;uint32_t subtype,connection,local;uintptr_t castle;uint32_t extra; };
Audit audit{};
void* sent_transport=nullptr;const void* sent_event=nullptr;
void __fastcall LocalSendFixture(void* self,void*,const void* event,uint32_t mode,uint32_t reliable,uint32_t extra,uint32_t target) {
    sent_transport=self;sent_event=event;
    Check(!mode && !reliable && !extra && !target,"local delivery cannot request vendor/network send");
}
__declspec(naked) void* AddedFixture() {
    __asm {
        mov audit.source,esi
        mov audit.subtype,edx
        mov eax,[esp+8]
        mov audit.connection,eax
        mov eax,[esp+12]
        mov audit.local,eax
        mov eax,[esp+4]
        ret 12
    }
}
__declspec(naked) void* ConfirmedFixture() {
    __asm {
        mov audit.source,esi
        mov eax,[esp+8]
        mov audit.connection,eax
        mov eax,[esp+12]
        mov audit.local,eax
        mov eax,[esp+16]
        mov audit.castle,eax
        mov eax,[esp+20]
        mov audit.extra,eax
        mov eax,[esp+4]
        ret 20
    }
}
void* __fastcall HistoryAddedFixture(void* self,void*,uint32_t subtype,uint32_t connection,
    const PlayerData* source,uint32_t local) {
    audit={reinterpret_cast<uintptr_t>(source),subtype,connection,local,0,0};return self;
}
void* __fastcall HistoryConfirmedFixture(void* self,void*,uint32_t subtype,uint32_t connection,
    const PlayerData* source,uint32_t local,const void* castle,uint32_t extra) {
    audit={reinterpret_cast<uintptr_t>(source),subtype,connection,local,reinterpret_cast<uintptr_t>(castle),extra};return self;
}
void GoldAbi(bool confirmed) {
    PlayerData source{};unsigned char output[0xa0]{},castle[0x50]{};
    auto* source_pointer=&source;auto* output_pointer=output;auto* castle_pointer=castle;
    uintptr_t entry=reinterpret_cast<uintptr_t>(confirmed?ConfirmedFixture:AddedFixture);
    uintptr_t returned=0,before=0,after=0,ebx_after=0,esi_after=0,edi_after=0;
    if(confirmed) {
        __asm {
            pushad
            mov ebx,12345678h
            mov esi,34567890h
            mov edi,456789abh
            mov before,esp
            push 10203040h
            push castle_pointer
            push 1
            push 1234h
            push source_pointer
            push output_pointer
            push entry
            call GoldConfirmed
            add esp,28
            mov returned,eax
            mov after,esp
            mov ebx_after,ebx
            mov esi_after,esi
            mov edi_after,edi
            popad
        }
    } else {
        __asm {
            pushad
            mov ebx,12345678h
            mov esi,34567890h
            mov edi,456789abh
            mov before,esp
            push 1
            push 1234h
            push 44
            push source_pointer
            push output_pointer
            push entry
            call GoldAdded
            add esp,24
            mov returned,eax
            mov after,esp
            mov ebx_after,ebx
            mov esi_after,esi
            mov edi_after,edi
            popad
        }
    }
    Check(before==after,"Gold wrapper keeps caller stack");
    Check(ebx_after==0x12345678 && esi_after==0x34567890 && edi_after==0x456789ab,"Gold wrapper keeps nonvolatiles");
    Check(returned==reinterpret_cast<uintptr_t>(output),"Gold native return pointer");
    Check(audit.source==reinterpret_cast<uintptr_t>(&source) && audit.connection==0x1234 && audit.local==1,"Gold ESI and stack arguments");
    if(confirmed) Check(audit.castle==reinterpret_cast<uintptr_t>(castle) && audit.extra==0x10203040,"Gold castle/extra arguments");
    else Check(audit.subtype==44,"Gold subtype in EDX");
}
void TransportGuards(const Profile& original) {
    auto profile=original;
    unsigned char send[10]={0x55,0x8b,0xec,0x83,0xec,0x18,0x89,0x4d,0xfc,0x8b};
    const unsigned char gold[]={0x55,0x8b,0xec,0x8b,0x45,0x0c,0x83,0xe8,0x00,0x56};
    if(profile.edition==Crossplay::Edition::Gold) memcpy(send,gold,10);
    uint32_t table[6]{},manager[24]{},main[5]{};CRITICAL_SECTION lock{};
    auto address=[](const void* value){return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));};
    uint32_t main_pointer=address(main);
    profile.main_pointer=address(&main_pointer);profile.main_vtable=123;
    profile.transport_vtable=address(table);profile.send=address(send);
    table[5]=profile.send;main[0]=profile.main_vtable;main[4]=address(manager);manager[0]=profile.transport_vtable;
    const unsigned lock_index=profile.edition==Crossplay::Edition::Gold?6:5;
    manager[lock_index]=address(&lock);
    Check(CurrentTransport(profile)==manager,"current native manager follows guarded root ownership");
    main_pointer=0;Check(!CurrentTransport(profile),"missing main excluded");main_pointer=address(main);
    main[0]^=1;Check(!CurrentTransport(profile),"foreign main class excluded");main[0]^=1;
    main[4]=0;Check(!CurrentTransport(profile),"missing transport excluded");main[4]=address(manager);
    manager[0]=0;Check(!CurrentTransport(profile),"foreign transport class excluded");manager[0]=profile.transport_vtable;
    manager[lock_index]=0;Check(!CurrentTransport(profile),"missing native queue lock excluded");manager[lock_index]=address(&lock);
    table[5]^=1;Check(!CurrentTransport(profile),"changed virtual send excluded");table[5]^=1;
    for(auto& byte:send) { byte^=1;Check(!CurrentTransport(profile),"changed local-send instructions excluded");byte^=1; }
}
}
int main() {
    GoldAbi(false);GoldAbi(true);
    PlayerData source{};unsigned char output[0xa0]{},castle[0x50]{};
    LocalSend(reinterpret_cast<uintptr_t>(LocalSendFixture),castle,output);
    Check(sent_transport==castle && sent_event==output,"local send uses native ECX/five-argument ABI");
    Check(HistoryConstruct(reinterpret_cast<uintptr_t>(HistoryAddedFixture),output,&source,2,0,1,nullptr,0,false)==output,"History native return");
    Check(audit.source==reinterpret_cast<uintptr_t>(&source) && audit.subtype==2 && audit.connection==0 && audit.local==1,"History ECX/four stack arguments with zero CID");
    Check(HistoryConstruct(reinterpret_cast<uintptr_t>(HistoryConfirmedFixture),output,&source,3,0,0,castle,0x10203040,true)==output,"History confirmed return");
    Check(audit.subtype==3 && audit.connection==0 && audit.local==0 && audit.castle==reinterpret_cast<uintptr_t>(castle) && audit.extra==0x10203040,"History six stack arguments");
    Participant member{1,1,9,1,"Crossplay",true,true};
    for(const auto& profile:Profiles) {
        TransportGuards(profile);
        for(auto change:{Change::Added,Change::Confirmed,Change::Leaving,Change::Removed}) Check(Eligible(change,member,profile),"known changes");
        auto candidate=member;candidate.connection=0;candidate.station=0;candidate.principal=0;
        Check(Eligible(Change::Confirmed,candidate,profile)==(profile.edition!=Crossplay::Edition::Gold),"zero CID retained for History independently of principal");
        candidate=member;candidate.principal=0;
        Check(Eligible(Change::Added,candidate,profile) && Eligible(Change::Confirmed,candidate,profile),"original zero player PID retained independently of CID and station");
        candidate=member;candidate.name.clear();
        Check(Eligible(Change::Added,candidate,profile) && Eligible(Change::Confirmed,candidate,profile),"native callback may precede name metadata");
        candidate=member;candidate.station=0;
        Check(Eligible(Change::Removed,candidate,profile) && Eligible(Change::Leaving,candidate,profile),"departure does not require a live station");
        candidate=member;candidate.slot=UINT32_MAX;
        Check(Eligible(Change::Added,candidate,profile) && !Eligible(Change::Confirmed,candidate,profile),"only original preassignment accepts unknown slot");
        for(auto slot:{0u,25u}) { candidate=member;candidate.slot=slot;Check(!Eligible(Change::Added,candidate,profile),"invalid native slot"); }
        candidate=member;candidate.connection=UINT32_MAX;Check(!Eligible(Change::Added,candidate,profile),"invalid CID");
        candidate=member;candidate.name=std::string(64,'x');Check(Eligible(Change::Confirmed,candidate,profile),"bounded name");
        for(const auto& name:{std::string(65,'x'),std::string("bad\0name",8),std::string("bad\nname"),std::string(1,static_cast<char>(0x80))}) {
            candidate=member;candidate.name=name;Check(!Eligible(Change::Confirmed,candidate,profile),"excluded name");
        }
        Check(!Eligible(static_cast<Change>(99),member,profile),"unknown change");
    }
    Check(!Readable(nullptr,1) && !Readable(reinterpret_cast<void*>(UINTPTR_MAX-1),4),"read range guards");
    ParticipantEvent event;
    Check(!event.Get() && event.Reset() && !event.Construct(Change::Added,member),"unbound event has no native calls");
    Check(!event.EnqueueLocal(),"unbound delivery has no native calls");
    Check(event.Failure()==ParticipantFailure::State,"unbound failure is identified");
    for(const auto& profile:Profiles) {
        Check(!event.Bind(profile.edition),"test executable cannot bind game profile");
        Check(event.Failure()==ParticipantFailure::Executable,"executable exclusion is identified");
    }
    std::printf("PASS %u participant ABI/policy checks; no game was launched.\n",checks);
}
