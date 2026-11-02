#include "offline_bootstrap.cpp"
#include <cstdio>
#include <thread>
void ReportLocalStatus(const char*) {}

#ifdef S7_LOCAL_NETWORK_PUMP
namespace {
uint32_t stores = 0, updates = 0, received_id = 0, received_team = 0;
uint8_t received_previous_team = 0;
DWORD received_error = 0;
void* received_configuration = nullptr;
bool recurse = false;
void __fastcall FakeStore(void* self, void*, uint32_t team) {
    ++stores;
    if (self) static_cast<unsigned char*>(self)[0x68] = static_cast<unsigned char>(team);
}
void __fastcall FakeTeam(void* self, void*, uint32_t player, uint32_t team) {
    ++updates; received_id = player; received_team = team;
    received_configuration = self; received_error = GetLastError();
    auto* entry = reinterpret_cast<unsigned char*>(*static_cast<uint32_t*>(static_cast<void*>(static_cast<unsigned char*>(self) + 4))) + player * 0x5a8;
    received_previous_team = entry[0x68];
    if (recurse) SetLocalLobbyTeam(entry, nullptr, team);
    entry[0x68] = static_cast<unsigned char>(team);
    SetLastError(777);
}
void Jump(uintptr_t address, const void* function) {
    auto* code = Native<unsigned char*>(address);
    code[0] = 0xe9;
    const int32_t displacement = static_cast<int32_t>(reinterpret_cast<uintptr_t>(function) - reinterpret_cast<uintptr_t>(code + 5));
    memcpy(code + 1, &displacement, 4);
    FlushInstructionCache(GetCurrentProcess(), code, 5);
}
}
#endif

int main() {
#ifndef S7_LOCAL_NETWORK_PUMP
    std::puts("PASS lobby team bridge disabled without local network pump");
    return 0;
#else
    int checks = 0, failures = 0;
    auto check = [&](bool value) { ++checks; if (!value) ++failures; };
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1106000,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!memory) return 2;
    image_base = reinterpret_cast<uintptr_t>(memory);
    for (bool steam : {false, true}) {
        steam_history = steam;
        Jump(0x586310, reinterpret_cast<void*>(&FakeStore));
        Jump(0x586720, reinterpret_cast<void*>(&FakeTeam));
        auto* manager = memory + 0x10000;
        auto* configuration = memory + 0x11000;
        auto* players = memory + 0x20000;
        auto* heads = memory + 0x30000;
        const auto word = [](void* at, uintptr_t value) { const uint32_t v = static_cast<uint32_t>(value); memcpy(at, &v, 4); };
        word(Native<void*>(0x13A5218), reinterpret_cast<uintptr_t>(manager));
        word(manager, NativeVA(0x113AE54)); manager[4] = 0;
        word(manager + 0x98, 0); // The current buffer is deliberately unusable.
        word(manager + 0xc4 + 0x98, reinterpret_cast<uintptr_t>(configuration));
        word(configuration, reinterpret_cast<uintptr_t>(players));
        word(configuration + 4, reinterpret_cast<uintptr_t>(players)); players[0x8db8] = 4;
        for (uint32_t id = 0; id < 25; ++id) {
            auto* entry = players + id * 0x5a8;
            auto* head = heads + id * 12;
            word(entry + 0x55c, NativeVA(0x11372D0));
            word(entry + 0x64, id <= 2 ? 1 : 2);
            entry[0x68] = static_cast<unsigned char>(id);
            word(entry + 0x554, reinterpret_cast<uintptr_t>(head)); word(entry + 0x558, 0);
            word(head, reinterpret_cast<uintptr_t>(head)); word(head + 4, reinterpret_cast<uintptr_t>(head));
        }
        auto* registry = memory + 0x32000;
        word(players + 0x8dbc, reinterpret_cast<uintptr_t>(registry));
        word(registry, reinterpret_cast<uintptr_t>(registry + 12));
        for (uint32_t id = 0; id < 25; ++id) {
            auto* node = registry + (id + 1) * 12;
            word(node, reinterpret_cast<uintptr_t>(id == 24 ? registry : node + 12));
            node[8] = static_cast<unsigned char>(id);
        }
        auto* entry = players + 3 * 0x5a8;
        const lobby_teams::Layout layout{static_cast<uint32_t>(NativeVA(0x13A5218)),
            static_cast<uint32_t>(NativeVA(0x113AE54)), static_cast<uint32_t>(NativeVA(0x11372D0))};
        lobby_teams::Player found;
        check(lobby_teams::Find(layout, reinterpret_cast<uint32_t>(entry), ReadLobbyTeamMemory, found)
            && found.id == 3 && found.configuration == reinterpret_cast<uint32_t>(configuration));
        bootstrap_thread = GetCurrentThreadId(); initialization = 2; local_connect_probe = true;
        stores = updates = 0; entry[0x68] = 2; SetLastError(4321);
        SetLocalLobbyTeam(entry, nullptr, 3);
        check(updates == 1 && !stores && received_id == 3 && received_team == 3
            && received_configuration == configuration && received_error == 4321 && GetLastError() == 4321
            && received_previous_team == 2 && entry[0x68] == 3);
        SetLocalLobbyTeam(entry, nullptr, 3);
        check(updates == 2 && stores == 1 && received_previous_team == 0 && entry[0x68] == 3 && GetLastError() == 4321);
        recurse = true; SetLocalLobbyTeam(entry, nullptr, 4); recurse = false;
        check(updates == 3 && stores == 2 && entry[0x68] == 4);
        const auto reject = [&](uint32_t team) {
            const auto before = updates, stored = stores;
            SetLastError(4321); SetLocalLobbyTeam(entry, nullptr, team);
            check(updates == before && stores == stored + 1 && GetLastError() == 4321);
            entry[0x68] = 3;
        };
        reject(0); reject(9); reject(UINT32_MAX);
        initialization = 1; reject(3); initialization = 2;
        local_connect_probe = false; reject(3); local_connect_probe = true;
        std::thread foreign([&]() { reject(3); }); foreign.join();
        manager[4] = 2; reject(3); manager[4] = 0;
        word(manager, 0); reject(3); word(manager, NativeVA(0x113AE54));
        word(entry + 0x55c, 0); reject(3); word(entry + 0x55c, NativeVA(0x11372D0));
        word(entry + 0x64, 8); reject(3); word(entry + 0x64, 2);
        players[0x8db8] = 2; reject(3); players[0x8db8] = 4;
        word(entry + 0x558, 26); reject(3); word(entry + 0x558, 0);
        auto* head = heads + 3 * 12;
        word(head, 1); reject(3); word(head, reinterpret_cast<uintptr_t>(head));
        word(head + 4, 1); reject(3); word(head + 4, reinterpret_cast<uintptr_t>(head));
        auto* node = memory + 0x31000;
        auto* second = node + 12;
        word(head, reinterpret_cast<uintptr_t>(node)); word(head + 4, reinterpret_cast<uintptr_t>(node));
        word(node, reinterpret_cast<uintptr_t>(head)); word(node + 4, reinterpret_cast<uintptr_t>(head));
        node[8] = 4; word(entry + 0x558, 1);
        check(lobby_teams::Find(layout, reinterpret_cast<uint32_t>(entry), ReadLobbyTeamMemory, found));
        node[8] = 25; reject(3); node[8] = 4;
        word(node + 4, 0); reject(3); word(node + 4, reinterpret_cast<uintptr_t>(head));
        word(node, reinterpret_cast<uintptr_t>(node)); reject(3); word(node, reinterpret_cast<uintptr_t>(head));
        word(head + 4, reinterpret_cast<uintptr_t>(head)); reject(3); word(head + 4, reinterpret_cast<uintptr_t>(node));
        word(node, reinterpret_cast<uintptr_t>(second)); word(second, reinterpret_cast<uintptr_t>(head));
        word(second + 4, reinterpret_cast<uintptr_t>(node)); second[8] = 4;
        word(head + 4, reinterpret_cast<uintptr_t>(second)); word(entry + 0x558, 2); reject(3);
        second[8] = 5;
        check(lobby_teams::Find(layout, reinterpret_cast<uint32_t>(entry), ReadLobbyTeamMemory, found));
        word(head, reinterpret_cast<uintptr_t>(head)); word(head + 4, reinterpret_cast<uintptr_t>(head));
        word(entry + 0x558, 0);
        check(!lobby_teams::Find(layout, reinterpret_cast<uint32_t>(entry + 1), ReadLobbyTeamMemory, found));
        check(!lobby_teams::Find(layout, reinterpret_cast<uint32_t>(players), ReadLobbyTeamMemory, found));
        auto* neutral = players + 21 * 0x5a8;
        word(neutral + 0x64, 8); neutral[0x68] = 0;
        word(neutral + 0x558, 26); word(neutral + 0x554, 0);
        check(lobby_teams::Find(layout, reinterpret_cast<uint32_t>(entry), ReadLobbyTeamMemory, found));
        neutral[0x68] = 3; reject(3); neutral[0x68] = 0;
        word(configuration, 0); reject(3); word(configuration, reinterpret_cast<uintptr_t>(players));
        word(registry, reinterpret_cast<uintptr_t>(registry)); reject(3);
        word(registry, reinterpret_cast<uintptr_t>(registry + 12));
        registry[20] = 25; reject(3); registry[20] = 0;
        word(manager + 0x98, reinterpret_cast<uintptr_t>(configuration));
        word(manager + 0xc4 + 0x98, 0); manager[4] = 1;
        check(lobby_teams::Find(layout, reinterpret_cast<uint32_t>(entry), ReadLobbyTeamMemory, found));
        manager[4] = 0; reject(3); // Same entry belongs to the current game now.
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("%s %d lobby team ownership/ABI checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
#endif
}
