#pragma once

// Ports for a test's own hosts and relay servers on the loopback.
//
// A host's socket is dual-stack IPv6 (net/socket.h). On macOS such a socket bound to port 0 can be given a port an IPv4
// socket of any process already holds, and IPv4 datagrams to 127.0.0.1 on that port then reach the other socket, never the
// host: a join that times out. The more processes with sockets (other test runs, a busy machine), the likelier. A port
// an IPv4 socket was just given is one no IPv4 or dual-stack socket holds; the host binds it at once.

#include "net/socket.h"

namespace gt4r::test {

// A port no socket holds for IPv4 (0: none found, so any).
inline gt3r::u16 free_port() {
    gt3r::net::Address v4;
    gt3r::net::UdpSocket probe;
    if (!gt3r::net::resolve("127.0.0.1", 1, v4) || !probe.open_for(v4)) return 0;
    return probe.local_port(); // (closed as it goes)
}

// `bind(port)` on such a port, again on another should something take it first.
template <class Bind>
bool on_free_port(Bind bind) {
    constexpr int kTries = 8;
    for (int i = 0; i < kTries; ++i)
        if (const gt3r::u16 p = free_port(); p && bind(p)) return true;
    return bind(gt3r::u16(0));
}

} // namespace gt4r::test
