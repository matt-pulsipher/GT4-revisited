#pragma once

// Finding a lobby on the local network (docs/specs/net.md "Finding a lobby"): a host answers questions on the first free
// of four UDP ports; a guest asks them all, on every network's broadcast address and on this machine. The answers name the
// lobby and the port to join. Nothing leaves the local network, and nothing is sent but the answer to a question.

#include "gt4r/base/types.h"

#include <string>
#include <vector>

namespace gt4r::net {

constexpr u16 kLanPort = 47634;       // the session's (the shared engine's games use 47630)
constexpr u16 kDiscoveryPort = 47640; // .. + 3
constexpr int kDiscoveryPorts = 4;

// A lobby found.
struct FoundLobby {
    std::string address; // "192.168.1.5:47634", to join
    std::string name;    // the host's
    std::string disc;    // its disc's id (net::online_disc)
    int players = 0, limit = 6;
    bool open = true;    // not racing
    double heard_ms = 0; // its last answer (gt3r::net::now_ms)
};

// Both sides; each used by one of them.
class Discovery {
public:
    Discovery() = default;
    ~Discovery() { close(); }
    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;

    // The host: answers from now on. False when none of the ports is free (the lobby is then joined by its address).
    bool host(bool loopback = false);
    // The guest: asks from now on. `loopback`: this machine only (tests, and runs that must not reach the network).
    bool search(bool loopback);
    void close();
    bool open() const { return fd_ != kNone; }

    // What the host answers.
    struct Answer {
        u16 port = kLanPort;
        std::string name, disc;
        int players = 1, limit = 6;
        bool open = true;
    };
    // Each frame: the host's answers; the guest's questions (one a second) and what came back.
    void poll(const Answer& answer);
    void poll();
    // The lobbies heard from in the last four seconds.
    std::vector<FoundLobby> found() const;

private:
#if defined(_WIN32)
    using Fd = unsigned long long;
    static constexpr Fd kNone = ~0ull;
#else
    using Fd = int;
    static constexpr Fd kNone = -1;
#endif
    bool open_socket();
    void receive(const Answer* answer);
    Fd fd_ = kNone;
    bool loopback_ = false, hosting_ = false;
    double asked_ms_ = -1e9;
    u32 nonce_ = 0;
    std::vector<FoundLobby> found_;
};

} // namespace gt4r::net
