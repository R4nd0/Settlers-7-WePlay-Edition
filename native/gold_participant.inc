// Supply our independent identity when the authenticated local NetZ player is
// originally created. Later preparation-record edits are overwritten by its
// metadata. The native constructor owns the string copy and object-ID result.
bool AccountParticipantNameEligible(void* session, const GoldStartup::AsciiString* name,
    unsigned int principal, const char** phase = nullptr) {
    const auto denied = [phase](const char* reason) { if (phase) *phase = reason; return false; };
    if (GetCurrentThreadId() != owner_thread.load()) return denied("participant-thread-excluded");
    if (!AccountLocalSceneAllowed()) return denied("participant-account-excluded");
    if (!ValidRange(name, sizeof(*name)) || name->capacity > 4096
        || (name->bytes && !ValidRange(name->bytes, 1))) return denied("participant-name-layout-excluded");
    if (name->length || (name->bytes && *static_cast<const char*>(name->bytes)))
        return denied("participant-existing-name-retained");
    const uintptr_t main = *reinterpret_cast<uintptr_t*>(image + 0xcf83dc);
    if (*reinterpret_cast<void**>(main + 0x14) != session || !ValidRange(session, 0x44)
        || *static_cast<uintptr_t*>(session) != image + 0xb569bc
        || !AccountIsService(reinterpret_cast<uintptr_t>(session))) return denied("participant-session-excluded");
    // Original host predicate resolves CRendezVous, not account management's
    // unrelated protocol client. Its authenticated ClientContext is also the
    // context consumed by MatchMakingClient in both guarded creation callers.
    const uintptr_t rendezvous = LocalLoginRendezVous(false);
    if (!rendezvous) return denied("participant-rendezvous-excluded");
    const uintptr_t context = *reinterpret_cast<uintptr_t*>(rendezvous + 0x78);
    if (!principal || !ValidRange(reinterpret_cast<void*>(context), 12)
        || *reinterpret_cast<unsigned int*>(context + 8) != principal)
        return denied("participant-principal-excluded");
    return true;
}
unsigned int __stdcall AccountCreateParticipant(void* session, const GoldStartup::AsciiString* name,
    unsigned int field70, unsigned int field7c, unsigned int principal,
    unsigned int flag75, unsigned int flag74) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (caller != image + 0x686752 && caller != image + 0x687bf1) Stop();
    const char* phase = "account-local-participant-identity-supplied";
    const bool supplied = AccountParticipantNameEligible(session, name, principal, &phase);
    const unsigned int length = static_cast<unsigned int>(strlen(local_login_name));
    const GoldStartup::AsciiString local{local_login_name, length, length, 0};
    using Original = unsigned int (__stdcall*)(void*, const GoldStartup::AsciiString*,
        unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
    const unsigned int result = reinterpret_cast<Original>(image + 0x652610)(session,
        supplied ? &local : name, field70, field7c, principal, flag75, flag74);
    const DWORD error = GetLastError();
    LocalLoginReport(phase);
    SetLastError(error);
    return result;
}
