#pragma once

// Loopback TCP sockets for the MCP server and client: POSIX sockets, or Winsock2 on Windows (MinGW links ws2_32).
// Internal to engine/mcp. Only 127.0.0.1 is ever bound or connected (ADR-015).

#include "tefra/core/types.h"

#include <cstdint>

namespace tefra::mcp::net {

// Native socket handle as a signed integer (-1 = invalid on every platform).
using Socket = std::intptr_t;
inline constexpr Socket kInvalidSocket = -1;

// Reference-counted Winsock start-up (a no-op on POSIX).
bool init();
void cleanup();

// Binds 127.0.0.1:port (0 = any free port; the bound port is returned in bound_port) and starts listening.
Socket listen_loopback(u16 port, u16& bound_port, bool& in_use, int& err_code);
Socket connect_loopback(u16 port, int timeout_ms, int& err_code);
Socket accept_client(Socket listener);

void set_timeouts(Socket s, int timeout_ms); // send and receive
void set_nodelay(Socket s);
void shutdown_socket(Socket s); // wakes a thread blocked in recv or select on this socket
void close_socket(Socket s);

// Bytes received (> 0), 0 when the peer closed, -1 on error or timeout.
int recv_some(Socket s, char* buf, int cap);
bool send_all(Socket s, const char* data, usize len);
// 1 when readable, 0 on timeout, -1 on error.
int wait_readable(Socket s, int timeout_ms);

} // namespace tefra::mcp::net
