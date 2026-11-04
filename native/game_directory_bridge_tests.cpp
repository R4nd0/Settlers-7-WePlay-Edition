#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}
#ifdef S7_GAME_DIRECTORY
namespace {
struct State { std::string input, output; int boolean = -1, originals = 0, conversions = 0; };
const char* __cdecl ToString(void* state, int index, size_t* length) {
    auto& value = *static_cast<State*>(state); if (index != 1) abort();
    ++value.conversions; if (length) *length = value.input.size();
    SetLastError(5555); return value.input.c_str();
}
void __cdecl PushBoolean(void* state, int value) { static_cast<State*>(state)->boolean = value; SetLastError(3333); }
int __cdecl Original(void* state) { ++static_cast<State*>(state)->originals; SetLastError(6789); return 17; }
void Jump(uintptr_t address, const void* destination) {
    auto* at = Native<unsigned char*>(address); at[0] = 0xe9;
    const auto offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(destination) - reinterpret_cast<uintptr_t>(at + 5));
    memcpy(at + 1, &offset, 4);
}
}
#endif
int main() {
#ifdef S7_GAME_DIRECTORY
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1106000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return 2;
    image_base = reinterpret_cast<uintptr_t>(memory);
    int checks = 0, failures = 0;
    auto check = [&](bool value) { ++checks; if (!value) ++failures; };
    for (bool steam : {false,true}) {
    steam_history = steam;
    Jump(0x45B650, reinterpret_cast<const void*>(&ToString)); Jump(0x44C6E0, reinterpret_cast<const void*>(&PushBoolean));
    Jump(0x5D1550, reinterpret_cast<const void*>(&Original));
    initialization = 2; local_connect_probe = true; bootstrap_thread = GetCurrentThreadId();
    State state;
    check(DirectoryHex("0123456789abcdef",16));check(!DirectoryHex("abcd/../",8));check(!DirectoryHex(nullptr,1));
    state.input = "ordinary console text";SetLastError(4444);
    check(DirectoryConsole(&state)==17 && state.originals==1 && GetLastError()==6789);
    state.input = "weplay-directory-read-v1";SetLastError(4444);
    check(DirectoryConsole(&state)==1 && state.boolean==0 && state.originals==1 && GetLastError()==4444);
    // A test executable has no valid Base/Bin DLL suffix: never read arbitrary files.
    check(DirectoryDataPath(L"global-games-v1.txt").empty());
    state.input="weplay-invitation-read-v1";state.boolean=-1;SetLastError(4444);
    check(DirectoryConsole(&state)==1 && state.boolean==0 && GetLastError()==4444);
    for (const char* command : {"weplay-invitation-open-v1:", "weplay-invitation-open-v1:../../bad",
            "weplay-invitation-open-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "weplay-invitation-open-v1:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}) {
        state.input=command;state.boolean=-1;SetLastError(4444);
        check(DirectoryConsole(&state)==1 && state.boolean==0 && GetLastError()==4444);
    }
    check(!RequestInvitation(nullptr,0));
    const std::string nonce(32,'a'), header="weplay-invitation-ui-v1|"+nonce;
    check(InvitationSnapshotMatches(header+"|private\n",nonce.c_str()));
    check(InvitationSnapshotMatches(header+"|public\n",nonce.c_str()));
    for (const char* mode : {"|guest\n","|lan\n","|private\nextra","|public","|private\r\n"})
        check(!InvitationSnapshotMatches(header+mode,nonce.c_str()));
    check(!InvitationSnapshotMatches(header+"|private\n",std::string(32,'b').c_str()));
    check(!InvitationSnapshotMatches(header+"|private\n",nullptr));
    for (const char* command : {"weplay-directory-join-v1:", "weplay-directory-join-v1:../../bad", "weplay-directory-join-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}) {
        state.input = command;state.boolean = -1;SetLastError(4444);
        check(DirectoryConsole(&state)==1 && state.boolean==0 && GetLastError()==4444);
    }
    const int before = state.conversions;bootstrap_thread = GetCurrentThreadId()+1;state.input="weplay-directory-read-v1";
    check(DirectoryConsole(&state)==17 && state.conversions==before);
    bootstrap_thread = GetCurrentThreadId();initialization=0;
    check(DirectoryConsole(&state)==17 && state.conversions==before);
    }
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("%s %d game directory bridge checks\n",failures?"FAIL":"PASS",checks);return failures?1:0;
#else
    std::puts("SKIP: game directory feature not selected");return 0;
#endif
}
