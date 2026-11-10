// Authored fixtures only; no game images, SDKs, accounts or network use.
#include "gold_startup.cpp"
#include <cstdio>
#include <string>
#include <d3d9.h>

namespace {
GoldStartup::AsciiString name{}, ticket{};
GoldStartup::WideString display{};
std::array<uintptr_t,4> outer{};
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
}
int main(int argc,char** argv) {
    if (argc==3 && !strcmp(argv[1],"renderer-probe")) {
        if (!ModernRenderer()) return 30;
        using Factory=IDirect3D9* (WINAPI*)(UINT);
        const auto factory=reinterpret_cast<Factory>(GetProcAddress(GetModuleHandleW(L"d3d9.dll"),"Direct3DCreate9"));
        IDirect3D9* d3d=factory ? factory(D3D_SDK_VERSION) : nullptr;
        if (!d3d) return 31;
        D3DADAPTER_IDENTIFIER9 adapter{};
        const bool valid=d3d->GetAdapterCount()==1
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
    std::vector<Patch> patches;
    auto* call=reinterpret_cast<unsigned char*>(image+0x100);
    const unsigned char original[]={0xe8,0,0,0,0};memcpy(call,original,5);
    if (!Add(patches,0x100,{0xe8,0,0,0,0},FakeAppend) || !Write(patches[0],true)
        || !Write(patches[0],false) || memcmp(call,original,5)) return 8;
    call[1]=1;if (Write(patches[0],true) || call[1]!=1) return 9;
    for (unsigned int mode=0;mode<6;++mode) if (!RejectedChild(mode)) return 10+static_cast<int>(mode);
    std::puts("Gold startup ABI, profile/mutex isolation, renderer file guards, transaction guards and six fail-closed contexts passed.");
    return 0;
}
