// Explicit experimental wire build only. The existing Storm transport carries
// one bounded extra cosmetic field; no new sockets or service ports are opened.
std::mutex castle_cache_mutex;
struct CachedCastle { bool occupied = false; uint32_t player = 0; ULONGLONG at = 0; castle_wire::Design design; };
std::array<CachedCastle, 8> castle_cache;
std::atomic<uint32_t> own_castle_player{UINT32_MAX};

void CacheCastle(uint32_t player, const castle_wire::Design& design) {
    if (player == UINT32_MAX || !castle_wire::Valid(design)) return;
    std::lock_guard<std::mutex> lock(castle_cache_mutex);
    auto found = castle_cache.begin();
    for (auto i = castle_cache.begin(); i != castle_cache.end(); ++i) {
        if (i->occupied && i->player == player) { found = i; break; }
        if (i->at < found->at) found = i;
    }
    *found = {true, player, GetTickCount64(), design};
}
bool FindCastle(uint32_t player, castle_wire::Design& design) {
    std::lock_guard<std::mutex> lock(castle_cache_mutex);
    for (const auto& item : castle_cache) if (item.occupied && item.player == player) {
        design = item.design; return true;
    }
    return false;
}
bool ReadCastleAsset(uint32_t address, std::string& result) {
    uint32_t words[4]{};
    if (!ReadOwnCastleMemory(address, words, sizeof(words)) || !words[2] || words[2] > castle_wire::MaxAsset
        || words[1] <= words[2] || words[0] > UINT32_MAX - words[2]) return false;
    result.resize(words[2]);
    return ReadOwnCastleMemory(words[0], &result[0], result.size()) && castle_wire::Asset(result);
}
bool SnapshotCastle(uint32_t castle, castle_wire::Design& design) {
    design = {};
    if (!castle) return true; // Explicit no-castle selection.
    uint32_t begin = 0, end = 0, parents = 0, slots = 0;
    if (!ReadCastleAsset(castle + 4, design.type)
        || !ReadOwnCastleMemory(castle + 0x24, &begin, 4) || !ReadOwnCastleMemory(castle + 0x28, &end, 4)
        || !ReadOwnCastleMemory(castle + 0x30, &parents, 4) || !ReadOwnCastleMemory(castle + 0x3c, &slots, 4)
        || end < begin || (end - begin) % 16 || (end - begin) / 16 > castle_wire::MaxParts) return false;
    design.parts.resize((end - begin) / 16);
    for (uint32_t i = 0; i < design.parts.size(); ++i)
        if (!ReadCastleAsset(begin + i * 16, design.parts[i].asset)
            || !ReadOwnCastleMemory(parents + i * 4, &design.parts[i].parent, 4)
            || !ReadOwnCastleMemory(slots + i * 4, &design.parts[i].slot, 4)) return false;
    return castle_wire::Valid(design);
}
struct BorrowedCastleString { const char* bytes; uint32_t capacity, length, reserved; };
struct BorrowedCastleVector { const void* begin; const void* end; const void* capacity; };
struct BorrowedCastle {
    uintptr_t type; BorrowedCastleString castle_type, name;
    BorrowedCastleVector parts, parents, slots;
};
static_assert(sizeof(BorrowedCastle) == 0x48 && sizeof(BorrowedCastleString) == 16, "native castle source ABI");
struct CastleView {
    BorrowedCastle value{}; std::vector<BorrowedCastleString> parts; std::vector<uint32_t> parents, slots;
    explicit CastleView(const castle_wire::Design& design) {
        auto text = [](const std::string& s) { return BorrowedCastleString{s.c_str(), static_cast<uint32_t>(s.size() + 1), static_cast<uint32_t>(s.size()), 0}; };
        static const std::string empty;
        value.type = NativeVA(0x11372D0); value.castle_type = text(design.type); value.name = text(empty);
        for (const auto& p : design.parts) { parts.push_back(text(p.asset)); parents.push_back(p.parent); slots.push_back(p.slot); }
        if (!parts.empty()) {
            value.parts = {parts.data(), parts.data() + parts.size(), parts.data() + parts.size()};
            value.parents = {parents.data(), parents.data() + parents.size(), parents.data() + parents.size()};
            value.slots = {slots.data(), slots.data() + slots.size(), slots.data() + slots.size()};
        }
    }
};

struct JoinCastle { const void* message = nullptr; ULONGLONG at = 0; castle_wire::Design design; };
std::array<JoinCastle, 32> join_castles;
thread_local castle_wire::Design pending_join_castle;
thread_local bool pending_join_castle_valid = false;
void SaveJoinCastle(const void* message, const castle_wire::Design* design) {
    if (!message) return;
    std::lock_guard<std::mutex> lock(castle_cache_mutex);
    auto found = join_castles.begin();
    for (auto i = join_castles.begin(); i != join_castles.end(); ++i) {
        if (i->message == message) { found = i; break; }
        if (i->at < found->at) found = i;
    }
    *found = design ? JoinCastle{message, GetTickCount64(), *design} : JoinCastle{};
}
bool GetJoinCastle(const void* message, castle_wire::Design& design) {
    if (!message) return false;
    std::lock_guard<std::mutex> lock(castle_cache_mutex);
    for (const auto& item : join_castles) if (item.message == message) { design = item.design; return true; }
    return false;
}
struct CastleWireContext { uint32_t player; const void* joining_message = nullptr; };
struct CastleWireField { const uintptr_t* type; uint32_t touched; bool* global_touched; CastleWireContext* context; };
struct CastleWireResult { int32_t error; uint32_t detail; };
void ReportCastleWire(unsigned int bit, const char* label) {
    static std::atomic<unsigned int> reported{0};
    if (!(reported.fetch_or(bit) & bit)) ReportLocalStatus(label);
}
CastleWireResult* __fastcall WriteCastleField(CastleWireField* field, void*, CastleWireResult* result, void* stream) {
    castle_wire::Design design;
    if (field->context->joining_message) {
        if (!GetJoinCastle(field->context->joining_message, design)) {
            *result = {static_cast<int32_t>(0xfff0bdc0), 0}; return result;
        }
    } else FindCastle(field->context->player, design);
    std::vector<unsigned char> bytes;
    if (!castle_wire::Encode(design, bytes)) castle_wire::Encode({}, bytes);
    using Write = bool (__thiscall*)(void*, uint32_t);
    bool okay = Native<Write>(0x6679E0)(stream, static_cast<uint32_t>(bytes.size()));
    for (size_t i = 0; okay && i < bytes.size(); i += 4) {
        uint32_t word = 0;
        for (size_t j = 0; j < 4 && i + j < bytes.size(); ++j) word |= static_cast<uint32_t>(bytes[i + j]) << (j * 8);
        okay = Native<Write>(0x6679E0)(stream, word);
    }
    if (okay && !design.parts.empty()) ReportCastleWire(1, "local-castle-wire-design-written");
    *result = {okay ? 1 : static_cast<int32_t>(0xfff0bdc0), 0}; return result;
}
CastleWireResult* __fastcall ReadCastleField(CastleWireField* field, void*, CastleWireResult* result, void* stream) {
    using Read = bool (__thiscall*)(void*, uint32_t*);
    uint32_t length = 0;
    bool okay = Native<Read>(0x667A10)(stream, &length) && length >= 12 && length <= castle_wire::Limit;
    std::vector<unsigned char> bytes(okay ? length : 0);
    for (size_t i = 0; okay && i < bytes.size(); i += 4) {
        uint32_t word = 0; okay = Native<Read>(0x667A10)(stream, &word);
        for (size_t j = 0; j < 4 && i + j < bytes.size(); ++j) bytes[i + j] = static_cast<unsigned char>(word >> (j * 8));
    }
    castle_wire::Design design;
    okay = okay && castle_wire::Decode(bytes, design);
    if (field->context->joining_message) SaveJoinCastle(field->context->joining_message, okay ? &design : nullptr);
    // IDs are assigned by the current session. A reused client may still have
    // its previous local ID while it receives the new host's player list.
    // Local rendering independently reads the selected local profile.
    else if (okay) CacheCastle(field->context->player, design);
    ReportCastleWire(okay ? 2 : 4, okay ? "local-castle-wire-design-read" : "local-castle-wire-design-rejected");
    *result = {okay ? 1 : static_cast<int32_t>(0xfff0bdc0), 0}; return result;
}
void __fastcall VisitCastlePlayerField(void* self, void*, void* visitor) {
    const DWORD incoming_error = GetLastError();
    using Original = void (__thiscall*)(void*, void*);
    Native<Original>(0x667E30)(self, visitor);
    const DWORD result_error = GetLastError();
    // This single callsite is the final field of StormPlayerDataMessage.
    auto* message = static_cast<unsigned char*>(self) - 0xd8;
    if (*reinterpret_cast<uintptr_t*>(message) == NativeVA(0x113F5DC)) {
        const auto first = *reinterpret_cast<uint32_t*>(message + 0x34);
        const auto fifth = *reinterpret_cast<uint32_t*>(message + 0x64);
        const auto own = own_castle_player.load();
        if (own != UINT32_MAX) {
            if (first == own) ReportCastleWire(32, "local-castle-wire-first-id-matches-local");
            if (fifth == own) ReportCastleWire(64, "local-castle-wire-fifth-id-matches-local");
            if (!own) ReportCastleWire(128, "local-castle-wire-zero-local-id");
        }
        CastleWireContext context{first};
        uintptr_t table[7]; memcpy(table, reinterpret_cast<const void*>(NativeVA(0x1086D9C)), sizeof(table));
        table[4] = reinterpret_cast<uintptr_t>(&WriteCastleField); table[5] = reinterpret_cast<uintptr_t>(&ReadCastleField);
        bool touched = true;
        CastleWireField field{table, 1, &touched, &context};
        SetLastError(incoming_error);
        Native<Original>(0xCB16D0)(&field, visitor);
    }
    SetLastError(result_error);
}

void* __fastcall ConstructCastleJoinMessage(void* self, void*) {
    const auto incoming = GetLastError();
    // Native message storage can be reused for a later join. A newly
    // constructed request must not inherit an older received/copied sidecar.
    SaveJoinCastle(self, nullptr);
    castle_wire::Design design;
    bool captured = false;
    if (GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && initialization == 2 && local_connect_probe) {
        const castle_profile::Layout layout{static_cast<uint32_t>(NativeVA(0x13A5228)),
            static_cast<uint32_t>(NativeVA(0x11372B8)), static_cast<uint32_t>(NativeVA(0x113729C)),
            static_cast<uint32_t>(NativeVA(0x11372D0))};
        std::vector<unsigned char> encoded;
        if (!SnapshotCastle(castle_profile::Selected(layout, ReadOwnCastleMemory), design)
            || !castle_wire::Encode(design, encoded)) design = {};
        captured = true;
    }
    SetLastError(incoming);
    auto* result = Native<void* (__thiscall*)(void*)>(0x668AC0)(self);
    const auto error = GetLastError();
    if (captured) SaveJoinCastle(self, &design);
    SetLastError(error);
    return result;
}
void __fastcall VisitCastleJoinMessage(void* self, void*, void* visitor) {
    using Visit = void (__thiscall*)(void*, void*);
    Native<Visit>(0x668060)(self, visitor);
    const auto error = GetLastError();
    if (*static_cast<uintptr_t*>(self) == NativeVA(0x113F5F4)) {
        CastleWireContext context{0, self};
        uintptr_t table[7]; memcpy(table, reinterpret_cast<const void*>(NativeVA(0x1086D9C)), sizeof(table));
        table[4] = reinterpret_cast<uintptr_t>(&WriteCastleField); table[5] = reinterpret_cast<uintptr_t>(&ReadCastleField);
        bool touched = true; CastleWireField field{table, 1, &touched, &context};
        Native<Visit>(0xCB16D0)(&field, visitor);
    }
    SetLastError(error);
}
void __fastcall CopyCastleJoinMessage(void* self, void*, const void* source) {
    const auto incoming = GetLastError();
    castle_wire::Design design; const bool found = GetJoinCastle(source, design);
    SetLastError(incoming);
    Native<void (__thiscall*)(void*, const void*)>(0x6680A0)(self, source);
    const auto error = GetLastError(); SaveJoinCastle(self, found ? &design : nullptr); SetLastError(error);
}
void __fastcall ExtractCastleJoinMessage(void* self, void*, void* name, void* avatar) {
    const auto incoming = GetLastError();
    pending_join_castle = {}; pending_join_castle_valid = GetJoinCastle(self, pending_join_castle);
    SetLastError(incoming);
    Native<void (__thiscall*)(void*, void*, void*)>(0x6680F0)(self, name, avatar);
}
void __fastcall DispatchCastleJoin(void* self, void*, const char* name, uint32_t player, uint32_t avatar) {
    const auto incoming = GetLastError();
    if (pending_join_castle_valid) CacheCastle(player, pending_join_castle);
    pending_join_castle_valid = false; pending_join_castle = {};
    SetLastError(incoming);
    Native<void (__thiscall*)(void*, const char*, uint32_t, uint32_t)>(0xEC9EC0)(self, name, player, avatar);
}
