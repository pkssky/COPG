// COPG open-source engine — per-app SOCKS5 proxy (:proxy / :proxy=<key>).
//
// PLT-hooks socket()/connect()/close() so an app's outgoing TCP connections
// are transparently tunneled through the SOCKS5 endpoint configured in
// PROXY/manifest.json:
//   app:  connect(fd, target)          hook: connect(fd, proxy)
//                                          SOCKS5 greeting (+ user/pass)
//                                          SOCKS5 CONNECT <target>
//                                          return 0  — the app believes it
//                                          is connected to the target
//   app:  send/recv on fd                   relayed end-to-end by the proxy
//
// connect() does not receive the fd, so the hook tracks sockets created on
// the calling thread — socket() immediately followed by connect() is the
// pattern virtually every client uses. The fd is only proxied when it can be
// identified unambiguously (exactly one unconnected tracked socket on this
// thread, still open, still a stream socket, still without a peer); every
// other case goes out direct. A thread that never created a tracked socket
// (e.g. connections made on another thread's fd) is never touched.
//
// When the proxy is unreachable or refuses the CONNECT, the hook falls back
// to a direct connection (fresh socket dup2'd onto the app's fd), so the app
// keeps working — just without the proxy.
//
// Not covered (documented): UDP (no SOCKS5 UDP ASSOCIATE), connections made
// through raw syscalls, and sockets created on a different thread than the
// one that connects them.
#include "log.hpp"
#include "paths.hpp"
#include "proxy.hpp"
#include "proxy_spoof.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace copg {
namespace proxy {
namespace {

using socket_fn = int (*)(int, int, int);
using connect_fn = int (*)(const struct sockaddr *, socklen_t);
using close_fn = int (*)(int);

socket_fn g_real_socket = nullptr;
connect_fn g_real_connect = nullptr;
close_fn g_real_close = nullptr;

bool g_armed = false;
ProxyEntry g_ep;                      // armed endpoint (valid when g_armed)
sockaddr_storage g_proxy_addr{};      // resolved endpoint
socklen_t g_proxy_addrlen = 0;
bool g_proxy_resolved = false;        // false = resolution failed; retry lazily

constexpr int kConnectTimeoutMs = 15000;  // proxy TCP connect budget
constexpr int kIoTimeoutSec = 10;         // per send/recv during the handshake

// ── thread-local socket tracking ────────────────────────────────────────────
// pending_fd:    most recent socket() result on this thread that has not been
//                connected yet (-1 = none)
// pending_count: how many tracked sockets are still unconnected on this
//                thread. Only 1 makes the fd unambiguous; 0 or >1 means we
//                cannot tell which fd connect() targets -> pass through.
struct TlsState {
    int pending_fd = -1;
    int pending_count = 0;
};
thread_local TlsState t_tls;

void tls_reset() {
    t_tls.pending_fd = -1;
    t_tls.pending_count = 0;
}

// ── small socket helpers ────────────────────────────────────────────────────

bool addr_is_loopback(const sockaddr *sa) {
    if (!sa) return true;
    if (sa->sa_family == AF_INET) {
        const auto *a = reinterpret_cast<const sockaddr_in *>(sa);
        uint32_t h = ntohl(a->sin_addr.s_addr);
        return (h >> 24) == 127;
    }
    if (sa->sa_family == AF_INET6) {
        const auto *a6 = reinterpret_cast<const sockaddr_in6 *>(sa);
        return IN6_IS_ADDR_LOOPBACK(&a6->sin6_addr);
    }
    return true;  // AF_UNIX & friends: never proxy
}

// True when `sa` addresses our own proxy (avoid connect loops).
bool addr_is_proxy(const sockaddr *sa) {
    if (!sa || !g_proxy_resolved) return false;
    if (sa->sa_family != g_proxy_addr.ss_family) return false;
    if (sa->sa_family == AF_INET) {
        const auto *a = reinterpret_cast<const sockaddr_in *>(sa);
        const auto *p = reinterpret_cast<const sockaddr_in *>(&g_proxy_addr);
        return a->sin_port == p->sin_port &&
               a->sin_addr.s_addr == p->sin_addr.s_addr;
    }
    if (sa->sa_family == AF_INET6) {
        const auto *a = reinterpret_cast<const sockaddr_in6 *>(sa);
        const auto *p = reinterpret_cast<const sockaddr_in6 *>(&g_proxy_addr);
        return a->sin6_port == p->sin6_port &&
               memcmp(&a->sin6_addr, &p->sin6_addr, sizeof(in6_addr)) == 0;
    }
    return false;
}

bool write_exact(int fd, const void *buf, size_t len) {
    const char *p = static_cast<const char *>(buf);
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, p + off, len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool read_exact(int fd, void *buf, size_t len) {
    char *p = static_cast<char *>(buf);
    size_t off = 0;
    while (off < len) {
        ssize_t n = recv(fd, p + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

// Perform the SOCKS5 greeting (+auth) and CONNECT for `target` on `fd`.
// `fd` is already connected to the proxy. Returns true on success.
bool socks5_handshake(int fd, const sockaddr *target) {
    unsigned char buf[64];

    const bool use_auth = !g_ep.user.empty() || !g_ep.pass.empty();
    // Greeting: VER=5, N methods, methods (0x00 none / 0x02 user/pass).
    if (use_auth) {
        unsigned char greet[] = {0x05, 0x02, 0x00, 0x02};
        if (!write_exact(fd, greet, sizeof(greet))) return false;
    } else {
        unsigned char greet[] = {0x05, 0x01, 0x00};
        if (!write_exact(fd, greet, sizeof(greet))) return false;
    }

    // Method selection: VER, METHOD.
    if (!read_exact(fd, buf, 2)) return false;
    if (buf[0] != 0x05) return false;
    const unsigned char method = buf[1];
    if (method == 0xFF) {
        COPG_LOGW("proxy: server rejected all auth methods");
        return false;
    }

    // Username/password auth (RFC 1929) when the server picked it.
    if (method == 0x02) {
        if (!use_auth) {
            COPG_LOGW("proxy: server wants user/pass but none configured");
            return false;
        }
        const std::string &u = g_ep.user;
        const std::string &p = g_ep.pass;
        if (u.size() > 255 || p.size() > 255) return false;
        std::string req;
        req.push_back(0x01);
        req.push_back(static_cast<char>(u.size()));
        req += u;
        req.push_back(static_cast<char>(p.size()));
        req += p;
        if (!write_exact(fd, req.data(), req.size())) return false;
        if (!read_exact(fd, buf, 2)) return false;
        if (buf[0] != 0x01 || buf[1] != 0x00) {
            COPG_LOGW("proxy: user/pass auth failed (status 0x%02x)", buf[1]);
            return false;
        }
    }

    // CONNECT request: VER=5, CMD=1 (connect), RSV=0, ATYP, ADDR, PORT.
    unsigned char req[22];
    size_t req_len = 0;
    req[0] = 0x05;
    req[1] = 0x01;
    req[2] = 0x00;
    if (target->sa_family == AF_INET) {
        const auto *a = reinterpret_cast<const sockaddr_in *>(target);
        req[3] = 0x01;  // IPv4
        memcpy(req + 4, &a->sin_addr, 4);
        memcpy(req + 8, &a->sin_port, 2);
        req_len = 10;
    } else if (target->sa_family == AF_INET6) {
        const auto *a6 = reinterpret_cast<const sockaddr_in6 *>(target);
        req[3] = 0x04;  // IPv6
        memcpy(req + 4, &a6->sin6_addr, 16);
        memcpy(req + 20, &a6->sin6_port, 2);
        req_len = 22;
    } else {
        return false;
    }
    if (!write_exact(fd, req, req_len)) return false;

    // Reply: VER, REP, RSV, ATYP, BND.ADDR, BND.PORT. REP != 0 = failure.
    if (!read_exact(fd, buf, 4)) return false;
    if (buf[0] != 0x05 || buf[1] != 0x00) {
        COPG_LOGW("proxy: CONNECT refused (reply 0x%02x)", buf[1]);
        return false;
    }
    size_t bnd_len = 0;
    switch (buf[3]) {
        case 0x01: bnd_len = 4 + 2; break;                    // IPv4
        case 0x04: bnd_len = 16 + 2; break;                   // IPv6
        case 0x03: {                                          // domain name
            unsigned char len = 0;
            if (!read_exact(fd, &len, 1)) return false;
            bnd_len = static_cast<size_t>(len) + 2;
            break;
        }
        default: return false;
    }
    if (bnd_len > sizeof(buf)) bnd_len = sizeof(buf);
    return read_exact(fd, buf, bnd_len);  // drain BND.ADDR/PORT
}

// Connect `fd` to the proxy with a bounded wait. Returns true when connected.
// The socket is left BLOCKING on success — the caller performs the SOCKS5
// handshake on it and restores the app's original flags afterwards.
bool connect_to_proxy(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = g_real_connect(reinterpret_cast<const sockaddr *>(&g_proxy_addr),
                            g_proxy_addrlen);
    bool ok = false;
    if (rc == 0) {
        ok = true;
    } else if (errno == EINPROGRESS) {
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int pr = poll(&pfd, 1, kConnectTimeoutMs);
        if (pr == 1 && (pfd.revents & POLLOUT)) {
            int err = 0;
            socklen_t el = sizeof(err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0)
                ok = true;
        }
    }
    if (ok) {
        if (flags >= 0) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);  // block for the handshake
    } else {
        if (flags >= 0) fcntl(fd, F_SETFL, flags);  // restore; fallback will re-point fd
        errno = EHOSTUNREACH;
    }
    return ok;
}

// Re-point the app's fd at a fresh socket connected DIRECTLY to `target`.
// A TCP socket whose connect() failed cannot be reconnected on Linux, so we
// dup2 a new socket onto the app's fd number (the app never sees the swap).
// Returns the direct connect()'s result (0 = success).
int fallback_direct(int fd, const sockaddr *target, socklen_t len, int sock_flags,
                    int type, int protocol) {
    COPG_LOGW("proxy: endpoint unreachable, falling back to a direct connection");
    int nfd = g_real_socket(target->sa_family, type, protocol);
    if (nfd < 0) return -1;
    // dup2 shares status flags with the open file description; make the new
    // socket blocking for the direct connect, then restore the app's flags.
    fcntl(nfd, F_SETFL, sock_flags & ~O_NONBLOCK);
    if (dup2(nfd, fd) < 0) {
        g_real_close(nfd);
        return -1;
    }
    g_real_close(nfd);
    int rc = g_real_connect(target, len);
    if (sock_flags >= 0) fcntl(fd, F_SETFL, sock_flags);
    return rc;
}

// Try to resolve the endpoint host once. Numeric IPs never fail; hostnames
// are resolved lazily so a transient DNS failure does not kill the feature.
bool resolve_endpoint() {
    if (g_proxy_resolved) return true;
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", g_ep.port);
    if (getaddrinfo(g_ep.host.c_str(), portbuf, &hints, &res) != 0 || !res) {
        g_proxy_resolved = false;
        return false;
    }
    memcpy(&g_proxy_addr, res->ai_addr, res->ai_addrlen);
    g_proxy_addrlen = res->ai_addrlen;
    freeaddrinfo(res);
    g_proxy_resolved = true;
    return true;
}

// ── hooks ───────────────────────────────────────────────────────────────────

int hook_socket(int domain, int type, int protocol) {
    int fd = g_real_socket(domain, type, protocol);
    if (fd >= 0 && g_armed) {
        const int sock_type = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (sock_type == SOCK_STREAM) {
            // Only stream sockets can be SOCKS5-proxied; track the newest.
            t_tls.pending_fd = fd;
            ++t_tls.pending_count;
        }
    }
    return fd;
}

int hook_close(int fd) {
    if (g_armed && fd == t_tls.pending_fd) tls_reset();
    return g_real_close(fd);
}

int hook_connect(const sockaddr *sa, socklen_t len) {
    if (!g_armed || !g_real_connect || !sa || len < sizeof(sockaddr) ||
        (sa->sa_family != AF_INET && sa->sa_family != AF_INET6) ||
        addr_is_loopback(sa) || addr_is_proxy(sa)) {
        return g_real_connect(sa, len);
    }

    const int saved_errno = errno;

    // Identify the fd: exactly one unconnected tracked socket on this thread.
    int fd = t_tls.pending_fd;
    if (fd < 0 || t_tls.pending_count != 1) {
        errno = saved_errno;
        return g_real_connect(sa, len);
    }

    int type = 0;
    socklen_t tl = sizeof(type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tl) != 0) {
        tls_reset();  // stale fd (closed without close())
        errno = saved_errno;
        return g_real_connect(sa, len);
    }
    const int sock_type = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (sock_type != SOCK_STREAM) {
        tls_reset();
        errno = saved_errno;
        return g_real_connect(sa, len);
    }
    sockaddr peer{};
    socklen_t pl = sizeof(peer);
    if (getpeername(fd, &peer, &pl) == 0 || errno != ENOTCONN) {
        // Already connected (or not a connectable socket): not ours to proxy.
        tls_reset();
        errno = saved_errno;
        return g_real_connect(sa, len);
    }
    int protocol = 0;
    socklen_t prl = sizeof(protocol);
    getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &prl);
    int sock_flags = fcntl(fd, F_GETFL);
    if (sock_flags < 0) sock_flags = 0;

    if (!resolve_endpoint()) {
        COPG_LOGD("proxy: endpoint host unresolved, going direct");
        tls_reset();
        errno = saved_errno;
        return g_real_connect(sa, len);
    }

    // ── connect the fd to the proxy ─────────────────────────────────────────
    if (!connect_to_proxy(fd)) {
        int rc = fallback_direct(fd, sa, len, sock_flags, sock_type, protocol);
        errno = rc == 0 ? saved_errno : errno;
        return rc;
    }

    // ── SOCKS5 handshake with a bounded I/O timeout ─────────────────────────
    struct timeval tv{}, saved_rcv{}, saved_snd{};
    socklen_t tvl = sizeof(tv);
    getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &saved_rcv, &tvl);
    tvl = sizeof(tv);
    getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &saved_snd, &tvl);
    tv.tv_sec = kIoTimeoutSec;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    bool ok = socks5_handshake(fd, sa);

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &saved_rcv, sizeof(saved_rcv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &saved_snd, sizeof(saved_snd));

    tls_reset();

    if (ok) {
        if (sock_flags >= 0) fcntl(fd, F_SETFL, sock_flags);  // restore O_NONBLOCK
        errno = saved_errno;
        COPG_LOGD("proxy: %s fd=%d via %s:%d", sa->sa_family == AF_INET6 ? "v6" : "v4",
                  fd, g_ep.host.c_str(), g_ep.port);
        return 0;
    }

    // Handshake failed: the fd is connected to the proxy but the app expects
    // the target — swap it for a fresh direct connection instead.
    int rc = fallback_direct(fd, sa, len, sock_flags, sock_type, protocol);
    errno = rc == 0 ? saved_errno : errno;
    return rc;
}

}  // namespace

bool set_endpoint(const std::string &key) {
    const ProxyEntry *e = proxy_lookup(key, kProxyManifest);
    if (!e) {
        COPG_LOGW("proxy: key '%s' not found in %s", key.c_str(), kProxyManifest);
        return false;
    }
    g_ep = *e;
    g_armed = true;
    g_proxy_resolved = false;
    if (!resolve_endpoint())
        COPG_LOGW("proxy: cannot resolve '%s' yet (will retry per connect)",
                  g_ep.host.c_str());
    else
        COPG_LOGI("proxy: endpoint armed %s:%d%s", g_ep.host.c_str(), g_ep.port,
                  (!g_ep.user.empty() || !g_ep.pass.empty()) ? " (auth)" : "");
    return true;
}

void register_hooks(zygisk::Api *api) {
    if (!g_armed) return;
    api->pltHookRegister(".*", "socket", reinterpret_cast<void *>(hook_socket),
                         reinterpret_cast<void **>(&g_real_socket));
    api->pltHookRegister(".*", "connect", reinterpret_cast<void *>(hook_connect),
                         reinterpret_cast<void **>(&g_real_connect));
    api->pltHookRegister(".*", "close", reinterpret_cast<void *>(hook_close),
                         reinterpret_cast<void **>(&g_real_close));
}

}  // namespace proxy
}  // namespace copg
