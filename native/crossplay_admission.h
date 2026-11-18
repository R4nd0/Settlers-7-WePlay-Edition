#pragma once
#include "crossplay_participant.h"
#include "crossplay_group_roster.h"
#include "crossplay_session_config.h"

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
    // Copy the already authenticated peer setup only while the actual native
    // roster remains bound. Does not construct/enqueue events or grant Ready.
    bool CapturePeerSetup(Participant& peer, CrossplayCastleSetup::Value& castle, uint32_t& extra);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

struct RoomRoute {
    uint32_t source_cid=UINT32_MAX, receiver_cid=UINT32_MAX, player_slot=0;
    bool host=false, local=false;
    Crossplay::Edition receiver{};
    Crossplay::Peer peer;
};

// Owns one group admission interval on the native game thread. The caller
// starts an original local lobby with the agreed map before Begin. The host's
// map determines which native slots the members may occupy.
class RoomLobbyAdmission {
public:
    RoomLobbyAdmission();
    ~RoomLobbyAdmission();
    RoomLobbyAdmission(const RoomLobbyAdmission&)=delete;
    RoomLobbyAdmission& operator=(const RoomLobbyAdmission&)=delete;
    bool Begin(Crossplay::Channel& channel,unsigned char local_transport_slot,
        const std::array<unsigned char,CrossplayGroupRoster::Members>& map_slots,
        const void* own_castle,uint32_t extra);
    bool Poll();
    AdmissionState GetState() const;
    // A route is usable only in the original session whose full roster still
    // matches. A started session is verified separately from lobby phase.
    bool Resolve(unsigned char source_slot,bool started,RoomRoute& route) const;
    bool CaptureConfigurationRoutes(unsigned char capacity,CrossplaySessionConfig::RoomConnections& routes) const;
    bool CaptureMemberSetup(uint32_t map_slot,Participant& player,CrossplayCastleSetup::Value& castle,uint32_t& extra) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
