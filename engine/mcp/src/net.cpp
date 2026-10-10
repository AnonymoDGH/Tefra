#include "net.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace tefra::mcp::net {

namespace {

#ifdef _WIN32
using Native = SOCKET;
int last_error() { return WSAGetLastError(); }
bool bad(Native s) { return s == INVALID_SOCKET; }
#else
using Native = int;
int last_error() { return errno; }
bool bad(Native s) { return s < 0; }
#endif

Native native(Socket s) { return static_cast<Native>(s); }
Socket wrap(Native s) { return bad(s) ? kInvalidSocket : static_cast<Socket>(s); }

timeval make_timeval(int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return tv;
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL; // no SIGPIPE when the peer went away
#else
constexpr int kSendFlags = 0;
#endif

} // namespace

bool init() {
#ifdef _WIN32
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0; // every successful init() is paired with cleanup()
#else
    return true;
#endif
}

void cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

Socket listen_loopback(u16 port, u16& bound_port, bool& in_use, int& err_code) {
    bound_port = 0;
    in_use = false;
    err_code = 0;
    const Native fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (bad(fd)) {
        err_code = last_error();
        return kInvalidSocket;
    }
#ifndef _WIN32
    // Quick rebind after a restart. Not set on Windows, where SO_REUSEADDR would let a second process steal the port.
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), static_cast<socklen_t>(sizeof one));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), static_cast<socklen_t>(sizeof addr)) != 0) {
        err_code = last_error();
#ifdef _WIN32
        in_use = err_code == WSAEADDRINUSE;
#else
        in_use = err_code == EADDRINUSE;
#endif
        close_socket(wrap(fd));
        return kInvalidSocket;
    }
    if (::listen(fd, 16) != 0) {
        err_code = last_error();
        close_socket(wrap(fd));
        return kInvalidSocket;
    }
    sockaddr_in bound{};
    socklen_t len = static_cast<socklen_t>(sizeof bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len) == 0) bound_port = ntohs(bound.sin_port);
    return wrap(fd);
}

Socket connect_loopback(u16 port, int timeout_ms, int& err_code) {
    err_code = 0;
    const Native fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (bad(fd)) {
        err_code = last_error();
        return kInvalidSocket;
    }
    const Socket s = wrap(fd);
    set_timeouts(s, timeout_ms);
    set_nodelay(s);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), static_cast<socklen_t>(sizeof addr)) != 0) {
        err_code = last_error();
        close_socket(s);
        return kInvalidSocket;
    }
    return s;
}

Socket accept_client(Socket listener) {
    const Native fd = ::accept(native(listener), nullptr, nullptr);
    return wrap(fd);
}

void set_timeouts(Socket s, int timeout_ms) {
#ifdef _WIN32
    const DWORD ms = static_cast<DWORD>(timeout_ms);
    ::setsockopt(native(s), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms),
                 static_cast<socklen_t>(sizeof ms));
    ::setsockopt(native(s), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms),
                 static_cast<socklen_t>(sizeof ms));
#else
    const timeval tv = make_timeval(timeout_ms);
    ::setsockopt(native(s), SOL_SOCKET, SO_RCVTIMEO, &tv, static_cast<socklen_t>(sizeof tv));
    ::setsockopt(native(s), SOL_SOCKET, SO_SNDTIMEO, &tv, static_cast<socklen_t>(sizeof tv));
#endif
}

void set_nodelay(Socket s) {
    int one = 1;
    ::setsockopt(native(s), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one),
                 static_cast<socklen_t>(sizeof one));
}

void shutdown_socket(Socket s) {
#ifdef _WIN32
    ::shutdown(native(s), SD_BOTH);
#else
    ::shutdown(native(s), SHUT_RDWR);
#endif
}

void close_socket(Socket s) {
    if (s == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(native(s));
#else
    ::close(native(s));
#endif
}

int recv_some(Socket s, char* buf, int cap) {
    for (;;) {
#ifdef _WIN32
        const int n = ::recv(native(s), buf, cap, 0);
        if (n >= 0) return n;
        return -1;
#else
        const ssize_t n = ::recv(native(s), buf, static_cast<usize>(cap), 0);
        if (n >= 0) return static_cast<int>(n);
        if (errno == EINTR) continue;
        return -1; // includes EAGAIN: receive timeout
#endif
    }
}

bool send_all(Socket s, const char* data, usize len) {
    usize sent = 0;
    while (sent < len) {
#ifdef _WIN32
        const int n = ::send(native(s), data + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<usize>(n);
#else
        const ssize_t n = ::send(native(s), data + sent, len - sent, kSendFlags);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<usize>(n);
#endif
    }
    return true;
}

int wait_readable(Socket s, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(native(s), &set);
    timeval tv = make_timeval(timeout_ms);
#ifdef _WIN32
    const int r = ::select(0, &set, nullptr, nullptr, &tv);
    if (r > 0) return 1;
    return r == 0 ? 0 : -1;
#else
    const int r = ::select(native(s) + 1, &set, nullptr, nullptr, &tv);
    if (r > 0) return 1;
    if (r == 0) return 0;
    return errno == EINTR ? 0 : -1;
#endif
}

} // namespace tefra::mcp::net
