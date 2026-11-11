#pragma once
#include "crossplay_participant.h"

namespace CrossplayNative {
enum class AdmissionState { New, AwaitingPeer, Applying, AwaitingCommit, RosterBound, Failed };
// One two-player roster interval. RosterBound describes matching native member
// lists and roles; the preparation frame and visible lobby can still differ.
// It never establishes readiness, content admission or gameplay transport.
class LobbyAdmission {
public:
    LobbyAdmission();
    ~LobbyAdmission();
    LobbyAdmission(const LobbyAdmission&) = delete;
    LobbyAdmission& operator=(const LobbyAdmission&) = delete;
    // Both launches first create their own native lobby. A shared host/guest
    // role then assigns slots through the authenticated common backend.
    // Call on the native owner thread with an original local Confirmed castle.
    bool Begin(Crossplay::Channel& channel, Crossplay::Edition edition,
               bool common_host, const void* own_castle, uint32_t extra);
    bool Poll();
    AdmissionState GetState() const;
    uint32_t OwnConnection() const;
    uint32_t PeerSourceConnection() const;
    uint32_t PeerLocalConnection() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
