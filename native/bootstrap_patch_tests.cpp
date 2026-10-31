// Exercise the actual patch writer/validator against owned synthetic memory.
// No game file is loaded and no native game function is ever called.
#include "offline_bootstrap.cpp"
#include <algorithm>
#include <cstdio>
std::vector<const char*> diagnostic_events;
void ReportLocalStatus(const char* event) { diagnostic_events.push_back(event); SetLastError(12345); }
namespace {
#ifdef S7_CASTLE_WIRE_PROTOTYPE
struct CastleTestStream { std::vector<uint32_t> words; size_t at = 0; };
bool __fastcall FakeCastleWrite(CastleTestStream* stream, void*, uint32_t word) { stream->words.push_back(word); return true; }
bool __fastcall FakeCastleRead(CastleTestStream* stream, void*, uint32_t* word) {
    if (stream->at == stream->words.size()) return false;
    *word = stream->words[stream->at++]; return true;
}
uintptr_t castle_join_words[4]{};
DWORD castle_join_incoming_error = 0;
void* __fastcall FakeCastleJoinConstructor(void* self, void*) {
    castle_join_words[0] = reinterpret_cast<uintptr_t>(self);
    castle_join_incoming_error = GetLastError(); SetLastError(6789); return self;
}
void __fastcall FakeCastleCopy(void* self, void*, const void* source) {
    castle_join_words[0] = reinterpret_cast<uintptr_t>(self); castle_join_words[1] = reinterpret_cast<uintptr_t>(source);
    castle_join_incoming_error = GetLastError(); SetLastError(6789);
}
void __fastcall FakeCastleExtract(void* self, void*, void* name, void* avatar) {
    castle_join_words[0] = reinterpret_cast<uintptr_t>(self); castle_join_words[1] = reinterpret_cast<uintptr_t>(name);
    castle_join_words[2] = reinterpret_cast<uintptr_t>(avatar); castle_join_incoming_error = GetLastError(); SetLastError(6789);
}
void __fastcall FakeCastleDispatch(void* self, void*, const char* name, uint32_t player, uint32_t avatar) {
    castle_join_words[0] = reinterpret_cast<uintptr_t>(self); castle_join_words[1] = reinterpret_cast<uintptr_t>(name);
    castle_join_words[2] = player; castle_join_words[3] = avatar;
    castle_join_incoming_error = GetLastError(); SetLastError(6789);
}
#endif
#ifdef S7_CASTLE_PROTOTYPE
uintptr_t castle_constructor_words[7]{};
DWORD castle_constructor_incoming_error = 0;
#ifdef S7_CASTLE_WIRE_PROTOTYPE
castle_wire::Design castle_constructor_design;
bool castle_constructor_design_valid = false;
std::string castle_constructor_name;
#endif
void* __fastcall FakeCastlePlayer(void* self, void*, unsigned int kind, unsigned int player,
    const void* setup, bool local, const void* castle, unsigned int extra) {
    const uintptr_t words[] = {reinterpret_cast<uintptr_t>(self), kind, player,
        reinterpret_cast<uintptr_t>(setup), local, reinterpret_cast<uintptr_t>(castle), extra};
    memcpy(castle_constructor_words, words, sizeof(words));
    castle_constructor_incoming_error = GetLastError();
#ifdef S7_CASTLE_WIRE_PROTOTYPE
    castle_constructor_design_valid = SnapshotCastle(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(castle)), castle_constructor_design);
    BorrowedCastle copied{};
    if (ReadOwnCastleMemory(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(castle)), &copied, sizeof(copied))) {
        if (!copied.castle_type.length && copied.parts.begin == copied.parts.end
            && copied.parents.begin == copied.parents.end && copied.slots.begin == copied.slots.end) {
            castle_constructor_design = {}; castle_constructor_design_valid = true;
        }
    }
    castle_constructor_name.clear();
    ReadCastleAsset(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(castle)) + 0x14, castle_constructor_name);
#endif
    SetLastError(6789); return reinterpret_cast<void*>(0x12345678);
}
#endif
#ifdef S7_LOCAL_PAIR_LAB
bool lab_interface_original = false;
bool __cdecl FakeLocalInterface(const void*, const char**) {
    SetLastError(6789); return lab_interface_original;
}
#endif
#ifdef S7_LOCAL_NETWORK_PUMP
void* turn_setter_self = nullptr;
unsigned int turn_setter_value = 0;
void __fastcall FakeTurnOffsetSetter(void* self, void*, unsigned int value) {
    turn_setter_self = self; turn_setter_value = value;
    *reinterpret_cast<unsigned int*>(static_cast<unsigned char*>(self) + 0x28) = value;
    SetLastError(6789);
}
uintptr_t lab_join_arguments[6]{};
int lab_join_code = 1;
const char* lab_rejection_literal = nullptr;
void* lab_dispatch_arguments[3]{};
void* lab_echo_self = nullptr;
unsigned int lab_echo_phase = 0;
int lab_echo_timeout = 0;
unsigned int lab_reason_length = 0;
unsigned char* pump_scheduler = nullptr;
unsigned int pending_core_tasks = 0;
uintptr_t lab_ticket_words[8]{};
uintptr_t lab_ticket_return = 0;
uintptr_t __fastcall FakeTicketWords(void* self, void*, uintptr_t a, uintptr_t b, uintptr_t c,
    uintptr_t d, uintptr_t e, uintptr_t f, uintptr_t g) {
    const uintptr_t words[] = {reinterpret_cast<uintptr_t>(self), a, b, c, d, e, f, g};
    memcpy(lab_ticket_words, words, sizeof(words)); SetLastError(6789); return lab_ticket_return;
}
NativeResult* __fastcall FakeTicketResultWords(void* self, void*, NativeResult* result, uintptr_t b, uintptr_t c,
    uintptr_t d, uintptr_t e, uintptr_t f, uintptr_t g) {
    FakeTicketWords(self, nullptr, reinterpret_cast<uintptr_t>(result), b, c, d, e, f, g);
    result->code = lab_join_code; result->detail = 123; return result;
}
void* __fastcall FakeTicketLookup(void* self, void*, uintptr_t reference) {
    lab_ticket_words[0]=reinterpret_cast<uintptr_t>(self); lab_ticket_words[1]=reference;
    SetLastError(6789); return reinterpret_cast<void*>(lab_ticket_return);
}
void __fastcall FakeHostJoinGate(void* self, void*, void* event) {
    lab_dispatch_arguments[0]=self; lab_dispatch_arguments[1]=event; SetLastError(6789);
}
NativeResult* __fastcall FakeHostEventRoute(void* self, void*, NativeResult* result,
    uintptr_t context, void* event, uintptr_t destination) {
    lab_ticket_words[0]=reinterpret_cast<uintptr_t>(self);
    lab_ticket_words[1]=context; lab_ticket_words[2]=reinterpret_cast<uintptr_t>(event);
    lab_ticket_words[3]=destination;
    result->code=lab_join_code; result->detail=123; SetLastError(6789); return result;
}
bool __fastcall FakeHostEventType(void* event, void*, uintptr_t left, uintptr_t right) {
    lab_ticket_words[0]=reinterpret_cast<uintptr_t>(event);
    lab_ticket_words[1]=left; lab_ticket_words[2]=right;
    SetLastError(6789); return lab_join_code == 1;
}
void* __fastcall FakePlayerReason(void* output, void*, const char* text, unsigned int length) {
    lab_rejection_literal = text; lab_reason_length = length; SetLastError(6789); return output;
}
NativeResult* __fastcall FakeEchoDispatcher(void* self, void*, NativeResult* result, unsigned int phase, int timeout) {
    lab_echo_self = self; lab_echo_phase = phase; lab_echo_timeout = timeout;
    result->code = 2; result->detail = 654; SetLastError(6789); return result;
}
NativeResult* __fastcall FakeSessionDispatch(void* self, void*, NativeResult* result, void* event) {
    lab_dispatch_arguments[0] = self; lab_dispatch_arguments[1] = result; lab_dispatch_arguments[2] = event;
    result->code = lab_join_code; result->detail = 987; SetLastError(6789); return result;
}
void* __fastcall FakeLabDiagnosticCtor(void* output, void*, const char* text) {
    lab_rejection_literal = text; SetLastError(6789); return output;
}
NativeResult* __stdcall FakeLabJoinRequest(NativeResult* result, void* peer, void* session,
    void* metadata, void* job, void* callback, unsigned int flags) {
    lab_join_arguments[0] = reinterpret_cast<uintptr_t>(peer);
    lab_join_arguments[1] = reinterpret_cast<uintptr_t>(session);
    lab_join_arguments[2] = reinterpret_cast<uintptr_t>(metadata);
    lab_join_arguments[3] = reinterpret_cast<uintptr_t>(job);
    lab_join_arguments[4] = reinterpret_cast<uintptr_t>(callback);
    lab_join_arguments[5] = flags;
    result->code = lab_join_code; result->detail = 123;
    SetLastError(6789);
    return result;
}
#endif
std::vector<char> pump_trace;
unsigned char* pump_network = nullptr;
unsigned char* pump_owner = nullptr;
int pump_mode = 0;
void ChangeTestFpControl() {
    unsigned short changed = 0;
    __asm fnstcw changed
    changed ^= 0x400; // Change x87 rounding only; no exception masks.
    __asm fldcw changed
    _mm_setcsr(_mm_getcsr() ^ 0x2000u); // Independent SSE rounding.
}
#ifdef S7_LOCAL_NETWORK_PUMP
NativeResult* __fastcall FakeCoreBoundary(void*, void*, NativeResult* result, unsigned int phase) {
    pump_trace.push_back(phase == 1 ? 'P' : phase == 2 ? 'Q' : '!');
    PumpOfflineBootstrap();
    if (pump_mode == 16 && phase == 1) *Native<unsigned char**>(0x1379E14) = nullptr;
    if (pump_mode == 17 && phase == 2) *reinterpret_cast<void**>(pump_network + 0x2c) = nullptr;
    if (pump_mode == 18 && phase == 1) pump_network[0x30] = 0;
    result->code = 1; result->detail = 0; return result;
}
NativeResult* __cdecl FakeEchoUpdate(NativeResult* result, unsigned int phase, int timeout) {
    pump_trace.push_back(phase == 3 && timeout == -1 ? 'E' : '!');
    if (pump_active) PumpOfflineBootstrap();
    if (pump_mode == 13) *Native<unsigned char**>(0x1379E14) = nullptr;
    if (pump_mode == 14) *reinterpret_cast<void**>(pump_network + 0x2c) = nullptr;
    result->code = 2; result->detail = 78; return result;
}
void __fastcall FakeManagerUpdate(void*, void*) {
    pump_trace.push_back('J');
    PumpOfflineBootstrap();
    if (pump_mode == 11) *Native<unsigned char**>(0x1379E14) = nullptr;
    if (pump_mode == 12) *reinterpret_cast<void**>(pump_owner + 0x100) = pump_owner;
}
void TickFakeManager() {
    UpdateLabManager(*reinterpret_cast<void**>(pump_owner + 0x100), nullptr);
}
#endif
int network_getter_calls = 0;
LegacyOrbitApi* legacy_test_api = nullptr;
LegacyOrbitApi* __cdecl FakeLegacyOrbitGetter() { SetLastError(6789); return legacy_test_api; }
void* owner_update_self = nullptr;
DWORD owner_update_thread = 0;
int search_space_calls = 0;
int search_space_mode = 0;
char search_space_copy[129]{};
void __fastcall FakeSetSearchSpace(void* self, void*, const NativeSmallString* input) {
    ++search_space_calls;
    auto* output = reinterpret_cast<NativeSmallString*>(static_cast<unsigned char*>(self) + 0x10);
    *output = *input;
    if (input->capacity >= 16) {
        memcpy(search_space_copy, SmallStringData(*input), input->length + 1);
        const char* copy = search_space_copy;
        memcpy(output->inline_data, &copy, sizeof(copy));
    }
    if (search_space_mode == 1) *Native<void**>(0x1379E14) = nullptr;
    if (search_space_mode == 2) output->inline_data[0] = '?';
}
void* __cdecl FakeNetworkGetter() { ++network_getter_calls; return pump_network; }
unsigned int nested_connect_jobs = 0;
DWORD nested_connect_thread = 0;
NativeResult nested_connect_result{};
void __fastcall FakeNestedConnectJob(void*, void*) {
    ++nested_connect_jobs;
    nested_connect_thread = GetCurrentThreadId();
}
void __fastcall FakeOwnerUpdate(void* self, void*) {
    pump_trace.push_back('O');
    owner_update_self = self;
    owner_update_thread = GetCurrentThreadId();
    if (pump_mode == 8) *Native<unsigned char**>(0x1379E14) = nullptr;
    if (pump_mode == 19) SetLastError(6789);
    auto* owner = static_cast<unsigned char*>(self);
    if (pump_mode == 21 && !owner[0x24] && owner[0x248]) {
        // Original C956E0..C956FA reaches patched C956F5 even when the outer
        // UpdateLocalGame wrapper forwarded this update on a foreign thread.
        RequestLocalGame(owner, nullptr, &nested_connect_result);
        owner[0x248] = 0; // Preserve that original callback's own work.
    }
    if (pump_mode == 22) {
        auto* game = owner + 0x50;
        // Original C958A2..C958CF after its unchanged retry timer is due.
        if (game[0x28] && !game[0x38] && !game[0x48])
            ConnectLocalGame(game, nullptr, &nested_connect_result);
    }
}
struct ForeignOwnerUpdate {
    void* owner;
    DWORD thread = 0;
    DWORD error = 0;
    unsigned int calls = 0;
};
DWORD WINAPI RunForeignOwnerUpdates(void* argument) {
    auto* run = static_cast<ForeignOwnerUpdate*>(argument);
    run->thread = GetCurrentThreadId();
    for (unsigned int i = 0; i < run->calls; ++i) {
        SetLastError(22222);
        UpdateLocalGame(run->owner, nullptr);
        run->error = GetLastError();
    }
    return 0;
}
DWORD WINAPI RunForeignPoisonedConnect(void*) {
    // Both entries must reject before reading self or any other bridge state.
    auto* poisoned = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    NativeResult output{123, 456};
    if (ConnectLocalGame(poisoned, nullptr, &output) != &output ||
        output.code != LocalConnectUnavailable || output.detail != 0) return 1;
    output = {123, 456};
    if (RequestLocalGame(poisoned, nullptr, &output) != &output ||
        output.code != LocalConnectUnavailable || output.detail != 0) return 1;
    return 0;
}
DWORD WINAPI RunForeignDesyncObservations(void* argument) {
    auto* count = static_cast<unsigned int*>(argument);
    for (unsigned int i = 0; i < 10000; ++i) {
        unsigned int events = 99;
        if (!desync_diagnostics.Begin(bootstrap_thread, GetCurrentThreadId(), i,
                ReadDesyncFpControl(), events) && events == 0) ++*count;
    }
    return 0;
}
void __fastcall FakeMasterUpdate(void*, void*) {
    pump_trace.push_back('M');
    PumpOfflineBootstrap(); // Recursive bridge entry must be ignored.
    if (pump_mode == 2) *Native<unsigned char**>(0x1379E14) = nullptr;
    if (pump_mode == 3) *reinterpret_cast<void**>(pump_network + 0xa18) = nullptr;
    if (pump_mode == 4) *reinterpret_cast<void**>(pump_network + 0x2c) = nullptr;
#ifdef S7_LOCAL_NETWORK_PUMP
    if (pump_mode == 9) TickFakeManager();
    if (pump_mode == 15) { NativeResult result{}; UpdateLabEcho(&result, 3, -1); }
#endif
}
NativeResult* __fastcall FakeCoreUpdate(void*, void*, NativeResult* result, unsigned int phase) {
    pump_trace.push_back(phase == 3 ? 'C' : '!');
#ifdef S7_LOCAL_NETWORK_PUMP
    if (pump_scheduler) ++*reinterpret_cast<unsigned int*>(pump_scheduler + 0x78);
    pending_core_tasks = 0;
#endif
    if (pump_mode == 1 || pump_mode == 8) UpdateLocalGame(pump_owner, nullptr);
    if (pump_mode == 5) *Native<unsigned char**>(0x1379E14) = nullptr;
    if (pump_mode == 6) *reinterpret_cast<void**>(pump_network + 0xa18) = nullptr;
    if (pump_mode == 7) *reinterpret_cast<void**>(pump_network + 0x2c) = nullptr;
    if (pump_mode == 20) ChangeTestFpControl();
#ifdef S7_LOCAL_NETWORK_PUMP
    if (pump_mode == 10) TickFakeManager();
#endif
    result->code = 1; result->detail = 0;
    return result;
}
bool FakeJump(uintptr_t address, const void* target) {
    auto* bytes = Native<unsigned char*>(address);
    DWORD old = 0;
    if (!VirtualProtect(bytes, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    bytes[0] = 0xe9;
    uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)) -
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bytes) + 5);
    memcpy(bytes + 1, &relative, 4);
    return FlushInstructionCache(GetCurrentProcess(), bytes, 5) != FALSE;
}
}
int main() {
    int failures = 0, checks = 0;
    auto check = [&](bool result, int line = 0) { ++checks; if (!result) { ++failures; std::printf("Failed check %d (line %d)\n", checks, line); } };
#define check(...) check(__VA_ARGS__, __LINE__)
    {
        DesyncDiagnostics state;
        const DesyncFpControl normal{0x23f, 0x1f80};
        unsigned int events = 0;
        check(!state.Begin(1, 2, 100, normal, events) && !state.started && events == 0);
        check(state.Begin(1, 1, 100, normal, events) && events == DesyncDiagnostics::Started);
        check(state.Observe(DesyncPhase::Core, normal, normal) == 0);
        const DesyncFpControl changed{0x63f, 0x3f80};
        check(state.Observe(DesyncPhase::Core, normal, changed) == 3);
        check(state.Observe(DesyncPhase::Core, normal, changed) == 0);
        check(state.Observe(DesyncPhase::Master, normal, changed) == 3);
        check(state.Begin(1, 1, 1100, changed, events) && events ==
            (DesyncDiagnostics::EntryX87Changed | DesyncDiagnostics::EntrySseChanged | DesyncDiagnostics::PumpGap));
        check(state.Begin(1, 1, 1200, changed, events) && events == 0);
        check(!state.Begin(1, 1, 100 + DesyncDiagnostics::DurationMs, normal, events) && events == DesyncDiagnostics::Finished);
        check(state.Observe(DesyncPhase::Echo, normal, changed) == 0 && state.observations[4] == 0);
        check(!state.Begin(1, 1, 100, normal, events) && events == 0); // Never re-arm.
        state = {};
        check(state.Begin(1, 1, 100, {0x33f, 0x1f80}, events) &&
            events == (DesyncDiagnostics::Started | DesyncDiagnostics::EntryNot53Bit));
        state.pumps = DesyncDiagnostics::MaximumPumps;
        check(!state.Begin(1, 1, 101, normal, events) && events == DesyncDiagnostics::Finished);
        state = {}; check(state.Begin(1, 1, 100, normal, events));
        check(!state.Begin(1, 1, 99, normal, events) && events == DesyncDiagnostics::Finished);
        const auto original = _mm_getcsr();
        const auto before = ReadDesyncFpControl();
        _mm_setcsr(original | 0x01u); // Sticky arithmetic status is not control drift.
        const auto after = ReadDesyncFpControl();
        _mm_setcsr(original);
        check(before.x87 == after.x87 && before.sse == after.sse);
    }
    const char* lab_digests[] = {
        "6dd278274aa885ff402a277f6d2f3ba05600e1ff841462e038f44bf233ce992f",
        "dbc108237301f329fed5372f79915c82acd9085f58d32adefa4efe434208ef72"};
    unsigned char digest[32]{};
    check(!AcceptExecutableDigest(digest));
#ifdef S7_LOCAL_PAIR_LAB
    check(!AcceptExecutableDigest(ExpectedHash));
#else
    check(AcceptExecutableDigest(ExpectedHash));
#endif
    for (int slot = 0; slot < 2; ++slot) {
        for (int i = 0; i < 32; ++i) {
            char hex[3] = { lab_digests[slot][i*2], lab_digests[slot][i*2+1], 0 };
            digest[i] = static_cast<unsigned char>(strtoul(hex, nullptr, 16));
        }
#ifdef S7_LOCAL_PAIR_LAB
        check(AcceptExecutableDigest(digest));
        check(local_pair_bind.load() == 0x7f000002u + slot);
#else
        check(!AcceptExecutableDigest(digest));
#endif
        for (int i = 0; i < 32; ++i) {
            digest[i] ^= 1; check(!AcceptExecutableDigest(digest)); digest[i] ^= 1;
        }
    }
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, ExpectedImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!memory) return 2;
    image_base = reinterpret_cast<uintptr_t>(memory);
    {
        const auto previous_thread = bootstrap_thread.load();
        const auto previous_initialization = initialization;
        const bool previous_probe = local_connect_probe;
        struct GuardedOrbit { uint32_t before; LegacyOrbitApi api; uint32_t after; } fixture{0x11223344, {}, 0x55667788};
        check(EmptyLegacyOrbitApi(&fixture.api, 0));
        check(!EmptyLegacyOrbitApi(nullptr, 0));
        initialization = 2; bootstrap_thread = GetCurrentThreadId(); local_connect_probe = true;
        check(LocalLegacyOrbitContext());
        SetLastError(7654);
        using OrbitInitializer = void (__thiscall*)(LegacyOrbitApi*);
        reinterpret_cast<OrbitInitializer>(&InitializeUnavailableLegacyOrbit)(&fixture.api);
        check(GetLastError() == 7654 && fixture.before == 0x11223344 && fixture.after == 0x55667788);
        check(EmptyLegacyOrbitApi(&fixture.api, 1) && !EmptyLegacyOrbitApi(&fixture.api, 0));
        for (size_t slot = 0; slot < 22; ++slot) {
            fixture.api.functions[slot] = reinterpret_cast<void*>(1);
            check(!EmptyLegacyOrbitApi(&fixture.api, 1)); fixture.api.functions[slot] = nullptr;
        }
        fixture.api.module = reinterpret_cast<void*>(1); check(!EmptyLegacyOrbitApi(&fixture.api, 1));
        fixture.api.module = nullptr; fixture.api.unavailable = 2; check(!EmptyLegacyOrbitApi(&fixture.api, 1));
        local_connect_probe = false; check(!LocalLegacyOrbitContext()); local_connect_probe = true;
        bootstrap_thread = GetCurrentThreadId() + 1; check(!LocalLegacyOrbitContext());
        bootstrap_thread = GetCurrentThreadId(); initialization = 1; check(!LocalLegacyOrbitContext());
        initialization = 2; steam_history = true; check(LocalLegacyOrbitContext()); steam_history = false;
        for (bool steam : {false, true}) {
            steam_history = steam; fixture.api = {}; legacy_test_api = &fixture.api;
            *Native<LegacyOrbitApi**>(0x13A570C) = legacy_test_api;
            check(FakeJump(0x4C1220, reinterpret_cast<const void*>(&FakeLegacyOrbitGetter)));
            SetLastError(7654);
            reinterpret_cast<OrbitInitializer>(&InitializeUnavailableLegacyOrbit)(&fixture.api);
            check(GetLastError() == 7654 && EmptyLegacyOrbitApi(&fixture.api, 1));
            check(AllowLocalLegacyOrbitAbsence() && GetLastError() == 7654);
            check(fixture.before == 0x11223344 && fixture.after == 0x55667788);
            *Native<LegacyOrbitApi**>(0x13A570C) = nullptr;
        }
        steam_history = false; legacy_test_api = nullptr;
        bootstrap_thread = previous_thread; initialization = previous_initialization; local_connect_probe = previous_probe;
        // Execute the replacement branch for both predicate values. The true
        // case reaches exactly EC5D27, the original constructor continuation.
        auto* branch = Native<unsigned char*>(0xEC5CF6);
        DWORD old = 0;
        check(VirtualProtect(branch, 50, PAGE_EXECUTE_READWRITE, &old) != FALSE);
        memset(branch, 0xcc, 50); memcpy(branch, LocalLegacyOrbitGate, sizeof(LocalLegacyOrbitGate));
        const unsigned char denied[] = {0x31,0xc0,0xc3};
        const unsigned char allowed[] = {0xb0,0x01,0xc3};
        memcpy(branch + 6, denied, sizeof(denied)); memcpy(branch + 0x31, allowed, sizeof(allowed));
        check(FlushInstructionCache(GetCurrentProcess(), branch, 53) != FALSE);
        using Branch = bool (__fastcall*)(bool);
        // MOV AL,CL supplies the synthetic predicate at the real instruction.
        branch[-2] = 0x8a; branch[-1] = 0xc1;
        check(!reinterpret_cast<Branch>(branch - 2)(false));
        check(reinterpret_cast<Branch>(branch - 2)(true));
    }
#ifdef S7_CASTLE_WIRE_PROTOTYPE
    check(FakeJump(0x6679E0, reinterpret_cast<const void*>(&FakeCastleWrite)));
    check(FakeJump(0x667A10, reinterpret_cast<const void*>(&FakeCastleRead)));
    check(FakeJump(0x668AC0, reinterpret_cast<const void*>(&FakeCastleJoinConstructor)));
    check(FakeJump(0x6680A0, reinterpret_cast<const void*>(&FakeCastleCopy)));
    check(FakeJump(0x6680F0, reinterpret_cast<const void*>(&FakeCastleExtract)));
    check(FakeJump(0xEC9EC0, reinterpret_cast<const void*>(&FakeCastleDispatch)));
    {
        const castle_wire::Design design{"CF_Castle_01", {{"CF_Base_03", UINT32_MAX, 0}, {"CF_Turret_01", 0, 7}}};
        own_castle_player = 101; CacheCastle(101, design);
        CastleWireContext context{101}; bool touched = true;
        CastleWireField field{nullptr, 1, &touched, &context}; CastleWireResult result{}; CastleTestStream stream;
        check(WriteCastleField(&field, nullptr, &result, &stream) == &result && result.error == 1);
        const auto original_words = stream.words;
        context.player = 102;
        check(ReadCastleField(&field, nullptr, &result, &stream) == &result && result.error == 1);
        castle_wire::Design received; check(FindCastle(102, received) && received.type == design.type
            && received.parts.size() == 2 && received.parts[1].asset == "CF_Turret_01");
        CastleView view(received);
        check(view.value.type == NativeVA(0x11372D0) && view.parts.size() == 2
            && view.value.castle_type.length == design.type.size() && view.parents[0] == UINT32_MAX
            && view.slots[1] == 7 && !view.value.name.length);
        CacheCastle(0, design); check(FindCastle(0, received) && received.parts.size() == 2);
        own_castle_player = 102; context.player = 102; stream.words = original_words; stream.at = 0;
        ReadCastleField(&field, nullptr, &result, &stream);
        check(result.error == 1 && FindCastle(102, received) && received.parts.size() == 2);
        own_castle_player = 101;
        check(!GetJoinCastle(nullptr, received));
        SaveJoinCastle(reinterpret_cast<void*>(111), &design);
        SetLastError(4321);
        CopyCastleJoinMessage(reinterpret_cast<void*>(222), nullptr, reinterpret_cast<void*>(111));
        check(GetLastError() == 6789 && castle_join_incoming_error == 4321
            && castle_join_words[0] == 222 && castle_join_words[1] == 111);
        check(GetJoinCastle(reinterpret_cast<void*>(222), received) && received.parts.size() == 2);
        SetLastError(4321);
        ExtractCastleJoinMessage(reinterpret_cast<void*>(222), nullptr, reinterpret_cast<void*>(333), reinterpret_cast<void*>(444));
        check(GetLastError() == 6789 && castle_join_incoming_error == 4321 && pending_join_castle_valid
            && castle_join_words[0] == 222 && castle_join_words[1] == 333 && castle_join_words[2] == 444);
        SetLastError(4321);
        DispatchCastleJoin(reinterpret_cast<void*>(111), nullptr, reinterpret_cast<const char*>(222), 104, 7);
        check(GetLastError() == 6789 && castle_join_incoming_error == 4321 && !pending_join_castle_valid
            && castle_join_words[0] == 111 && castle_join_words[1] == 222 && castle_join_words[2] == 104 && castle_join_words[3] == 7);
        check(FindCastle(104, received) && received.parts.size() == 2);
        SaveJoinCastle(reinterpret_cast<void*>(111), nullptr);
        CopyCastleJoinMessage(reinterpret_cast<void*>(222), nullptr, reinterpret_cast<void*>(111));
        check(!GetJoinCastle(reinterpret_cast<void*>(222), received));
        ExtractCastleJoinMessage(reinterpret_cast<void*>(222), nullptr, nullptr, nullptr);
        DispatchCastleJoin(nullptr, nullptr, nullptr, 105, 0);
        check(!FindCastle(105, received));
        SaveJoinCastle(reinterpret_cast<void*>(222), &design);
        SetLastError(4321);
        check(ConstructCastleJoinMessage(reinterpret_cast<void*>(222), nullptr) == reinterpret_cast<void*>(222)
            && GetLastError() == 6789 && castle_join_incoming_error == 4321 && castle_join_words[0] == 222);
        check(!GetJoinCastle(reinterpret_cast<void*>(222), received));
        for (size_t n = 0; n < original_words.size(); ++n) {
            context.player = 103; stream.words.assign(original_words.begin(), original_words.begin() + n); stream.at = 0;
            check(ReadCastleField(&field, nullptr, &result, &stream) == &result && result.error < 0);
            check(!FindCastle(103, received));
        }
        stream.words = {0xffffffff}; stream.at = 0;
        ReadCastleField(&field, nullptr, &result, &stream); check(result.error < 0 && !FindCastle(103, received));
        for (auto& item : castle_cache) item = {};
        for (auto& item : join_castles) item = {};
        own_castle_player = UINT32_MAX;
    }
#endif
#ifdef S7_CASTLE_PROTOTYPE
    check(FakeJump(0x690C20, reinterpret_cast<const void*>(&FakeCastlePlayer)));
    {
        auto fixture = image_base + 0x60000;
        auto put = [&](uintptr_t at, uint32_t value) { memcpy(reinterpret_cast<void*>(at), &value, 4); };
        auto vec = [&](uintptr_t at, uintptr_t begin, uint32_t bytes) {
            put(at, static_cast<uint32_t>(begin)); put(at + 4, static_cast<uint32_t>(begin + bytes));
            put(at + 8, static_cast<uint32_t>(begin + bytes));
        };
        put(NativeVA(0x13A5228), static_cast<uint32_t>(fixture));
        put(fixture + 4, static_cast<uint32_t>(fixture + 0x100));
        put(fixture + 0x100, static_cast<uint32_t>(NativeVA(0x11372B8)));
        vec(fixture + 0x108, fixture + 0x200, 0x1d0);
        put(fixture + 0x200, static_cast<uint32_t>(NativeVA(0x113729C)));
        vec(fixture + 0x2e8, fixture + 0x400, 0x48);
        put(fixture + 0x400, static_cast<uint32_t>(NativeVA(0x11372D0)));
        vec(fixture + 0x424, fixture + 0x800, 32);
        vec(fixture + 0x430, fixture + 0x900, 8);
        vec(fixture + 0x43c, fixture + 0xa00, 8);
#ifdef S7_CASTLE_WIRE_PROTOTYPE
        auto text = [&](uintptr_t at, const char* value) {
            const auto size = static_cast<uint32_t>(strlen(value));
            put(at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));
            put(at + 4, size + 1); put(at + 8, size); put(at + 12, 0);
        };
        text(fixture + 0x404, "CF_Castle_01");
        text(fixture + 0x414, "local-name-not-transferred");
        text(fixture + 0x800, "CF_Base_03"); text(fixture + 0x810, "CF_Turret_01");
        put(fixture + 0x900, UINT32_MAX); put(fixture + 0x904, 0);
        put(fixture + 0xa00, 0); put(fixture + 0xa04, 7);
#endif
        const auto old_thread = bootstrap_thread.load(); const auto old_initialization = initialization;
        const bool old_probe = local_connect_probe;
        bootstrap_thread = GetCurrentThreadId(); initialization = 2; local_connect_probe = true;
        for (unsigned int kind : {2u, 3u}) for (bool local : {false, true}) {
            SetLastError(4321);
            auto* result = ConstructLocalCastlePlayer(reinterpret_cast<void*>(111), nullptr,
                kind, 222, reinterpret_cast<const void*>(333), local, reinterpret_cast<const void*>(444), 555);
            check(result == reinterpret_cast<void*>(0x12345678) && GetLastError() == 6789);
            check(castle_constructor_incoming_error == 4321);
            check(castle_constructor_words[0] == 111 && castle_constructor_words[1] == kind
                && castle_constructor_words[2] == 222 && castle_constructor_words[3] == 333
                && castle_constructor_words[4] == static_cast<uintptr_t>(local) && castle_constructor_words[6] == 555);
#ifdef S7_CASTLE_WIRE_PROTOTYPE
            check(kind == 3 && local ? castle_constructor_words[5] != 444 : castle_constructor_words[5] == 444);
            if (kind == 3 && local) {
                check(castle_constructor_design_valid && castle_constructor_design.parts.size() == 2);
                check(castle_constructor_name.empty());
                castle_wire::Design cached;
                std::vector<unsigned char> local_bytes, peer_bytes;
                check(FindCastle(222, cached) && castle_wire::Encode(cached, peer_bytes)
                    && castle_wire::Encode(castle_constructor_design, local_bytes) && local_bytes == peer_bytes);
                ConstructLocalCastlePlayer(nullptr, nullptr, 3, 222, nullptr, false, reinterpret_cast<void*>(444), 0);
                check(castle_constructor_design_valid && castle_constructor_name.empty()
                    && castle_wire::Encode(castle_constructor_design, peer_bytes) && local_bytes == peer_bytes);
            }
#else
            check(castle_constructor_words[5] == (kind == 3 && local ? fixture + 0x400 : 444));
#endif
        }
#ifdef S7_CASTLE_WIRE_PROTOTYPE
        // A request retains the profile captured at its own construction even
        // if another request is built before it is serialized or copied.
        ConstructCastleJoinMessage(reinterpret_cast<void*>(321), nullptr);
        text(fixture + 0x810, "CF_Turret_02");
        ConstructCastleJoinMessage(reinterpret_cast<void*>(322), nullptr);
        CopyCastleJoinMessage(reinterpret_cast<void*>(323), nullptr, reinterpret_cast<void*>(321));
        for (uintptr_t id : {321u, 322u, 323u}) {
            CastleWireContext context{0, reinterpret_cast<void*>(id)}; bool touched = true;
            CastleWireField field{nullptr, 1, &touched, &context}; CastleWireResult result{}; CastleTestStream stream;
            WriteCastleField(&field, nullptr, &result, &stream); check(result.error == 1);
            context.joining_message = nullptr; context.player = 223;
            ReadCastleField(&field, nullptr, &result, &stream);
            castle_wire::Design received;
            check(result.error == 1 && FindCastle(223, received) && received.parts.size() == 2
                && received.parts[1].asset == (id == 322 ? "CF_Turret_02" : "CF_Turret_01"));
        }
        CastleWireContext missing{0, reinterpret_cast<void*>(324)}; bool touched = true;
        CastleWireField field{nullptr, 1, &touched, &missing}; CastleWireResult result{}; CastleTestStream stream;
        WriteCastleField(&field, nullptr, &result, &stream); check(result.error < 0 && stream.words.empty());
        // Valid native profile, too large for the bounded field: both sides
        // must see the same empty design, rather than only peers losing it.
        vec(fixture + 0x424, fixture + 0x2000, 512 * 16);
        vec(fixture + 0x430, fixture + 0x5000, 512 * 4);
        vec(fixture + 0x43c, fixture + 0x6000, 512 * 4);
        for (uint32_t i = 0; i < 512; ++i) {
            text(fixture + 0x2000 + i * 16, "CF_Base_03");
            put(fixture + 0x5000 + i * 4, i ? i - 1 : UINT32_MAX);
            put(fixture + 0x6000 + i * 4, 0);
        }
        ConstructLocalCastlePlayer(nullptr, nullptr, 3, 222, nullptr, true, reinterpret_cast<void*>(444), 0);
        castle_wire::Design empty;
        check(castle_constructor_design_valid && castle_constructor_design.parts.empty() && castle_constructor_name.empty());
        check(FindCastle(222, empty) && empty.parts.empty() && empty.type.empty());
        ConstructLocalCastlePlayer(nullptr, nullptr, 3, 222, nullptr, false, reinterpret_cast<void*>(444), 0);
        check(castle_constructor_design_valid && castle_constructor_design.parts.empty() && castle_constructor_name.empty());
        for (auto& item : castle_cache) item = {};
        for (auto& item : join_castles) item = {};
#endif
        put(fixture + 0x260, 0xffffffff);
        ConstructLocalCastlePlayer(nullptr, nullptr, 3, 0, nullptr, true, reinterpret_cast<const void*>(444), 0);
#ifdef S7_CASTLE_WIRE_PROTOTYPE
        check(castle_constructor_design_valid && castle_constructor_design.parts.empty());
#else
        check(castle_constructor_words[5] == 444); // Explicit no-castle remains empty.
#endif
        put(fixture + 0x260, 0);
        bootstrap_thread = GetCurrentThreadId() + 1;
        ConstructLocalCastlePlayer(nullptr, nullptr, 3, 0, nullptr, true, reinterpret_cast<const void*>(444), 0);
        check(castle_constructor_words[5] == 444); // Foreign thread cannot read profile state.
        bootstrap_thread = old_thread; initialization = old_initialization; local_connect_probe = old_probe;
        put(NativeVA(0x13A5228), 0);
    }
#endif
#ifdef S7_LOCAL_PAIR_LAB
    check(FakeJump(0xCB7F50, reinterpret_cast<const void*>(&FakeLocalInterface)));
    {
        unsigned char address[0x22]{};
        const uint32_t kind = 0x97;
        memcpy(address, &kind, sizeof(kind));
        const auto original_slot = local_pair_bind.load();
        local_pair_bind.store(0x7f000002u);
        for (uint32_t candidate : {0x7f000001u, 0x7f000002u, 0x7f000003u, 0xc0a80a01u, 0u}) {
            const auto ip = _byteswap_ulong(candidate); memcpy(address + 0x1e, &ip, sizeof(ip));
            check(AcceptLabLocalInterface(address, nullptr) == (candidate == 0x7f000002u)
                && GetLastError() == 6789);
            const char* reason = nullptr;
            check(!AcceptLabLocalInterface(address, &reason) && !reason);
        }
        check(!AcceptLabLocalInterface(nullptr, nullptr));
        lab_interface_original = true;
        check(AcceptLabLocalInterface(address, nullptr) && GetLastError() == 6789);
        lab_interface_original = false; local_pair_bind.store(original_slot);
    }
#endif
#ifdef S7_LOCAL_NETWORK_PUMP
    check(FakeJump(0xD49B10, reinterpret_cast<const void*>(&FakeLabJoinRequest)));
    check(FakeJump(0xC33020, reinterpret_cast<const void*>(&FakeEchoDispatcher)));
    {
        unsigned char dispatcher[4]{}; NativeResult result{};
        check(ObserveLabEchoDispatcher(dispatcher, nullptr, &result, 3, -1) == &result);
        check(lab_echo_self == dispatcher && lab_echo_phase == 3 && lab_echo_timeout == -1);
        check(result.code == 2 && result.detail == 654 && GetLastError() == 6789);
    }
    check(FakeJump(0xC24050, reinterpret_cast<const void*>(&FakeSessionDispatch)));
    check(FakeJump(0xB833E0, reinterpret_cast<const void*>(&FakeSessionDispatch)));
    check(FakeJump(0xC25610, reinterpret_cast<const void*>(&FakeSessionDispatch)));
    check(FakeJump(0xC22790, reinterpret_cast<const void*>(&FakeSessionDispatch)));
    check(FakeJump(0xB86710, reinterpret_cast<const void*>(&FakeSessionDispatch)));
    for (int code : {1, 2, 0, -1000000, -999912}) {
        NativeResult result{}; unsigned char manager[4]{}, event[4]{}; lab_join_code = code;
        check(ObserveLabSessionDispatch(manager, nullptr, &result, event) == &result);
        check(result.code == code && result.detail == 987 && GetLastError() == 6789);
        check(lab_dispatch_arguments[0] == manager && lab_dispatch_arguments[1] == &result && lab_dispatch_arguments[2] == event);
        check(ObserveLabTicketReceived(manager, nullptr, &result, event) == &result);
        check(result.code == code && result.detail == 987 && GetLastError() == 6789);
        check(lab_dispatch_arguments[0] == manager && lab_dispatch_arguments[1] == &result && lab_dispatch_arguments[2] == event);
        check(ObserveLabPlayerTicket(manager, nullptr, &result, event) == &result);
        check(result.code == code && result.detail == 987 && GetLastError() == 6789);
        check(lab_dispatch_arguments[0] == manager && lab_dispatch_arguments[1] == &result && lab_dispatch_arguments[2] == event);
        check(ObserveLabPlayerRequest(manager, nullptr, &result, event) == &result);
        check(result.code == code && result.detail == 987 && GetLastError() == 6789);
        check(lab_dispatch_arguments[0] == manager && lab_dispatch_arguments[1] == &result && lab_dispatch_arguments[2] == event);
        check(ObserveLabTicketRequest(manager, nullptr, &result, event) == &result);
        check(result.code == code && result.detail == 987 && GetLastError() == 6789);
        check(lab_dispatch_arguments[0] == manager && lab_dispatch_arguments[1] == &result && lab_dispatch_arguments[2] == event);
    }
    lab_join_code = 1;
    check(FakeJump(0xCFA5E0, reinterpret_cast<const void*>(&FakeHostEventRoute)));
    check(FakeJump(0xC9A280, reinterpret_cast<const void*>(&FakeHostEventType)));
    for(int match : {0,1}) {
        lab_join_code=match;
        check(ObserveLabHostJoinEventType(reinterpret_cast<void*>(11),nullptr,22,33)==(match==1));
        check(lab_ticket_words[0]==11 && lab_ticket_words[1]==22 && lab_ticket_words[2]==33 && GetLastError()==6789);
    }
    for(int code : {1,2,0,-999996,-1000000,-999912}) {
        lab_join_code=code; NativeResult result{};
        check(ObserveLabHostEventRoute(reinterpret_cast<void*>(11),nullptr,&result,22,reinterpret_cast<void*>(33),44)==&result);
        check(result.code==code && result.detail==123 && GetLastError()==6789);
        check(lab_ticket_words[0]==11 && lab_ticket_words[1]==22 && lab_ticket_words[2]==33 && lab_ticket_words[3]==44);
    }
    lab_join_code = 1;
    check(FakeJump(0xC0A8B0, reinterpret_cast<const void*>(&FakeTicketLookup)));
    check(FakeJump(0xED3070, reinterpret_cast<const void*>(&FakeHostJoinGate)));
    for (uintptr_t returned : {0u, 123u}) {
        lab_ticket_return=returned;
        check(ObserveLabTicketSessionLookup(reinterpret_cast<void*>(11), nullptr, 22) == reinterpret_cast<void*>(returned));
        check(lab_ticket_words[0]==11 && lab_ticket_words[1]==22 && GetLastError()==6789);
    }
    for (int gate : {0,1}) {
        unsigned char handler[0xc4]{}; handler[0xc0]=static_cast<unsigned char>(gate); unsigned char event[4]{};
        ObserveLabHostJoinGate(handler,nullptr,event);
        check(lab_dispatch_arguments[0]==handler && lab_dispatch_arguments[1]==event && GetLastError()==6789 && handler[0xc0]==gate);
    }
    check(FakeJump(0xC223F0, reinterpret_cast<const void*>(&FakeTicketResultWords)));
    check(FakeJump(0xC15E80, reinterpret_cast<const void*>(&FakeTicketResultWords)));
    for (int code : {2,1,0,-999912}) {
        NativeResult result{}; lab_join_code = code;
        const uintptr_t expected[] = {11,reinterpret_cast<uintptr_t>(&result),33,44,55,66,77,88};
        check(ObserveLabTicketSeven<0xC223F0>(reinterpret_cast<void*>(11), nullptr, &result, 33,44,55,66,77,88) == &result);
        check(result.code == code && result.detail == 123 && GetLastError() == 6789 && memcmp(lab_ticket_words,expected,sizeof(expected)) == 0);
        check(ObserveLabTicketSeven<0xC15E80>(reinterpret_cast<void*>(11), nullptr, &result, 33,44,55,66,77,88) == &result);
        check(result.code == code && result.detail == 123 && GetLastError() == 6789 && memcmp(lab_ticket_words,expected,sizeof(expected)) == 0);
    }
    lab_join_code = 1;
    check(FakeJump(0xC2BEB0, reinterpret_cast<const void*>(&FakeTicketWords)));
    check(FakeJump(0xC0BAE0, reinterpret_cast<const void*>(&FakeTicketWords)));
    for (uintptr_t returned : {0u, 123u}) {
        lab_ticket_return = returned;
        check(ObserveLabTicketTransport(reinterpret_cast<void*>(11), nullptr, 22, 33, 44, 55, 66, 77, 88) == returned);
        check(GetLastError() == 6789);
        const uintptr_t expected[] = {11,22,33,44,55,66,77,88};
        check(memcmp(lab_ticket_words, expected, sizeof(expected)) == 0);
        ObserveLabTicketEvent(reinterpret_cast<void*>(11), nullptr, 22, 33, 44, 55, 66, 77, 88);
        check(GetLastError() == 6789 && memcmp(lab_ticket_words, expected, sizeof(expected)) == 0);
    }
    for (int code : {2,1,0,-999912}) {
        NativeResult result{code, 123}; SetLastError(9012);
        check(ObserveLabTicketLinkResult(&result, nullptr) == code && result.detail == 123 && GetLastError() == 9012);
    }
    check(FakeJump(0x6576D0, reinterpret_cast<const void*>(&FakeLabDiagnosticCtor)));
    check(FakeJump(0x657E70, reinterpret_cast<const void*>(&FakePlayerReason)));
    for (uintptr_t literal : {0x10888D8u, 0x10888E8u, 0x10888C8u}) {
        unsigned char output[32]{}; const char* text = Native<const char*>(literal);
        check(ObserveLabPlayerReason(output, nullptr, text, 13) == output);
        check(lab_rejection_literal == text && lab_reason_length == 13 && GetLastError() == 6789 && output[0] == 0);
        check(ObserveLabTicketReferenceMissing(output, nullptr, text, 0) == output);
        check(lab_rejection_literal == text && lab_reason_length == 0 && GetLastError() == 6789 && output[0] == 0);
        check(ObserveLabTicketStateRejection(output, nullptr, text, 0) == output);
        check(lab_rejection_literal == text && lab_reason_length == 0 && GetLastError() == 6789 && output[0] == 0);
        check(ObserveLabTicketRejection(output, nullptr, text) == output);
        check(lab_rejection_literal == text && GetLastError() == 6789 && output[0] == 0);
    }
    const struct { uintptr_t caller; const char* label; } ticket_reasons[] = {
        {0xC24C0E, "lab-native-ticket-session-state-rejected"},
        {0xC24CA2, "lab-native-ticket-session-flag-rejected"},
        {0xC24D41, "lab-native-ticket-empty-peer-rejected"},
        {0xC24DD6, "lab-native-ticket-transport-pool-exhausted"},
        {0xC24E27, "lab-native-ticket-add-host-failed"},
        {0xC24E8B, "lab-native-ticket-existing-request"},
        {0xC24F11, "lab-native-ticket-pool-exhausted"},
        {0xC25143, "lab-native-ticket-no-ready-peer"},
        {0xC252C3, "lab-native-ticket-host-reference-rejected"},
        {0, "lab-native-ticket-rejection-unclassified"}
    };
    for (const auto& reason : ticket_reasons)
        check(strcmp(LabTicketRejectionLabel(reason.caller), reason.label) == 0);
    for (uintptr_t literal : {0x108DAD0u, 0x108DB10u, 0x108DC4Cu, 0x108DC78u, 0x108DCA4u, 0x108DCDCu, 0x108DD04u, 0x10888F8u}) {
        unsigned char owned_output[32]{};
        auto* text = Native<const char*>(literal);
        check(ObserveLabSessionRejection(owned_output, nullptr, text) == owned_output
            && lab_rejection_literal == text && owned_output[0] == 0 && GetLastError() == 6789);
    }
    for (int result_code : {1, 2, 0, -1000000, -999985, -999977, -999920, -123}) {
        lab_join_code = result_code; NativeResult result{};
        check(ObserveLabJoinRequest(&result, reinterpret_cast<void*>(11), reinterpret_cast<void*>(22),
            reinterpret_cast<void*>(33), reinterpret_cast<void*>(44), reinterpret_cast<void*>(55), 66) == &result);
        check(result.code == result_code && result.detail == 123 && GetLastError() == 6789);
        SetLastError(9012);
        check(ObserveLabJoinCompletion(&result, nullptr) == result_code
            && result.code == result_code && result.detail == 123 && GetLastError() == 9012);
        check(lab_join_arguments[0] == 11 && lab_join_arguments[1] == 22 && lab_join_arguments[2] == 33
            && lab_join_arguments[3] == 44 && lab_join_arguments[4] == 55 && lab_join_arguments[5] == 66);
    }
    for (int code : {-999912, -999907, -999906}) {
        for (unsigned int detail : {0u, 5u, 995u, 10013u, 10048u, 10049u, 10060u, 10061u, 0xabcdef01u}) {
            NativeResult result{code, detail};
            const auto before = diagnostic_events.size();
            SetLastError(9012);
            check(ObserveLabJoinCompletion(&result, nullptr) == code && result.code == code
                && result.detail == detail && GetLastError() == 9012);
            check(diagnostic_events.size() == before + 2);
            const char* expected = detail == 0 ? "local-join-os-error-none"
                : detail == 5 || detail == 10013 ? "local-join-os-error-access-denied"
                : detail == 995 ? "local-join-os-error-operation-aborted"
                : detail == 10048 ? "local-join-os-error-address-in-use"
                : detail == 10049 ? "local-join-os-error-address-unavailable"
                : detail == 10060 ? "local-join-os-error-timeout"
                : detail == 10061 ? "local-join-os-error-refused" : "local-join-os-error-other";
            check(strcmp(diagnostic_events.back(), expected) == 0);
        }
    }
#endif
    local_connect_probe = true; developer_mode = true;
    std::vector<const Patch*> selected;
    for (const auto& patch : Patches) selected.push_back(&patch);
    for (const auto& patch : LegacyOrbitPatches) selected.push_back(&patch);
    selected.push_back(&DevelopmentPatch);
    for (const auto& patch : ConnectPatches) selected.push_back(&patch);
    auto occupied_patch_byte = [&](const unsigned char* byte) {
        for (const auto* candidate : selected) {
            const auto* begin = PatchAddress(*candidate);
            const size_t length = candidate->pointer_slot ? 4 : (candidate->raw_length ? candidate->raw_length : 5);
            if (byte >= begin && byte < begin + length) return true;
        }
        return false;
    };
    memcpy(Native<void*>(0x770906), RewardUnlockedEpilogue, sizeof(RewardUnlockedEpilogue));
    for (const auto* patch : selected) {
        auto* at = PatchAddress(*patch);
        const size_t length = patch->pointer_slot ? 4 : (patch->raw_length ? patch->raw_length : 5);
        // Adjacent vtable slots are both legitimate patch ranges. Do not put
        // a canary inside the next slot and corrupt its original-byte guard.
        if (!occupied_patch_byte(at - 1)) at[-1] = 0xa5;
        if (!occupied_patch_byte(at + length)) at[length] = 0x5a;
        if (patch->pointer_slot) {
            uint32_t original = OriginalPointer(*patch); memcpy(at, &original, 4);
        } else memcpy(at, patch->original, length);
    }
    DWORD old = 0;
    if (!VirtualProtect(memory, ExpectedImageSize, PAGE_READONLY, &old)) return 2;
    check(Install());
    for (const auto* patch : selected) {
        auto* at = PatchAddress(*patch);
        const size_t length = patch->pointer_slot ? 4 : (patch->raw_length ? patch->raw_length : 5);
        check((occupied_patch_byte(at - 1) || at[-1] == 0xa5)
            && (occupied_patch_byte(at + length) || at[length] == 0x5a));
        uint32_t actual = 0;
        if (patch->raw_length) check(memcmp(at, patch->replacement, length) == 0);
        else if (patch->pointer_slot) memcpy(&actual, at, 4);
        else { check(at[0] == 0xe8); memcpy(&actual, at + 1, 4); actual += static_cast<uint32_t>(reinterpret_cast<uintptr_t>(at) + 5); }
        if (!patch->raw_length) check(actual == reinterpret_cast<uintptr_t>(patch->replacement));
        MEMORY_BASIC_INFORMATION info{};
        check(VirtualQuery(at, &info, sizeof(info)) && info.Protect == PAGE_READONLY);
        check(WritePatch(*patch, true));
        if (patch->pointer_slot) { memcpy(&actual, at, 4); check(actual == OriginalPointer(*patch)); }
        else check(memcmp(at, patch->original, length) == 0);
    }
    // Execute the real six-byte reward branch and original false epilogue in
    // owned synthetic x86 memory. Only a tiny prologue is supplied; no game,
    // SDK, profile, reward catalogue or native constructor is called.
    const Patch* reward_patch = nullptr;
    for (const auto& patch : Patches) if (patch.va == 0x770915) reward_patch = &patch;
    check(reward_patch != nullptr);
    Patch oversized_reward = *reward_patch;
    oversized_reward.raw_length = 7;
    check(!WritePatch(oversized_reward, false) && !WritePatch(oversized_reward, true));
    auto* reward_tail = Native<unsigned char*>(0x770906);
    DWORD reward_protection = 0, reward_ignored = 0;
    check(VirtualProtect(reward_tail, 21, PAGE_EXECUTE_READWRITE, &reward_protection) != FALSE);
    const unsigned char reward_prologue[] = {0x55,0x8b,0xec,0x53,0x56,0x57};
    memcpy(reward_tail + 9, reward_prologue, sizeof(reward_prologue));
    check(WritePatch(*reward_patch, false));
    check(FlushInstructionCache(GetCurrentProcess(), reward_tail, 21) != FALSE);
    using RewardPredicate = bool (__stdcall*)(unsigned int, const void*);
    auto reward_locked = reinterpret_cast<RewardPredicate>(reward_tail + 9);
    for (unsigned int type : {0u, 1u, 2u}) {
        SetLastError(8765);
        check(!reward_locked(type, nullptr) && GetLastError() == 8765);
    }
    check(WritePatch(*reward_patch, true));
    // Guard the whole six-byte instruction AND the separate branch target.
    auto* reward_site = PatchAddress(*reward_patch);
    reward_site[5] ^= 1;
    check(!Install() && error_code == ChangedCallsite);
    reward_site[5] ^= 1;
    reward_tail[8] ^= 1;
    check(!Install() && error_code == ChangedCallsite);
    reward_tail[8] ^= 1;
    check(VirtualProtect(reward_tail, 21, reward_protection, &reward_ignored) != FALSE);
    for (const auto& patch : Patches)
        check(memcmp(PatchAddress(patch), patch.original, patch.raw_length ? patch.raw_length : 5) == 0);
    // Change the last pointer guard: the preflight must fail before any earlier
    // CALL is rewritten, regardless of which optional patches were selected.
    const Patch& last = *selected.back();
    auto* at = PatchAddress(last);
    VirtualProtect(at, 4, PAGE_READWRITE, &old);
    uint32_t bad = 0; memcpy(at, &bad, 4);
    DWORD ignored = 0; VirtualProtect(at, 4, old, &ignored);
    check(!Install() && error_code == ChangedCallsite);
    for (const auto& patch : Patches) check(memcmp(PatchAddress(patch), patch.original, patch.raw_length ? patch.raw_length : 5) == 0);
    check(!LocalContentLocked(nullptr));
    // Baseline mode ignores the opt-in connection table.
    local_connect_probe = false;
    check(Install());
    memcpy(&bad, at, 4); check(bad == 0);
    for (const auto& patch : Patches) check(WritePatch(patch, true));
    check(WritePatch(DevelopmentPatch, true));
    // Developer mode is optional locally and forbidden in LAN, including a
    // contradictory caller request. All ordinary content patches still apply.
    // Execute the real guarded SETcc bytes in a tiny owned x86 predicate;
    // ECX supplies the synthetic original engine boolean, without any game.
    auto* development_predicate = PatchAddress(DevelopmentPatch) - 4;
    VirtualProtect(development_predicate, 8, PAGE_EXECUTE_READWRITE, &old);
    const unsigned char predicate[] = {0x33,0xc0,0x85,0xc9,0x0f,0x95,0xc0,0xc3};
    memcpy(development_predicate, predicate, sizeof(predicate));
    check(FlushInstructionCache(GetCurrentProcess(), development_predicate, sizeof(predicate)) != FALSE);
    VirtualProtect(development_predicate, 8, PAGE_EXECUTE_READ, &ignored);
    using DevelopmentPredicate = unsigned int (__fastcall*)(unsigned int);
    auto read_developer = reinterpret_cast<DevelopmentPredicate>(development_predicate);
    for (bool lan : {false, true}) for (bool requested : {false, true}) {
        lan_game = lan; developer_mode = requested;
        check(Install());
        check(memcmp(PatchAddress(DevelopmentPatch), requested && !lan
            ? DevelopmentModeBytes : DevelopmentPatch.original, 3) == 0);
        check(read_developer(0) == (requested && !lan ? 1u : 0u));
        check(read_developer(1) == (requested && !lan ? 0u : 1u));
        for (const auto& patch : Patches) check(WritePatch(patch, true));
        check(WritePatch(DevelopmentPatch, true));
    }
    lan_game = true; developer_mode = true;
    check(WritePatch(DevelopmentPatch, false));
    check(!Install() && error_code == ChangedCallsite); // Guard even when skipped.
    for (const auto& patch : Patches)
        check(memcmp(PatchAddress(patch), patch.original, 5) == 0);
    check(WritePatch(DevelopmentPatch, true));
    lan_game = false; developer_mode = false;
    // Pump gating must not call any game function on the disabled path, a
    // foreign thread, recursion, a resumed natural tick or incomplete objects.
    initialization = 2;
    bootstrap_thread = GetCurrentThreadId();
    desync_diagnostics = {};
    unsigned int foreign_observations = 0;
    HANDLE diagnostic_thread = CreateThread(nullptr, 0, RunForeignDesyncObservations, &foreign_observations, 0, nullptr);
    check(diagnostic_thread != nullptr);
    if (diagnostic_thread) {
        check(WaitForSingleObject(diagnostic_thread, 10000) == WAIT_OBJECT_0);
        check(CloseHandle(diagnostic_thread) != FALSE);
        check(foreign_observations == 10000 && !desync_diagnostics.started && desync_diagnostics.pumps == 0);
    }
    PumpOfflineBootstrap(); check(!pump_active);
    local_connect_probe = true;
    bootstrap_thread = 0;
    PumpOfflineBootstrap(); check(!pump_active);
    bootstrap_thread = GetCurrentThreadId();
    pump_active = true;
    PumpOfflineBootstrap(); check(pump_active);
    pump_active = false; natural_updates_seen = true;
    PumpOfflineBootstrap(); check(!pump_active);
    natural_updates_seen = false;
    PumpOfflineBootstrap(); check(!pump_active); // Null network.
    VirtualProtect(memory, ExpectedImageSize, PAGE_READWRITE, &old);
    // The local-start decision is scoped to its exact platform instance,
    // thread and explicit opt-in. It never changes the platform's offline bit.
    alignas(4) unsigned char policy[0x84]{};
    *Native<void**>(0x13789D0) = policy;
    check(SkipLocalNetworkStartup(nullptr, nullptr));
    check(SkipLocalNetworkStartup(policy, nullptr)); // Unknown platform type.
    *reinterpret_cast<uintptr_t*>(policy) = image_base + 0x112BE3C - PreferredBase;
    policy[0x80] = 1;
    check(!SkipLocalNetworkStartup(policy, nullptr) && policy[0x80] == 1);
    local_connect_probe = false;
    check(SkipLocalNetworkStartup(policy, nullptr) && policy[0x80] == 1);
    local_connect_probe = true; initialization = 1;
    check(SkipLocalNetworkStartup(policy, nullptr));
    initialization = 2; bootstrap_thread = 0;
    check(SkipLocalNetworkStartup(policy, nullptr));
    bootstrap_thread = GetCurrentThreadId();
    *Native<void**>(0x13789D0) = nullptr;
    check(SkipLocalNetworkStartup(policy, nullptr));
    *Native<void**>(0x13789D0) = policy;
    policy[0x80] = 0;
    check(!SkipLocalNetworkStartup(policy, nullptr) && policy[0x80] == 0);
    *Native<void**>(0x13789D0) = nullptr;
    alignas(4) unsigned char network[0xa80]{};
    alignas(4) unsigned char owner[0x2a8]{};
    *Native<unsigned char**>(0x1379E14) = network;
    PumpOfflineBootstrap(); check(!pump_active); // Null owner.
    *reinterpret_cast<unsigned char**>(network + 0xa18) = owner;
    PumpOfflineBootstrap(); check(!pump_active); // Wrong owner table.
    *reinterpret_cast<uintptr_t*>(owner) = image_base + 0x10919BC - PreferredBase;
    PumpOfflineBootstrap(); check(!pump_active); // Wrong RV table.
    *reinterpret_cast<uintptr_t*>(owner + 0x50) = image_base + 0x109C9D8 - PreferredBase;
    PumpOfflineBootstrap(); check(!pump_active); // Missing facade.
    *reinterpret_cast<void**>(owner + 0xa4) = owner;
    PumpOfflineBootstrap(); check(!pump_active); // Missing update dependencies.
    alignas(4) unsigned char facade[0x24]{};
    *reinterpret_cast<void**>(owner + 0xa4) = facade;
    check(!LocalConnectionReady(nullptr) && !LocalConnectionReady(owner));
    check(!LocalConnectionReady(network)); // Disconnected.
    owner[0x88] = 1; owner[0x98] = 1;
    check(!LocalConnectionReady(network)); // Pending cannot claim readiness.
    owner[0x98] = 0;
    check(LocalConnectionReady(network));
    local_connect_probe = false;
    check(!LocalConnectionReady(network));
    local_connect_probe = true; initialization = 1;
    check(!LocalConnectionReady(network));
    initialization = 2;
    *reinterpret_cast<void**>(facade + 0x0c) = owner;
    check(!LocalConnectionReady(network)); // Borrowed Ubi facade is not local.
    *reinterpret_cast<void**>(facade + 0x0c) = nullptr;
    *reinterpret_cast<uintptr_t*>(owner + 0x50) = 0;
    check(!LocalConnectionReady(network));
    *reinterpret_cast<uintptr_t*>(owner + 0x50) = image_base + 0x109C9D8 - PreferredBase;
    *reinterpret_cast<uintptr_t*>(owner) = 0;
    check(!LocalConnectionReady(network));

    // Exercise the production pump against synthetic native objects and x86
    // trampoline stubs. This verifies actual calling conventions, dispatch
    // order, fallback suppression and callback-driven teardown, without S7.
    *reinterpret_cast<uintptr_t*>(owner) = image_base + 0x10919BC - PreferredBase;
    *reinterpret_cast<void**>(owner + 0x100) = owner;
    *reinterpret_cast<void**>(owner + 0x104) = owner;
    *reinterpret_cast<void**>(owner + 0x110) = owner;
#ifdef S7_LOCAL_NETWORK_PUMP
    {
        alignas(4) unsigned char main[0x48]{}, state[0x100]{}, latency[0x98]{};
        *Native<unsigned char**>(0x13A5210) = main;
        *reinterpret_cast<uintptr_t*>(main) = image_base + 0x113F2C0 - PreferredBase;
        *reinterpret_cast<void**>(main + 0x18) = state;
        *reinterpret_cast<uintptr_t*>(state) = image_base + 0x113F424 - PreferredBase;
        *reinterpret_cast<void**>(network + 0xa58) = latency;
        *reinterpret_cast<uintptr_t*>(latency) = image_base + 0x1140188 - PreferredBase;
        latency[0x18] = 1; state[0x60] = 5;
        auto* samples = reinterpret_cast<unsigned int*>(latency + 0x44);
        for (unsigned int i = 0; i < 20; ++i) samples[i] = UINT32_MAX;
        check(FakeJump(0xECC870, reinterpret_cast<const void*>(&FakeTurnOffsetSetter)));
        SetLocalTurnOffset(state, nullptr, 1083);
        check(turn_setter_self == state && turn_setter_value == 5 && GetLastError() == 6789);
        // Reproduce six hours of the original unknown-latency increment path.
        // The real wrapper feeds the fake native setter; only native smoothing
        // is represented by the one-step convergence below.
        unsigned int offset = 1;
        for (unsigned int turn = 0; turn < 216000; ++turn) {
            SetLocalTurnOffset(state, nullptr, offset + 1);
            if (offset < turn_setter_value) ++offset;
            else if (offset > turn_setter_value) --offset;
        }
        check(offset == 5 && turn_setter_value == 5);
        for (unsigned int index : {0u, 19u}) for (unsigned int measured : {0u, 1u, 66u, 1000u, UINT32_MAX - 1}) {
            samples[index] = measured;
            for (unsigned int requested : {0u, 2u, 1083u, UINT32_MAX}) {
                SetLocalTurnOffset(state, nullptr, requested);
                check(turn_setter_value == requested && GetLastError() == 6789);
            }
            samples[index] = UINT32_MAX;
        }
        state[0x60] = 0; *reinterpret_cast<unsigned int*>(state + 0x28) = 7;
        SetLocalTurnOffset(state, nullptr, 1083); check(turn_setter_value == 7);
        state[0x60] = 5;
        local_connect_probe = false;
        SetLocalTurnOffset(state, nullptr, 9); check(turn_setter_value == 9); local_connect_probe = true;
        bootstrap_thread = 0;
        SetLocalTurnOffset(state, nullptr, 10); check(turn_setter_value == 10); bootstrap_thread = GetCurrentThreadId();
        initialization = 1;
        SetLocalTurnOffset(state, nullptr, 11); check(turn_setter_value == 11); initialization = 2;
        *reinterpret_cast<void**>(main + 0x18) = nullptr;
        SetLocalTurnOffset(state, nullptr, 12); check(turn_setter_value == 12);
        *reinterpret_cast<void**>(main + 0x18) = state;
        *reinterpret_cast<uintptr_t*>(latency) = 0;
        SetLocalTurnOffset(state, nullptr, 13); check(turn_setter_value == 13);
        *reinterpret_cast<uintptr_t*>(latency) = image_base + 0x1140188 - PreferredBase;
        latency[0x18] = 0;
        SetLocalTurnOffset(state, nullptr, 14); check(turn_setter_value == 14); latency[0x18] = 1;
        *reinterpret_cast<void**>(facade + 0x0c) = owner;
        SetLocalTurnOffset(state, nullptr, 15); check(turn_setter_value == 15);
        *reinterpret_cast<void**>(facade + 0x0c) = nullptr;
        *Native<unsigned char**>(0x13A5210) = nullptr;
        *reinterpret_cast<void**>(network + 0xa58) = nullptr;
    }
#endif
    owner[0x78] = 1; // A real connection is already requested in this fixture.
    pump_network = network; pump_owner = owner;
    check(FakeJump(0xE82240, reinterpret_cast<const void*>(&FakeNetworkGetter)));
    check(FakeJump(0xC956C0, reinterpret_cast<const void*>(&FakeOwnerUpdate)));
    check(FakeJump(0xED3BA0, reinterpret_cast<const void*>(&FakeMasterUpdate)));
    check(FakeJump(0xCBC710, reinterpret_cast<const void*>(&FakeCoreUpdate)));
    // Exercise the actual shared virtual-slot wrapper from a real foreign
    // Windows thread. Every original callback must run there, while the
    // bridge's non-atomic main-thread state and connect machinery stay idle.
    const auto foreign_owner_before = owner_update_count;
    const auto foreign_getter_before = network_getter_calls;
    natural_updates_seen = false;
    pump_mode = 19; pump_trace.clear();
    ForeignOwnerUpdate foreign_owner{owner, 0, 0, 10000};
    HANDLE foreign_thread = CreateThread(nullptr, 0, RunForeignOwnerUpdates, &foreign_owner, 0, nullptr);
    check(foreign_thread != nullptr);
    if (foreign_thread) {
        check(WaitForSingleObject(foreign_thread, 10000) == WAIT_OBJECT_0);
        check(CloseHandle(foreign_thread) != FALSE);
        check(foreign_owner.thread != bootstrap_thread && owner_update_thread == foreign_owner.thread);
        check(owner_update_self == owner && pump_trace.size() == foreign_owner.calls);
        check(std::all_of(pump_trace.begin(), pump_trace.end(), [](char value) { return value == 'O'; }));
        check(foreign_owner.error == 6789);
        check(owner_update_count == foreign_owner_before && !natural_updates_seen);
        check(network_getter_calls == foreign_getter_before && !pump_active);
    }
    check(FakeJump(0xD49B80, reinterpret_cast<const void*>(&FakeNestedConnectJob)));
    const auto saved_udp_port = udp_port;
    udp_port = 9101;
    for (int mode : {21, 22}) {
        pump_mode = mode; pump_trace.clear();
        owner[0x24] = 0; owner[0x248] = 1;
        owner[0x78] = mode == 22 ? 1 : 0;
        owner[0x88] = 0; owner[0x98] = 0;
        nested_connect_jobs = 0; nested_connect_thread = 0;
        nested_connect_result = {123, 456};
        auto* stored = reinterpret_cast<NativeResult*>(owner + 0x278);
        *stored = {321, 654};
        const auto count_before = owner_update_count;
        const auto getters_before = network_getter_calls;
        const auto labels_before = diagnostic_events.size();
        ForeignOwnerUpdate nested{owner, 0, 0, 1};
        HANDLE nested_thread = CreateThread(nullptr, 0, RunForeignOwnerUpdates, &nested, 0, nullptr);
        check(nested_thread != nullptr);
        if (nested_thread) {
            check(WaitForSingleObject(nested_thread, 10000) == WAIT_OBJECT_0);
            check(CloseHandle(nested_thread) != FALSE);
            check(nested.thread != bootstrap_thread && owner_update_thread == nested.thread);
            check(pump_trace.size() == 1 && pump_trace[0] == 'O');
            check(nested_connect_result.code == LocalConnectUnavailable && nested_connect_result.detail == 0);
            check(stored->code == 321 && stored->detail == 654);
            check(owner[0x78] == (mode == 22 ? 1 : 0) && owner[0x98] == 0);
            check(nested_connect_jobs == 0 && nested_connect_thread == 0);
            check(owner_update_count == count_before && network_getter_calls == getters_before);
            check(diagnostic_events.size() == labels_before);
            check(mode != 21 || owner[0x248] == 0); // Original callback was not dropped.
        }
        // The ordinary next owner tick starts the genuine job on its owner
        // thread, without synthetic readiness or a new cross-thread queue.
        UpdateLocalGame(owner, nullptr);
        check(nested_connect_jobs == 1 && nested_connect_thread == bootstrap_thread);
        check(owner[0x78] == 1 && owner[0x98] == 1 && owner_update_count == count_before + 1);
        if (mode == 22) check(nested_connect_result.code == 2 && nested_connect_result.detail == 0);
    }
    HANDLE poisoned_thread = CreateThread(nullptr, 0, RunForeignPoisonedConnect, nullptr, 0, nullptr);
    check(poisoned_thread != nullptr);
    if (poisoned_thread) {
        check(WaitForSingleObject(poisoned_thread, 10000) == WAIT_OBJECT_0);
        DWORD exit_code = 99;
        check(GetExitCodeThread(poisoned_thread, &exit_code) != FALSE && exit_code == 0);
        check(CloseHandle(poisoned_thread) != FALSE);
    }
    udp_port = saved_udp_port;
    owner[0x78] = 1; owner[0x88] = 1; owner[0x98] = 0;
    natural_updates_seen = false;
    pump_mode = 0; pump_trace.clear();
    auto trace_is = [&](const char* expected) {
        return pump_trace.size() == strlen(expected) &&
            (pump_trace.empty() || memcmp(pump_trace.data(), expected, pump_trace.size()) == 0);
    };
    PumpOfflineBootstrap(); check(trace_is("O") && !pump_active);
    alignas(4) unsigned char core[0x24]{};
    alignas(4) unsigned char implementation[8]{};
    *reinterpret_cast<void**>(network + 0x2c) = core;
    check(!LocalUpdateCore(network)); // Unknown core type.
    *reinterpret_cast<uintptr_t*>(core) = image_base + 0x10940A0 - PreferredBase;
    check(!LocalUpdateCore(network)); // Master not constructed.
    *reinterpret_cast<uintptr_t*>(network + 0x5c) = image_base + 0x1091E3C - PreferredBase;
    check(!LocalUpdateCore(network)); // Master not registered with the core.
    *reinterpret_cast<void**>(network + 0x5c + 0x4c0) = core;
    check(!LocalUpdateCore(network)); // Missing implementation.
    *reinterpret_cast<void**>(core + 0x20) = implementation;
    check(!LocalUpdateCore(network)); // Unknown implementation type.
    *reinterpret_cast<uintptr_t*>(implementation) = image_base + 0x1099548 - PreferredBase;
    check(!LocalUpdateCore(network)); // Missing scheduler.
#ifdef S7_LOCAL_NETWORK_PUMP
    alignas(4) unsigned char scheduler[0xc0]{};
    *reinterpret_cast<void**>(implementation + 4) = scheduler;
    check(!LocalUpdateCore(network)); // Reject unknown scheduler before reading its frame.
    *reinterpret_cast<uintptr_t*>(scheduler) = image_base + 0x109959C - PreferredBase;
    pump_scheduler = scheduler;
#else
    *reinterpret_cast<void**>(implementation + 4) = implementation;
#endif
    check(LocalUpdateCore(network) == core);
    // A supplied core callback leaks both FP control modes. The observer must
    // attribute it once, preserve the callback's state, and never "fix" it.
    unsigned short original_x87 = 0;
    __asm fnstcw original_x87
    const auto original_sse = _mm_getcsr();
    const auto original_controls = ReadDesyncFpControl();
    desync_diagnostics = {}; diagnostic_events.clear(); pump_mode = 20;
    PumpOfflineBootstrap();
    const auto leaked_controls = ReadDesyncFpControl();
    check(original_controls.x87 != leaked_controls.x87 && original_controls.sse != leaked_controls.sse);
    check(desync_diagnostics.changes[static_cast<unsigned int>(DesyncPhase::Core)] == 3);
    check(desync_diagnostics.changes[static_cast<unsigned int>(DesyncPhase::Master)] == 0);
    check(std::count_if(diagnostic_events.begin(), diagnostic_events.end(), [](const char* value) {
        return strcmp(value, "local-desync-fp-core-x87-changed") == 0; }) == 1);
    check(std::count_if(diagnostic_events.begin(), diagnostic_events.end(), [](const char* value) {
        return strcmp(value, "local-desync-fp-core-sse-changed") == 0; }) == 1);
    __asm fldcw original_x87
    _mm_setcsr(original_sse);
    desync_diagnostics = {}; diagnostic_events.clear(); pump_mode = 0; pump_trace.clear();
    udp_port = 10001;
    check(!LocalSessionSearchReady(network)); // No successful native startup.
    network[0x5c + 0x4c4] = 1;
    owner[0x18] = 1; owner[0x38] = 1;
    *reinterpret_cast<int32_t*>(owner + 0x28) = 1;
    check(!LocalSessionSearchReady(network)); // Missing search helper.
    *reinterpret_cast<void**>(owner + 0x108) = owner;
    check(LocalSessionSearchReady(network) && owner[0x3a] == 0);
    for (auto* flag : {network + 0x5c + 0x4c4, owner + 0x18, owner + 0x38, owner + 0x88}) {
        *flag = 0; check(!LocalSessionSearchReady(network)); *flag = 1;
    }
    for (int32_t result : {-1, 0, 2}) {
        *reinterpret_cast<int32_t*>(owner + 0x28) = result;
        check(!LocalSessionSearchReady(network));
    }
    *reinterpret_cast<int32_t*>(owner + 0x28) = 1;
    owner[0x249] = 1; check(!LocalSessionSearchReady(network)); owner[0x249] = 0;
    owner[0x98] = 1; check(!LocalSessionSearchReady(network)); owner[0x98] = 0;
    udp_port = 0; check(!LocalSessionSearchReady(network)); udp_port = 10001;
    local_connect_probe = false; check(!LocalSessionSearchReady(network)); local_connect_probe = true;
    bootstrap_thread = 0; check(!LocalSessionSearchReady(network)); bootstrap_thread = GetCurrentThreadId();
    *reinterpret_cast<void**>(network + 0x2c) = nullptr;
    check(!LocalSessionSearchReady(network)); *reinterpret_cast<void**>(network + 0x2c) = core;
    *reinterpret_cast<void**>(facade + 0x0c) = owner;
    check(!LocalSessionSearchReady(network)); *reinterpret_cast<void**>(facade + 0x0c) = nullptr;
    check(!LocalSessionSearchReady(nullptr) && !LocalSessionSearchReady(owner));
    check(LocalSessionSearchReady(network) && owner[0x3a] == 0);
    // Check the returned native small-string layout, surrounding stack memory
    // and thiscall/fastcall callee cleanup through the actual patched target.
    struct SandboxFixture { uint32_t before; NativeSmallString value; uint32_t after; } sandbox;
    memset(&sandbox, 0xa5, sizeof(sandbox));
    using SandboxGetter = NativeSmallString* (__thiscall*)(void*, NativeSmallString*);
    check(FakeJump(0xD43F50, reinterpret_cast<const void*>(&GetLocalSearchSandbox)));
    for (int iteration = 0; iteration < 3; ++iteration) {
        check(Native<SandboxGetter>(0xD43F50)(owner + 0x50, &sandbox.value) == &sandbox.value);
        check(sandbox.before == 0xa5a5a5a5 && sandbox.after == 0xa5a5a5a5);
        check(sandbox.value.length == 0 && sandbox.value.capacity == 15 &&
            sandbox.value.inline_data[0] == 0 && sandbox.value.inline_data[15] == 0);
    }
    const auto unchanged = sandbox;
    check(!ConstructLocalSearchSandbox(nullptr, &sandbox.value));
    check(!ConstructLocalSearchSandbox(owner, &sandbox.value));
    check(!ConstructLocalSearchSandbox(owner + 0x50, nullptr));
    bootstrap_thread = 0;
    check(!ConstructLocalSearchSandbox(owner + 0x50, &sandbox.value));
    bootstrap_thread = GetCurrentThreadId(); local_connect_probe = false;
    check(!ConstructLocalSearchSandbox(owner + 0x50, &sandbox.value));
    local_connect_probe = true; initialization = 1;
    check(!ConstructLocalSearchSandbox(owner + 0x50, &sandbox.value));
    initialization = 2; owner[0x98] = 1;
    check(!ConstructLocalSearchSandbox(owner + 0x50, &sandbox.value));
    owner[0x98] = 0; *reinterpret_cast<void**>(facade + 0x0c) = owner;
    check(!ConstructLocalSearchSandbox(owner + 0x50, &sandbox.value));
    *reinterpret_cast<void**>(facade + 0x0c) = nullptr;
    check(memcmp(&sandbox, &unchanged, sizeof(sandbox)) == 0);
    alignas(4) unsigned char search_helper[0x1dc]{};
    alignas(4) unsigned char backend[0xe8]{};
    *reinterpret_cast<void**>(owner + 0x108) = search_helper;
    check(!EnsureLocalSearchSpace(network)); // Reject unknown helper type.
    *reinterpret_cast<uintptr_t*>(search_helper) = image_base + 0x109CB58 - PreferredBase;
    check(!EnsureLocalSearchSpace(network)); // Missing private context.
    *reinterpret_cast<void**>(search_helper + 0x1d8) = search_helper;
    check(!EnsureLocalSearchSpace(network)); // Missing backend.
    check(!QueryLocalSessionCreate(network, nullptr));
    check(!QueryLocalSessionJoin(network, nullptr));
    *reinterpret_cast<void**>(facade + 0x20) = backend;
    check(!EnsureLocalSearchSpace(network)); // Missing public space string.
    *reinterpret_cast<const char**>(backend + 0xe4) = "";
    check(!EnsureLocalSearchSpace(network));
    char too_long[130]; memset(too_long, 'x', sizeof(too_long)); too_long[129] = 0;
    *reinterpret_cast<const char**>(backend + 0xe4) = too_long;
    check(!EnsureLocalSearchSpace(network));
    *reinterpret_cast<const char**>(backend + 0xe4) = "invalid space";
    check(!EnsureLocalSearchSpace(network) && search_space_calls == 0);
    check(FakeJump(0xD4A860, reinterpret_cast<const void*>(&FakeSetSearchSpace)));
    auto* bound_space = reinterpret_cast<NativeSmallString*>(search_helper + 0x10);
    for (const char* value : {"local-s7", "12345678-1234-1234-1234-123456789abc"}) {
        *bound_space = {}; bound_space->capacity = 15;
        *reinterpret_cast<const char**>(backend + 0xe4) = value;
        const int before = search_space_calls;
        check(EnsureLocalSearchSpace(network));
        check(SameSearchSpace(*bound_space, value, strlen(value)));
        check(EnsureLocalSearchSpace(network) && search_space_calls == before + 1);
        check(QueryLocalSessionCreate(network, nullptr) && search_space_calls == before + 1);
        check(QueryLocalSessionJoin(network, nullptr) && search_space_calls == before + 1);
        const unsigned char original_nat = owner[0x3a];
        local_connect_probe = false;
        check(!QueryLocalSessionCreate(network, nullptr));
        check(!QueryLocalSessionJoin(network, nullptr));
        local_connect_probe = true;
        bootstrap_thread = 0;
        check(!QueryLocalSessionCreate(network, nullptr));
        check(!QueryLocalSessionJoin(network, nullptr));
        bootstrap_thread = GetCurrentThreadId();
        owner[0x88] = 0;
        check(!QueryLocalSessionCreate(network, nullptr));
        check(!QueryLocalSessionJoin(network, nullptr));
        owner[0x88] = 1;
        check(QueryLocalSessionCreate(network, nullptr) && owner[0x3a] == original_nat);
        check(QueryLocalSessionJoin(network, nullptr) && owner[0x3a] == original_nat);
        *reinterpret_cast<const char**>(backend + 0xe4) = "different-space";
        check(!EnsureLocalSearchSpace(network) && search_space_calls == before + 1);
        check(!QueryLocalSessionCreate(network, nullptr));
        check(!QueryLocalSessionJoin(network, nullptr));
    }
    *bound_space = {}; bound_space->capacity = 15;
    *reinterpret_cast<const char**>(backend + 0xe4) = "local-s7";
    search_space_mode = 1;
    check(!EnsureLocalSearchSpace(network)); // Setter tears down ownership.
    *Native<unsigned char**>(0x1379E14) = network;
    *bound_space = {}; bound_space->capacity = 15; search_space_mode = 2;
    check(!EnsureLocalSearchSpace(network)); // Setter did not preserve namespace.
    search_space_mode = 0;
    for (int mode = 0; mode <= 8; ++mode) {
        pump_mode = mode; pump_trace.clear(); network_getter_calls = 0;
        *Native<unsigned char**>(0x1379E14) = network;
        *reinterpret_cast<void**>(network + 0xa18) = owner;
        *reinterpret_cast<void**>(network + 0x2c) = core;
        PumpOfflineBootstrap();
        check(trace_is(mode < 2 || mode == 8 ? "MCO" : mode < 5 ? "M" : "MC"));
        check(!pump_active && !natural_updates_seen);
        if (mode == 8) check(network_getter_calls == 1); // No post-destruction owner work.
    }
    *Native<unsigned char**>(0x1379E14) = network;
    *reinterpret_cast<void**>(network + 0xa18) = owner;
    *reinterpret_cast<void**>(network + 0x2c) = core;
    pump_mode = 0;
#ifdef S7_LOCAL_NETWORK_PUMP
    alignas(4) unsigned char manager[4]{};
    *reinterpret_cast<uintptr_t*>(manager) = image_base + 0x10934EC - PreferredBase;
    check(FakeJump(0xCA6380, reinterpret_cast<const void*>(&FakeManagerUpdate)));
    for (int mode : {0, 9, 10, 11, 12}) {
        *Native<unsigned char**>(0x1379E14) = network;
        *reinterpret_cast<void**>(owner + 0x100) = manager;
        pump_mode = mode; pump_trace.clear();
        auto before = manager_update_count;
        PumpOfflineBootstrap();
        check(trace_is(mode == 9 ? "MJCO" : mode == 10 || mode == 0 ? "MCJO" : "MCJ"));
        check(manager_update_count == before + 1 && !pump_active && !natural_updates_seen);
    }
    *Native<unsigned char**>(0x1379E14) = network;
    *reinterpret_cast<void**>(owner + 0x100) = manager;
    check(FakeJump(0xBAF5D0, reinterpret_cast<const void*>(&FakeEchoUpdate)));
    NativeResult echo_result{};
    auto echo_before = echo_update_count;
    check(UpdateLabEcho(&echo_result, 3, -1) == &echo_result && echo_result.code == 2 && echo_result.detail == 78);
    check(echo_update_count == echo_before + 1);
    network[0x5c + 0x4c7] = 1;
    *reinterpret_cast<unsigned int*>(network + 0x5c + 0x400) = 1;
    for (int mode : {0, 13, 14, 15}) {
        *Native<unsigned char**>(0x1379E14) = network;
        *reinterpret_cast<void**>(network + 0x2c) = core;
        pump_mode = mode; pump_trace.clear(); echo_before = echo_update_count;
        PumpOfflineBootstrap();
        check(trace_is(mode == 0 ? "MCEJO" : mode == 15 ? "MECJO" : "MCE"));
        check(echo_update_count == echo_before + 1 && !pump_active);
    }
    *Native<unsigned char**>(0x1379E14) = network;
    *reinterpret_cast<void**>(network + 0x2c) = core;
    pump_mode = 0; pump_trace.clear();
    *reinterpret_cast<unsigned int*>(network + 0x5c + 0x400) = 2;
    PumpOfflineBootstrap(); check(trace_is("MCJO"));
    network[0x5c + 0x4c7] = 0;
    *reinterpret_cast<unsigned int*>(network + 0x5c + 0x400) = 1;
    check(FakeJump(0xCB90C0, reinterpret_cast<const void*>(&FakeCoreBoundary)));
    for (int mode : {0, 16, 17, 18}) {
        *Native<unsigned char**>(0x1379E14) = network;
        *reinterpret_cast<void**>(network + 0x2c) = core;
        network[0x30] = 1; network[0x5c + 0x4c7] = 1;
        pump_mode = mode; pump_trace.clear();
        PumpOfflineBootstrap();
        check(trace_is(mode == 0 ? "PMQCEJO" : mode == 16 ? "P" : mode == 17 ? "PMQ" : "PMCEJO"));
        check(!pump_active && !natural_updates_seen);
    }
    *Native<unsigned char**>(0x1379E14) = network;
    *reinterpret_cast<void**>(network + 0x2c) = core;
    network[0x30] = 0; network[0x5c + 0x4c7] = 0;
    pump_mode = 0; pump_trace.clear();
    UpdateLocalJoinWait(network, nullptr); check(trace_is("MCJO") && !pump_active);
    pump_mode = 11; pump_trace.clear();
    UpdateLocalJoinWait(network, nullptr); check(trace_is("MCJ") && !pump_active);
    *Native<unsigned char**>(0x1379E14) = network;
    pump_mode = 0; local_connect_probe = false; pump_trace.clear();
    UpdateLocalJoinWait(network, nullptr); check(trace_is("M"));
    local_connect_probe = true; natural_updates_seen = true; pump_trace.clear();
    UpdateLocalJoinWait(network, nullptr); check(trace_is("MCJO"));
    natural_updates_seen = false;
    bootstrap_thread = 0; pump_trace.clear();
    UpdateLocalJoinWait(network, nullptr); check(trace_is("M"));
    pump_trace.clear();
    auto manager_before = manager_update_count;
    PumpOfflineBootstrap(); check(trace_is("") && manager_update_count == manager_before);
    TickFakeManager(); check(trace_is("J") && manager_update_count == manager_before);
    bootstrap_thread = GetCurrentThreadId();
    *reinterpret_cast<void**>(owner + 0x100) = owner;
    pump_mode = 0; pump_trace.clear();
    PumpOfflineBootstrap(); check(trace_is("MCO"));
    pump_trace.clear();
    // Reproduce the live host failure: an isolated natural owner callback must
    // not permanently starve queued core work after lobby creation.
    UpdateLocalGame(owner, nullptr);
    check(natural_updates_seen);
    pending_core_tasks = 3; pump_trace.clear();
    PumpOfflineBootstrap(); check(trace_is("MCO") && pending_core_tasks == 0);
    // A genuine core tick suppresses the next fallback, then a stopped core
    // resumes on the following call. No permanent natural-update latch remains.
    NativeResult external_result{};
    pending_core_tasks = 2;
    FakeCoreUpdate(core, nullptr, &external_result, 3);
    check(pending_core_tasks == 0);
    pump_trace.clear(); PumpOfflineBootstrap(); check(trace_is(""));
    pending_core_tasks = 2;
    PumpOfflineBootstrap(); check(trace_is("MCO") && pending_core_tasks == 0);
    natural_updates_seen = false; pump_trace.clear();
#endif
    pump_trace.clear(); natural_updates_seen = true;
    PumpOfflineBootstrap();
#ifdef S7_LOCAL_NETWORK_PUMP
    check(trace_is("MCO")); pump_trace.clear();
#else
    check(trace_is(""));
#endif
    natural_updates_seen = false; bootstrap_thread = 0;
    PumpOfflineBootstrap(); check(trace_is(""));
    bootstrap_thread = GetCurrentThreadId(); local_connect_probe = false;
    PumpOfflineBootstrap(); check(trace_is(""));
    local_connect_probe = true;
    *reinterpret_cast<void**>(facade + 0x0c) = owner;
    PumpOfflineBootstrap(); check(trace_is("")); // Never pump a borrowed Ubi facade.
    *Native<unsigned char**>(0x1379E14) = nullptr;
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("%s %d bootstrap patch checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
