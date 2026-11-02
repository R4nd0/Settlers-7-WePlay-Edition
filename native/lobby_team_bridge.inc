bool ReadLobbyTeamMemory(uint32_t address, void* destination, size_t length) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
        destination, length, &copied) && copied == length;
}

void __fastcall SetLocalLobbyTeam(void* self, void*, uint32_t team) {
    const DWORD incoming_error = GetLastError();
    using Store = void (__thiscall*)(void*, uint32_t);
    using SetTeam = void (__thiscall*)(void*, uint32_t, uint32_t);
    static thread_local bool active = false;
    lobby_teams::Player player;
    const lobby_teams::Layout layout{static_cast<uint32_t>(NativeVA(0x13A5218)),
        static_cast<uint32_t>(NativeVA(0x113AE54)), static_cast<uint32_t>(NativeVA(0x11372D0))};
    const bool owned = GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && !active && initialization == 2 && local_connect_probe
        && team >= 1 && team <= 8
        && lobby_teams::Find(layout, reinterpret_cast<uint32_t>(self), ReadLobbyTeamMemory, player);
    SetLastError(incoming_error);
    if (!owned) {
        Native<Store>(0x586310)(self, team);
        return;
    }
    struct Scope { bool& value; explicit Scope(bool& v) : value(v) { value = true; } ~Scope() { value = false; } } scope(active);
    // The roster used a scalar store, leaving alliances from an earlier lobby.
    // Use the game's own reciprocal team update. For a repeated team number,
    // briefly mark only this preparation entry unassigned, so its early return
    // cannot retain stale relations. Native handling preserves neutral allies.
    if (player.previous_team == team) Native<Store>(0x586310)(self, 0);
    Native<SetTeam>(0x586720)(reinterpret_cast<void*>(player.configuration), player.id, team);
    static std::atomic<bool> reported{false};
    if (!reported.exchange(true)) ReportLocalStatus("local-lobby-team-relations-rebuilt");
    SetLastError(incoming_error); // The original scalar store leaves this intact.
}
