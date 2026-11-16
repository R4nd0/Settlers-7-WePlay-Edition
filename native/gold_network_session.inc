// Qualified two-human loopback experiment. Native CNetwork continues to own
// peer commands, end-of-turn messages and checksums. This only permits its
// authenticated local OSF handler to call the original Ready implementation.
bool local_multiplayer_enabled = false;
bool local_peer_leave_enabled = false;
// A native human-to-AI transition may only continue the exact world which
// previously qualified with two connected humans. This is not initial AI
// admission, reconnect or a replacement participant supplied by the adapter.
struct GoldNetworkQualification {
    uintptr_t session = 0, rows = 0, world = 0;
    unsigned int local = 0, first_handle = 0, second_handle = 0, turn = 0;
    unsigned int own_row = 0, peer_row = 0;
    bool departed = false;
};
GoldNetworkQualification network_qualification{};
struct GoldNetworkRowsView {
    uintptr_t setup=0,reference=0,rows=0;
    unsigned char mode=0;
};
// Select a bounded original preparation view only. The caller independently
// authorizes mixed-world ownership before requesting the active-buffer view.
bool NetworkPlayingRows(uintptr_t setup,bool mixed,GoldNetworkRowsView& result){
    result={};
    if(!ValidRange(reinterpret_cast<const void*>(setup),0x1ac))return false;
    unsigned char mode=0;uintptr_t offset=0;
    if(mixed){
        if(*reinterpret_cast<const uintptr_t*>(setup)!=image+0xb51af0)return false;
        mode=*reinterpret_cast<const unsigned char*>(setup+4);if(mode>1)return false;
        // Playing data uses the selected buffer. The preparation writers read
        // its opposite buffer while assembling the next game configuration.
        offset=mode*0xd4;
    }
    const auto reference=*reinterpret_cast<const uintptr_t*>(setup+offset+0xa4);
    if(!ValidRange(reinterpret_cast<const void*>(reference),sizeof(uintptr_t)))return false;
    const auto rows=*reinterpret_cast<const uintptr_t*>(reference);
    if(!ValidRange(reinterpret_cast<const void*>(rows),25*0x5c4))return false;
    result={setup,reference,rows,mode};return true;
}
bool NetworkLocalReadyAllowed() {
#ifdef S7_GOLD_NATIVE_SESSION_PROVIDER
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
    bool mixed=false;
#ifdef S7_PLATFORM_INDEPENDENT
    const auto* launch=CrossplayProduction::CurrentLaunch();
    if(launch && launch->mixed){
        if(!CrossplayProduction::GoldWorldOwnsStartedSession(session))return false;
        mixed=true;
    }
#endif
    GoldNetworkRowsView view;
    if(!NetworkPlayingRows(setup,mixed,view))return false;
    const uintptr_t rows=view.rows;
    unsigned int humans = 0, computers = 0, indices = 0, own = 0, own_row = 0, peer_row = 0;
    unsigned int admitted_ai=0;
    for (unsigned int row = 1; row <= 24; ++row) {
        const uintptr_t player = rows + row * 0x5c4;
        const auto kind = *reinterpret_cast<unsigned int*>(player + 0x64);
        if (kind != 1 && kind != 2) continue;
#ifdef S7_PLATFORM_INDEPENDENT
        if(mixed && kind==2 && CrossplayProduction::GoldWorldAIPlayer(session,rows,row)){
            admitted_ai|=1u<<row;continue;
        }
#endif
        if (kind == 2 && !local_peer_leave_enabled) return false;
        if (*reinterpret_cast<unsigned int*>(player + 0x60) != 3) return false;
        const auto index = *reinterpret_cast<unsigned int*>(player + 0x5c);
        if ((index != 1 && index != 2) || (indices & (1u << index))) return false;
        indices |= 1u << index;
        if (kind == 1) ++humans; else ++computers;
        if (index == local) {
            if (kind != 1) return false;
            ++own; own_row = row;
        } else peer_row = row;
    }
    if (indices != 6 || own != 1 || humans + computers != 2) return false;
#ifdef S7_PLATFORM_INDEPENDENT
    if(mixed && admitted_ai!=CrossplayProduction::GoldWorldExpectedAIPlayers(session))return false;
#endif
#ifdef S7_PLATFORM_INDEPENDENT
    if(mixed){
        GoldNetworkRowsView after;
        if(*reinterpret_cast<const uintptr_t*>(image+0xcf834c)!=setup
            || !CrossplayProduction::GoldWorldOwnsStartedSession(session)
            || !NetworkPlayingRows(setup,true,after) || after.setup!=view.setup || after.reference!=view.reference
            || after.rows!=view.rows || after.mode!=view.mode)return false;
    }
#endif
    if (!local_peer_leave_enabled) return humans == 2;
    const uintptr_t world = *reinterpret_cast<uintptr_t*>(image + 0xcf83f0);
    if (!ValidRange(reinterpret_cast<void*>(world), 0x30)
        || *reinterpret_cast<uintptr_t*>(world) != image + 0xb32a50) return false;
    const auto turn = *reinterpret_cast<unsigned int*>(world + 0x2c);
    if (!turn || turn == 0xffffffffu) return false;
    const auto first = *reinterpret_cast<unsigned int*>(session + 0x3c);
    const auto second = *reinterpret_cast<unsigned int*>(session + 0x40);
    const bool same = network_qualification.session == session
        && network_qualification.rows == rows && network_qualification.world == world
        && network_qualification.local == local && network_qualification.first_handle == first
        && network_qualification.second_handle == second && network_qualification.own_row == own_row
        && network_qualification.peer_row == peer_row && turn >= network_qualification.turn;
    if (humans == 2) {
        // A new connection or match after departure needs a fresh process;
        // changed handles must not turn an unqualified reconnect into admission.
        if (network_qualification.departed) return false;
        network_qualification = {session, rows, world, local, first, second, turn, own_row, peer_row, false};
        return true;
    }
    if (!same || humans != 1 || computers != 1) return false;
    network_qualification.turn = turn;
    network_qualification.departed = true;
    return true;
#else
    return false;
#endif
}
