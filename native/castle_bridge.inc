// Included only by the explicitly selected castle prototype build.
bool ReadOwnCastleMemory(uint32_t address, void* destination, size_t length) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
        destination, length, &copied) && copied == length;
}
#ifdef S7_CASTLE_WIRE_PROTOTYPE
#include "castle_wire_bridge.inc"
#endif
void* __fastcall ConstructLocalCastlePlayer(void* self, void*, unsigned int kind, unsigned int player,
    const void* setup, bool local, const void* castle, unsigned int extra) {
    const DWORD incoming_error = GetLastError();
    const void* selected = castle;
#ifdef S7_CASTLE_WIRE_PROTOTYPE
    castle_wire::Design peer_design;
    std::unique_ptr<CastleView> peer_view;
#endif
    if (kind == 3 && local && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && initialization == 2 && local_connect_probe) {
        const castle_profile::Layout layout{
            static_cast<uint32_t>(NativeVA(0x13A5228)),
            static_cast<uint32_t>(NativeVA(0x11372B8)),
            static_cast<uint32_t>(NativeVA(0x113729C)),
            static_cast<uint32_t>(NativeVA(0x11372D0))};
        const auto address = castle_profile::Selected(layout, ReadOwnCastleMemory);
#ifdef S7_CASTLE_WIRE_PROTOTYPE
        own_castle_player.store(player);
        castle_wire::Design design;
        std::vector<unsigned char> encoded;
        if (!SnapshotCastle(address, design) || !castle_wire::Encode(design, encoded)) {
            design = {};
            ReportCastleWire(8, "local-castle-wire-snapshot-unavailable");
        }
        // The profile's name is not sent. Designs exceeding the wire budget
        // are empty on peers. Use that same canonical value locally, too.
        CacheCastle(player, design);
        peer_design = std::move(design);
        peer_view = std::make_unique<CastleView>(peer_design); selected = &peer_view->value;
#else
        if (address) selected = reinterpret_cast<const void*>(address);
#endif
    }
#ifdef S7_CASTLE_WIRE_PROTOTYPE
    if (kind == 3 && !local && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && initialization == 2 && local_connect_probe && FindCastle(player, peer_design)) {
        peer_view = std::make_unique<CastleView>(peer_design); selected = &peer_view->value;
    }
#endif
    // Native 690C20 deep-copies CCastleForgeData with its own allocator. Keep
    // the original constructor, all other arguments and its returned pointer.
    using Original = void* (__thiscall*)(void*, unsigned int, unsigned int, const void*, bool, const void*, unsigned int);
    SetLastError(incoming_error);
    void* result = Native<Original>(0x690C20)(self, kind, player, setup, local, selected, extra);
    const DWORD result_error = GetLastError();
    static std::atomic<unsigned int> reported{0};
    const auto bit = selected != castle ? 1u : 2u;
    if (!(reported.fetch_or(bit) & bit))
        ReportLocalStatus(selected != castle ? "local-castle-selected-design-supplied" : "local-castle-original-design-kept");
    SetLastError(result_error);
    return result;
}
