// Authored fixtures only; no game images, SDKs, accounts or network use.
#include "gold_startup.cpp"
#include <cstdio>
#include <string>
#include <d3d9.h>

namespace {
GoldStartup::AsciiString name{}, ticket{};
GoldStartup::WideString display{};
std::array<uintptr_t,7> outer{};
std::array<unsigned char,0x11c> facade{};
GoldStartup::OrbitRecord record{};
void* appended = nullptr;
void* assigned = nullptr;
const char* appended_text = nullptr;
const wchar_t* assigned_text = nullptr;
unsigned int assigned_length = 0;
__declspec(naked) void FakeAppend() {
    __asm {
        mov appended, eax
        mov edx, dword ptr [esp + 4]
        mov appended_text, edx
        mov dword ptr [eax + 8], 15
        ret 4
    }
}
__declspec(naked) void FakeWide() {
    __asm {
        mov assigned_length, ecx
        mov eax, dword ptr [esp + 4]
        mov assigned, eax
        mov edx, dword ptr [esp + 8]
        mov assigned_text, edx
        mov dword ptr [eax + 20], ecx
        ret 8
    }
}
void Setup() {
    image = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0xd10000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if (!image) std::exit(2);
    *reinterpret_cast<GoldStartup::OrbitRecord**>(image+0xd0b58c)=&record;
    outer[0]=image+0xb247dc; outer[3]=reinterpret_cast<uintptr_t>(facade.data());
    *reinterpret_cast<uintptr_t*>(facade.data())=image+0xb25ec0;
    display.capacity=7;
    ascii_append=reinterpret_cast<uintptr_t>(&FakeAppend);
    wide_assign=reinterpret_cast<uintptr_t>(&FakeWide);
    GoldStartup::OrbitRecord* destination=&record;
    __asm {
        mov edi, destination
        call InitOrbitBridge
    }
}
bool IdentityAbi() {
    auto* at=reinterpret_cast<unsigned char*>(image+0xe577);
    at[0]=0xe8;
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(LocalIdentityBridge)-reinterpret_cast<uintptr_t>(at)-5);
    memcpy(at+1,&relative,4); at[5]=0xc3;
    DWORD before=0,after=0,observed_bx=0,observed_si=0,observed_di=0;
    auto* o=outer.data();auto* n=&name;auto* t=&ticket;auto* d=&display;
    __asm {
        mov before, esp
        mov ebx, 0x12345678
        mov esi, 0x34567890
        mov edi, 0x56789012
        mov edx, o
        mov ecx, t
        lea eax, after_identity
        push eax
        push d
        push n
        jmp at
    after_identity:
        mov observed_bx, ebx
        mov observed_si, esi
        mov observed_di, edi
        mov after, esp
    }
    return before==after && observed_bx==0x12345678 && observed_si==0x34567890 && observed_di==0x56789012
        && appended==&name && assigned==&display && assigned_length==17
        && !strcmp(appended_text,"WePlayGoldLocal") && !wcscmp(assigned_text,L"WePlay Gold Local")
        && !ticket.length && !identity_active.load();
}
bool RejectedChild(unsigned int mode) {
    wchar_t path[32768]{};if (!GetModuleFileNameW(nullptr,path,32768)) return false;
    std::wstring command=L"\""+std::wstring(path)+L"\" reject "+std::to_wstring(mode);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    if (!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)) return false;
    const DWORD wait=WaitForSingleObject(child.hProcess,10000);DWORD code=0;
    const bool result=wait==WAIT_OBJECT_0 && GetExitCodeProcess(child.hProcess,&code) && code==Failure;
    if (wait==WAIT_TIMEOUT) TerminateProcess(child.hProcess,2);
    CloseHandle(child.hThread);CloseHandle(child.hProcess);return result;
}
bool ProfileIsolation() {
    unsigned char digest[32]{};
    for (unsigned int i=0;i<32;++i) digest[i]=static_cast<unsigned char>(i);
    profile_names=GoldProfile::FromDigest(digest);
    if (strcmp(profile_names.id.data(),"000102030405060708090a0b0c0d0e0f")
        || strcmp(profile_names.suffix.data(),"\\WePlay\\Gold\\000102030405060708090a0b0c0d0e0f")
        || strcmp(profile_names.mutex.data(),"Local\\G00010203")) return false;
    const auto first=profile_names;
    digest[0]++;
    const auto second=GoldProfile::FromDigest(digest);
    if (!strcmp(first.suffix.data(),second.suffix.data()) || !strcmp(first.mutex.data(),second.mutex.data())) return false;
    const uintptr_t original=image+0xb2ecf0;
    for (uintptr_t rva : {0x133fdeu,0x1352fbu,0x135600u}) {
        *reinterpret_cast<unsigned char*>(image+rva)=0x68;
        memcpy(reinterpret_cast<void*>(image+rva+1),&original,4);
    }
    constexpr char mutex[]="Global\\Settlers7";
    memcpy(reinterpret_cast<void*>(image+0xb24fc4),mutex,sizeof(mutex));
    std::vector<Patch> patches;
    if (!ProfilePatches(patches) || patches.size()!=4) return false;
    for (const auto& patch:patches) if (!Write(patch,true)) return false;
    for (uintptr_t rva : {0x133fdeu,0x1352fbu,0x135600u})
        if (*reinterpret_cast<uintptr_t*>(image+rva+1)!=reinterpret_cast<uintptr_t>(profile_names.suffix.data())) return false;
    if (memcmp(reinterpret_cast<void*>(image+0xb24fc4),profile_names.mutex.data(),16)) return false;
    for (const auto& patch:patches) if (!Write(patch,false)) return false;
    for (uintptr_t rva : {0x133fdeu,0x1352fbu,0x135600u}) {
        auto* instruction=reinterpret_cast<unsigned char*>(image+rva);
        instruction[0]++;
        std::vector<Patch> rejected;
        if (ProfilePatches(rejected)) return false;
        instruction[0]--;
        instruction[1]++;
        rejected.clear();
        if (ProfilePatches(rejected)) return false;
        instruction[1]--;
    }
    *reinterpret_cast<char*>(image+0xb24fc4)='X';
    patches.clear();
    return !ProfilePatches(patches);
}
bool RendererFileGuards() {
    wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH,folder) || !GetTempFileNameW(folder,L"wpg",0,path)) return false;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,0,nullptr);
    if (file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0;
    const bool wrote=WriteFile(file,"abc",3,&written,nullptr) && written==3;
    CloseHandle(file);
    char digest[65]{};
    HANDLE held=INVALID_HANDLE_VALUE;
    bool valid=wrote && FileDigest(path,digest,3,&held)
        && !strcmp(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    valid=valid && file==INVALID_HANDLE_VALUE;
    if (file!=INVALID_HANDLE_VALUE) CloseHandle(file);
    if (held!=INVALID_HANDLE_VALUE) CloseHandle(held);
    valid=valid && !FileDigest(path,digest,4);
    DeleteFileW(path);
    // The authored fixture directory has no DXVK payload; no system fallback.
    return valid && !ModernRenderer();
}
#ifdef S7_GOLD_LOCAL_SIMULATION
std::array<unsigned char,0x200> local_game{}, message_core{};
std::array<uintptr_t,3> message_backend{};
bool fixture_result=false,fixture_called=false;
bool __fastcall FixtureReady(void* backend,void*,unsigned int turn,unsigned char* ready,unsigned int delay,bool network) {
    fixture_called=backend==message_backend.data() && turn==121 && delay==18 && !network
        && !message_core[0x16] && !message_core[0x3a] && message_core[0x58]==1 && message_core[0x14]==7;
    *ready=fixture_result ? 1 : 0;
    return fixture_result;
}
bool __stdcall FixtureAdvance(void* core) {
    fixture_called=core==message_core.data() && !message_core[0x16] && message_core[0x58]==1
        && message_core[0x1f0]==1 && message_core[0x14]==7;
    return fixture_result;
}
void LocalSetup() {
    outer[2]=reinterpret_cast<uintptr_t>(message_backend.data());
    message_backend[0]=image+0xb28b74;message_backend[2]=reinterpret_cast<uintptr_t>(message_core.data());
    *reinterpret_cast<uintptr_t*>(local_game.data())=image+0xb32870;
    *reinterpret_cast<uintptr_t*>(local_game.data()+0xcc)=reinterpret_cast<uintptr_t>(outer.data());
    *reinterpret_cast<unsigned int*>(local_game.data()+0x184)=2;
    *reinterpret_cast<uintptr_t*>(image+0xcf8310)=reinterpret_cast<uintptr_t>(local_game.data());
    *reinterpret_cast<uintptr_t*>(message_core.data())=image+0xb29058;
    *reinterpret_cast<uintptr_t*>(message_core.data()+4)=image+0xb29064;
    const unsigned char not_network[]={0x33,0xc0,0xc3};
    memcpy(reinterpret_cast<void*>(image+0x58c910),not_network,sizeof(not_network));
    message_core[0x14]=7;
    local_ready=reinterpret_cast<NativeReady>(FixtureReady);
    local_advance=FixtureAdvance;
}
bool LocalReadyAbi() {
    auto* at=reinterpret_cast<unsigned char*>(image+0x1a6a93);
    at[0]=0xe8;
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(LocalReady)-reinterpret_cast<uintptr_t>(at)-5);
    memcpy(at+1,&relative,4);at[5]=0xc3;
    DWORD before=0,after=0,observed_bx=0,observed_si=0,observed_di=0,result=0;
    unsigned char ready=9;auto* output=&ready;auto* owner=outer.data();
    fixture_called=false;message_core[0x16]=1;message_core[0x3a]=1;message_core[0x58]=0;
    __asm {
        mov before, esp
        mov ebx, 0x12345678
        mov esi, 0x34567890
        mov edi, 0x56789012
        mov ecx, owner
        lea eax, local_ready_return
        push eax
        push 0
        push 18
        push output
        push 121
        jmp at
    local_ready_return:
        movzx eax, al
        mov result, eax
        mov observed_bx, ebx
        mov observed_si, esi
        mov observed_di, edi
        mov after, esp
    }
    return fixture_called && before==after && observed_bx==0x12345678 && observed_si==0x34567890 && observed_di==0x56789012
        && result==static_cast<DWORD>(fixture_result) && ready==static_cast<unsigned char>(fixture_result) && !local_step_active.load();
}
bool LocalAdvanceAbi() {
    auto* at=reinterpret_cast<unsigned char*>(image+0x40ed4);at[0]=0xe8;
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(LocalAdvance)-reinterpret_cast<uintptr_t>(at)-5);
    memcpy(at+1,&relative,4);at[5]=0xc3;
    DWORD before=0,after=0,result=0;auto* core=message_core.data();fixture_called=false;
    __asm {
        mov before, esp
        lea eax, local_advance_return
        push eax
        push core
        jmp at
    local_advance_return:
        movzx eax, al
        mov result, eax
        mov after, esp
    }
    return fixture_called && before==after && result==static_cast<DWORD>(fixture_result) && !local_step_active.load();
}
std::array<unsigned int,58> local_task{};
unsigned int choice_mode=0;
std::string choice_calls;
void __stdcall FixtureExisting(void*,void* output) {
    choice_calls+='E';
    if (choice_mode==1) static_cast<unsigned int*>(output)[2]=17;
}
void* __stdcall FixtureTransport(void*,unsigned int owner,void* output) {
    if (owner!=0x1234) Stop();
    choice_calls+='T';auto* data=static_cast<unsigned int*>(output);data[2]=choice_mode==4 ? 0 : 19;
    const float cost=10;memcpy(data+3,&cost,4);return output;
}
void* __stdcall FixtureAlternative(void*,unsigned int owner,void* output) {
    if (owner!=0x1234) Stop();
    choice_calls+='A';auto* data=static_cast<unsigned int*>(output);
    const float cost=choice_mode==2 ? 5.0f : 15.0f;
    data[2]=(choice_mode==3 || choice_mode==4) ? 0 : 23;memcpy(data+3,&cost,4);return output;
}
void* __stdcall FixtureFallback(void*,unsigned int owner,void* output) {
    if (owner!=0x1234) Stop();
    choice_calls+='F';static_cast<unsigned int*>(output)[2]=29;return output;
}
void TaskSetup() {
    local_task.fill(0);local_task[0]=static_cast<unsigned int>(image+0xb4c684);
    local_choice_existing=FixtureExisting;local_choice_transport=FixtureTransport;
    local_choice_alternative=FixtureAlternative;local_choice_fallback=FixtureFallback;
}
bool LocalTaskAbi() {
    TaskSetup();auto* at=reinterpret_cast<unsigned char*>(image+0x54943f);
    at[0]=0xe8;
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(LocalTaskBridge)-reinterpret_cast<uintptr_t>(at)-5);
    memcpy(at+1,&relative,4);
    const unsigned char tail[]={0x83,0xc4,0x20,0xc3};memcpy(at+5,tail,sizeof(tail));
    for (choice_mode=0;choice_mode<5;++choice_mode) {
        std::array<unsigned int,11> output{};output[3]=output[4]=0x7f7fffff;
        auto* task=local_task.data();auto* result=output.data();
        DWORD before=0,after=0,observed_bx=0,observed_si=0,observed_di=0;
        choice_calls.clear();
        __asm {
            mov before, esp
            mov ebx, result
            mov esi, task
            mov edi, 0x56789012
            lea eax, after_task_choice
            push eax
            sub esp, 0x20
            mov dword ptr [esp + 0x1c], 0x1234
            jmp at
        after_task_choice:
            mov after, esp
            mov observed_bx, ebx
            mov observed_si, esi
            mov observed_di, edi
        }
        if (before!=after || observed_bx!=reinterpret_cast<DWORD>(result)
            || observed_si!=reinterpret_cast<DWORD>(task) || observed_di!=0x56789012
            || local_step_active.load() || local_task[0xb4/4]) return false;
        const unsigned int expected[]={19,17,23,19,29};
        const char* expected_calls[]={"ETA","E","ETA","ETA","ETAF"};
        if (output[2]!=expected[choice_mode] || choice_calls!=expected_calls[choice_mode]) return false;
    }
    return true;
}
bool LocalSimulationAbi() {
    LocalSetup();
    for (bool value : {false,true}) {
        fixture_result=value;
        if (!LocalReadyAbi() || !LocalAdvanceAbi()) return false;
    }
    return LocalTaskAbi();
}
bool LocalPatchGuards() {
    *reinterpret_cast<uintptr_t*>(image+0xb247dc)=image+0x1470;
    *reinterpret_cast<uintptr_t*>(image+0xb28b78)=image+0x40ec0;
    *reinterpret_cast<uintptr_t*>(image+0xb28b7c)=image+0x40ed0;
    const unsigned char existing[]={0x55,0x8b,0xec,0x83,0xec,0x0c};
    const unsigned char search[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x6a,0xff};
    const unsigned char fallback[]={0x55,0x8b,0xec,0x81,0xec,0xd0,0,0,0};
    const unsigned char caller[]={0xb8,1,0,0,0,0x39,0x45,0x10,0x0f,0x84,0x4d,2,0,0};
    memcpy(reinterpret_cast<void*>(image+0x548940),existing,sizeof(existing));
    memcpy(reinterpret_cast<void*>(image+0x548a50),search,sizeof(search));
    memcpy(reinterpret_cast<void*>(image+0x548d60),search,sizeof(search));
    memcpy(reinterpret_cast<void*>(image+0x548250),fallback,sizeof(fallback));
    memcpy(reinterpret_cast<void*>(image+0x54943f),caller,sizeof(caller));
    std::vector<Patch> valid;
    if (!LocalSimulationPatches(valid) || valid.size()!=2) return false;
    for (uintptr_t rva : {0xb247dcu,0xb28b78u,0xb28b7cu,0x548940u,0x548a50u,0x548d60u,0x548250u,0x54943fu,0x54944cu}) {
        auto* byte=reinterpret_cast<unsigned char*>(image+rva);*byte^=1;
        std::vector<Patch> rejected;const bool accepted=LocalSimulationPatches(rejected);*byte^=1;
        if (accepted) return false;
    }
    for (auto& patch:valid) if (!Write(patch,true)) return false;
    for (auto& patch:valid) if (!Write(patch,false)) return false;
    return !memcmp(reinterpret_cast<void*>(image+0x54943f),caller,sizeof(caller))
        && *reinterpret_cast<uintptr_t*>(image+0xb247dc)==image+0x1470;
}
#endif
}
int main(int argc,char** argv) {
    if (argc==3 && !strcmp(argv[1],"renderer-probe")) {
        if (!ModernRenderer()) return 30;
        using Factory=IDirect3D9* (WINAPI*)(UINT);
        const auto factory=reinterpret_cast<Factory>(GetProcAddress(GetModuleHandleW(L"d3d9.dll"),"Direct3DCreate9"));
        IDirect3D9* d3d=factory ? factory(D3D_SDK_VERSION) : nullptr;
        if (!d3d) return 31;
        D3DADAPTER_IDENTIFIER9 adapter{};
        const UINT count=d3d->GetAdapterCount();
        std::printf("D3D9 display adapters: %u\n",count);
        const bool valid=count>0
            && SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT,0,&adapter))
            && !strcmp(adapter.Description,argv[2]);
        std::printf("Pinned DXVK default adapter: %s\n",adapter.Description);
        d3d->Release();
        return valid ? 0 : 32;
    }
    if (argc==3 && !strcmp(argv[1],"reject")) {
        Setup();const unsigned int mode=static_cast<unsigned int>(std::stoul(argv[2]));
        if (mode==0) record.functions[3]=1;
        if (mode==1) owner_thread.store(GetCurrentThreadId()+1);
        if (mode==2) outer[0]++;
        if (mode==3) facade[0x1c]=1;
        if (mode==4) ticket.length=1;
#ifdef S7_GOLD_LOCAL_SIMULATION
        if (mode>=6) {
            LocalSetup();TaskSetup();std::array<unsigned int,11> output{};
            if (mode==7) local_task[0]++;
            if (mode==8) local_task[0xb4/4]=1;
            if (mode==9) { const unsigned char network[]={0xb0,0x01,0xc3};memcpy(reinterpret_cast<void*>(image+0x58c910),network,3); }
            if (mode==10) *reinterpret_cast<uintptr_t*>(local_game.data()+0xcc)=0;
            if (mode==12) *reinterpret_cast<unsigned int*>(local_game.data()+0x184)=3;
            LocalTaskChoice(local_task.data(),mode==11 ? nullptr : output.data(),0x1234,image+(mode==6 ? 0x549445 : 0x549444));
            return 3;
        }
#endif
        LocalIdentity(outer.data(),&ticket,&name,&display,image+(mode==5 ? 0xe57d : 0xe57c));
        return 3;
    }
    if (GoldInitialize(nullptr)!=0) return 4; // Unknown EXE rejected before hooks.
    for (unsigned int field=0;field<24;++field) {
        GoldStartup::OrbitRecord value{};
        if (!GoldStartup::Empty(&value,0)) return 5;
        reinterpret_cast<uint32_t*>(&value)[field]=1;
        if (GoldStartup::Empty(&value,0)) return 6;
    }
    Setup();if (!GoldStartup::Empty(&record,1) || !IdentityAbi() || !ProfileIsolation() || !RendererFileGuards()) return 7;
#ifdef S7_GOLD_LOCAL_SIMULATION
    if (!LocalSimulationAbi() || !LocalPatchGuards()) return 33;
#endif
    std::vector<Patch> patches;
    auto* call=reinterpret_cast<unsigned char*>(image+0x100);
    const unsigned char original[]={0xe8,0,0,0,0};memcpy(call,original,5);
    if (!Add(patches,0x100,{0xe8,0,0,0,0},FakeAppend) || !Write(patches[0],true)
        || !Write(patches[0],false) || memcmp(call,original,5)) return 8;
    call[1]=1;if (Write(patches[0],true) || call[1]!=1) return 9;
    for (unsigned int mode=0;mode<6;++mode) if (!RejectedChild(mode)) return 10+static_cast<int>(mode);
#ifdef S7_GOLD_LOCAL_SIMULATION
    for (unsigned int mode=6;mode<13;++mode) if (!RejectedChild(mode)) return 40+static_cast<int>(mode);
#endif
#ifdef S7_GOLD_LOCAL_SIMULATION
    std::puts("Gold startup/local logistics ABI, profile/renderer and transaction guards, thirteen fail-closed contexts passed.");
#else
    std::puts("Gold startup ABI, profile/mutex isolation, renderer file guards, transaction guards and six fail-closed contexts passed.");
#endif
    return 0;
}
