#pragma once
#include "crossplay_channel.h"
#include "castle_wire.h"
#include <cstdint>
#include <memory>
#include <string>

// Native event construction, not session admission. The caller must obtain
// these bindings from its session backend; a channel/account ID is not a CID.
namespace CrossplayNative {
struct Participant {
    uint32_t connection = 0, slot = 0, principal = 0, station = 0;
    std::string name;
    bool host = false, local = false;
};
struct LobbyMember { Participant player; uint32_t status = 0; };
struct LobbySnapshot {
    uintptr_t instance = 0;
    uint32_t local_slot = 0;
    bool host = false;
    std::vector<LobbyMember> members;
};
enum class Change : uint16_t { Added=2, Confirmed=3, Leaving=4, Removed=44 };
enum class ParticipantFailure { None, State, Executable, NativeMethods, Thread, Participant, Castle, Transport };

class ParticipantEvent {
public:
    ParticipantEvent();
    ~ParticipantEvent();
    ParticipantEvent(const ParticipantEvent&) = delete;
    ParticipantEvent& operator=(const ParticipantEvent&) = delete;
    // Pins the actual process executable, original constructor bytes and
    // native vtable. It can only be bound once, on the calling owner thread.
    bool Bind(Crossplay::Edition edition);
    // Confirmed requires a castle value owned by this SAME native engine.
    // Never pass a castle object from the other edition: layouts differ.
    // Other changes use their original native default-castle constructor.
    bool Construct(Change change, const Participant& participant,
                   const void* native_castle = nullptr, uint32_t extra = 0);
    bool ConstructConfirmed(const Participant& participant, const castle_wire::Design& castle, uint32_t extra = 0);
    bool CaptureCastle(const void* native_castle, castle_wire::Design& result) const;
    // Borrowed original event, valid until Reset/destruction. Does not enqueue,
    // dispatch, assert readiness or change any native session flags.
    const void* Get() const;
    // Snapshot the current native lobby on its owner thread. Backend handles
    // and process-local string pointers are never exported in the roster.
    bool CaptureLobby(LobbySnapshot& result) const;
    // Original local-only send clones the event into the current transport's
    // native queue. No NetZ/Storm send or immediate dispatcher call occurs.
    // Once per constructed event; Reset retains responsibility for our copy.
    // Caller still needs genuine session admission and native CID bindings.
    bool EnqueueLocal();
    bool Reset();
    ParticipantFailure Failure() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
