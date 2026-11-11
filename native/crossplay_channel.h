#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Experimental loopback transport only. Connected means an authenticated
// channel, never a native participant, lobby, Ready result or running match.
namespace Crossplay {
using Id = std::array<unsigned char,16>;
using Digest = std::array<unsigned char,32>;
enum class Edition : unsigned char { Gold=1, UbisoftHistory=2, SteamHistory=3 };
enum class State { New, Connecting, Authenticating, Connected, Draining, Closed };
struct Config {
    uint16_t port = 0;
    unsigned char slot = 0;
    Edition edition{}, expected_peer{};
    Id session{}, native_identity{};
    Digest key{}, executable{}, content{};
};
struct Peer {
    Edition edition{};
    Id local_id{}, remote_id{}, native_identity{};
    // Session-scoped WePlay projections, never a cast or vendor identity.
    Id peer_in_local_namespace{}, local_in_peer_namespace{};
};
class Channel {
public:
    static constexpr size_t MaximumPayload = 65536;
    static constexpr size_t MaximumBuffered = 8*1024*1024;
    Channel();
    ~Channel();
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    bool Start(const Config& config);
    bool Poll();
    bool Send(const std::vector<unsigned char>& bytes);
    bool Receive(std::vector<unsigned char>& bytes);
    bool HasPendingWrites() const;
    void Close();
    State GetState() const;
    // Owner-thread view, valid only until the next Poll/Close. Copy bindings
    // before passing them to a separately guarded native session projection.
    const Peer* GetPeer() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
