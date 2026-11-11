// Qualified two-human loopback experiment. Native CNetwork continues to own
// peer commands, end-of-turn messages and checksums. This only permits its
// authenticated local OSF handler to call the original Ready implementation.
bool local_multiplayer_enabled = false;
bool NetworkLocalReadyAllowed() {
#ifdef S7_GOLD_ACCOUNT_LOGIN
    if (!local_multiplayer_enabled || GetCurrentThreadId() != owner_thread.load()
        || !AccountLocalSceneAllowed()) return false;
    const uintptr_t main = *reinterpret_cast<uintptr_t*>(image + 0xcf83dc);
    if (!ValidRange(reinterpret_cast<void*>(main), 0x28)
        || *reinterpret_cast<uintptr_t*>(main) != image + 0xb55600) return false;
    const uintptr_t session = *reinterpret_cast<uintptr_t*>(main + 0x14);
    if (!ValidRange(reinterpret_cast<void*>(session), 0x44)
        || *reinterpret_cast<uintptr_t*>(session) != image + 0xb569bc
        || !AccountIsService(session)
        || *reinterpret_cast<unsigned int*>(session + 0x10) != 2
        || *reinterpret_cast<unsigned int*>(session + 0x30) != 2) return false;
    for (unsigned int offset : {0x3cu, 0x40u}) {
        const auto handle = *reinterpret_cast<unsigned int*>(session + offset);
        if (!handle || handle == 0xffffffffu) return false;
    }
    const auto local = *reinterpret_cast<unsigned int*>(session + 0xc);
    if (local != 1 && local != 2) return false;
    const uintptr_t setup = *reinterpret_cast<uintptr_t*>(image + 0xcf834c);
    if (!ValidRange(reinterpret_cast<void*>(setup), 0x1ac)) return false;
    // The independently observed playing-session roster is frame zero.
    const uintptr_t ref = *reinterpret_cast<uintptr_t*>(setup + 0xa4);
    if (!ValidRange(reinterpret_cast<void*>(ref), sizeof(uintptr_t))) return false;
    const uintptr_t rows = *reinterpret_cast<uintptr_t*>(ref);
    if (!ValidRange(reinterpret_cast<void*>(rows), 25 * 0x5c4)) return false;
    unsigned int humans = 0, indices = 0, own = 0;
    for (unsigned int row = 1; row <= 24; ++row) {
        const uintptr_t player = rows + row * 0x5c4;
        const auto kind = *reinterpret_cast<unsigned int*>(player + 0x64);
        if (kind == 2) return false; // AI remains a separate qualification.
        if (kind != 1) continue;
        if (*reinterpret_cast<unsigned int*>(player + 0x60) != 3) return false;
        const auto index = *reinterpret_cast<unsigned int*>(player + 0x5c);
        if ((index != 1 && index != 2) || (indices & (1u << index))) return false;
        indices |= 1u << index;
        ++humans;
        if (index == local) ++own;
    }
    return humans == 2 && indices == 6 && own == 1;
#else
    return false;
#endif
}
