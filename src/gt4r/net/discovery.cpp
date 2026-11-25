#include "gt4r/net/discovery.h"

#include "net/protocol.h"
#include "net/socket.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#if !defined(__ANDROID__) || __ANDROID_API__ >= 24
#include <ifaddrs.h>
#include <net/if.h>
#define GT4R_IFADDRS 1
#endif
#endif

namespace gt4r::net {

namespace {

constexpr char kMagic[4] = {'G', 'T', '4', 'L'};
constexpr u8 kVersion = 1, kAsk = 1, kTell = 2;

gt3r::net::Writer message(u8 type, u32 nonce) {
    gt3r::net::Writer w;
    for (char c : kMagic) w.u8_(u8(c));
    w.u8_(kVersion), w.u8_(type), w.u32_(nonce);
    return w;
}

// Every IPv4 network's broadcast address, and the general one.
std::vector<u32> broadcast_addresses() {
    std::vector<u32> out{0xFFFFFFFFu};
#if defined(GT4R_IFADDRS)
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_BROADCAST) || !(a->ifa_flags & IFF_UP) || !a->ifa_netmask) continue;
            const u32 ip = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr), mask = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
            const u32 b = ip | ~mask;
            if (std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
        }
        freeifaddrs(list);
    }
#endif
    return out;
}

} // namespace

bool Discovery::open_socket() {
    close();
    // (the engine's socket layer starts the platform's: Winsock)
    gt3r::net::UdpSocket starter;
    starter.open(0);
    const auto fd = ::socket(AF_INET, SOCK_DGRAM, 0);
#if defined(_WIN32)
    if (fd == INVALID_SOCKET) return false;
    u_long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#else
    if (fd < 0) return false;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
    fd_ = Fd(fd);
    return true;
}

bool Discovery::host(bool loopback) {
    if (!open_socket()) return false;
    for (int i = 0; i < kDiscoveryPorts; ++i) {
        sockaddr_in a{};
        a.sin_family = AF_INET, a.sin_port = htons(u16(kDiscoveryPort + i)), a.sin_addr.s_addr = htonl(loopback ? INADDR_LOOPBACK : INADDR_ANY);
        if (::bind(fd_, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0) {
            hosting_ = true;
            return true;
        }
    }
    close();
    return false;
}

bool Discovery::search(bool loopback) {
    if (!open_socket()) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET, a.sin_port = 0, a.sin_addr.s_addr = htonl(loopback ? INADDR_LOOPBACK : INADDR_ANY);
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
        close();
        return false;
    }
    hosting_ = false, loopback_ = loopback;
    nonce_ = u32(gt3r::net::now_ms() * 1000) | 1;
    asked_ms_ = -1e9;
    found_.clear();
    return true;
}

void Discovery::close() {
    if (fd_ == kNone) return;
#if defined(_WIN32)
    closesocket(fd_);
#else
    ::close(fd_);
#endif
    fd_ = kNone;
    hosting_ = false;
}

void Discovery::receive(const Answer* answer) {
    u8 buf[512];
    for (int n = 0; n < 64; ++n) {
        sockaddr_in from{};
        socklen_t len = sizeof from;
        const auto got = ::recvfrom(fd_, reinterpret_cast<char*>(buf), sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
        if (got <= 0) return;
        gt3r::net::Reader r(ByteSpan(buf, size_t(got)));
        bool ours = true;
        for (char c : kMagic) ours = r.u8_() == u8(c) && ours;
        const u8 version = r.u8_(), type = r.u8_();
        const u32 nonce = r.u32_();
        if (!ours || version != kVersion || !r.ok()) continue;
        if (answer && type == kAsk) {
            gt3r::net::Writer w = message(kTell, nonce);
            w.u16_(answer->port);
            w.str(answer->name.substr(0, 32)), w.str(answer->disc.substr(0, 80));
            w.u8_(u8(answer->players)), w.u8_(u8(answer->limit)), w.u8_(answer->open ? 1 : 0);
            ::sendto(fd_, reinterpret_cast<const char*>(w.data.data()), int(w.data.size()), 0, reinterpret_cast<const sockaddr*>(&from), len);
        } else if (!answer && type == kTell && nonce == nonce_) {
            FoundLobby f;
            const u16 port = r.u16_();
            f.name = r.str(), f.disc = r.str();
            f.players = r.u8_(), f.limit = r.u8_(), f.open = r.u8_() != 0;
            if (!r.ok() || f.name.empty()) continue;
            char ip[64] = {};
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
            f.address = std::string(ip) + ":" + std::to_string(port);
            f.heard_ms = gt3r::net::now_ms();
            // (a machine heard on several of its addresses: the first one stands, this machine's own last)
            auto same = [&](const FoundLobby& o) { return o.address == f.address; };
            if (auto it = std::find_if(found_.begin(), found_.end(), same); it != found_.end()) *it = f;
            else if (found_.size() < 32) found_.push_back(f);
        }
    }
}

void Discovery::poll(const Answer& answer) {
    if (open() && hosting_) receive(&answer);
}

void Discovery::poll() {
    if (!open() || hosting_) return;
    const double now = gt3r::net::now_ms();
    if (now - asked_ms_ >= 1000) {
        asked_ms_ = now;
        const gt3r::net::Writer w = message(kAsk, nonce_);
        std::vector<u32> to{INADDR_LOOPBACK};
        if (!loopback_)
            for (u32 b : broadcast_addresses()) to.push_back(b);
        for (u32 ip : to)
            for (int i = 0; i < kDiscoveryPorts; ++i) {
                sockaddr_in a{};
                a.sin_family = AF_INET, a.sin_port = htons(u16(kDiscoveryPort + i)), a.sin_addr.s_addr = htonl(ip);
                ::sendto(fd_, reinterpret_cast<const char*>(w.data.data()), int(w.data.size()), 0, reinterpret_cast<const sockaddr*>(&a), sizeof a);
            }
    }
    receive(nullptr);
}

std::vector<FoundLobby> Discovery::found() const {
    std::vector<FoundLobby> out;
    const double now = gt3r::net::now_ms();
    for (const FoundLobby& f : found_) {
        if (now - f.heard_ms > 4000) continue;
        // (this machine's own lobby answers on its network address too: one of them)
        const size_t colon = f.address.rfind(':');
        const bool local = f.address.rfind("127.", 0) == 0;
        const bool twice = std::any_of(found_.begin(), found_.end(), [&](const FoundLobby& o) {
            return &o != &f && local && o.name == f.name && now - o.heard_ms <= 4000 && o.address.substr(o.address.rfind(':')) == f.address.substr(colon) && o.address.rfind("127.", 0) != 0;
        });
        if (!twice) out.push_back(f);
    }
    return out;
}

} // namespace gt4r::net
