#pragma once

// Blocking HTTP/1.1 client for loopback servers (used by the tefra_mcp stdio bridge and tests). One request per
// connection ("Connection: close"); handles Content-Length, chunked and close-delimited responses.

#include "tefra/core/types.h"
#include "tefra/mcp/http.h"

#include <string>
#include <string_view>
#include <vector>

namespace tefra::mcp::http {

// Sends one request to 127.0.0.1:port (host "127.0.0.1" or "localhost" only). timeout_ms applies to connect and to
// each read. Returns false with a reason in error on transport failure; HTTP error statuses are a successful call.
bool request(const std::string& host, u16 port, std::string_view method, std::string_view target,
             const std::vector<Header>& headers, std::string_view body, int timeout_ms, Response& out,
             std::string& error);

} // namespace tefra::mcp::http
