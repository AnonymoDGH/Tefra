#pragma once

// Minimal HTTP/1.1 (RFC 9112) for the MCP server and its client: request parsing with Content-Length and chunked
// bodies, response building, keep-alive rules. Pure functions over byte strings, so they are unit-tested without
// sockets. No exceptions.

#include "tefra/core/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace tefra::mcp::http {

struct Header {
    std::string name;
    std::string value;
};

struct Limits {
    usize max_header_bytes = 16 * 1024;
    usize max_body_bytes = 8 * 1024 * 1024;
};

struct Request {
    std::string method;  // "POST"
    std::string target;  // "/mcp?x=1" (raw, query included)
    std::string version; // "HTTP/1.1" or "HTTP/1.0"
    std::vector<Header> headers;
    std::string body;
    bool keep_alive = true; // HTTP/1.1 default, or Connection: keep-alive for 1.0

    const std::string* find_header(std::string_view name) const; // case-insensitive; nullptr when absent
    std::string header(std::string_view name) const;             // "" when absent
};

struct Response {
    int status = 0;
    std::vector<Header> headers;
    std::string body;

    std::string header(std::string_view name) const;
};

enum class Parse { Complete, Incomplete, Error };

// Parses one request from the start of buf. On Complete, consumed is the number of bytes used (pipelined data stays
// in buf). On Incomplete more bytes are needed. On Error, error holds the reason and status the HTTP status to send.
Parse parse_request(std::string_view buf, const Limits& limits, Request& out, usize& consumed, std::string& error,
                    int& status);

// Decodes a chunked body that starts at buf. Appends the payload to out (the caller clears it first).
Parse decode_chunked(std::string_view buf, const Limits& limits, usize& consumed, std::string& out, std::string& error,
                     int& status);

// True when the request head in buf carries "Expect: 100-continue" (the client waits for an interim reply).
bool wants_continue(std::string_view buf);

// Parses the status line and headers of a response head (the text before the blank line).
bool parse_response_head(std::string_view head, Response& out);

// Full response bytes: status line, headers, Content-Length and Connection (keep-alive or close), then the body.
std::string build_response(int status, const std::vector<Header>& headers, std::string_view body, bool keep_alive);
const char* reason_phrase(int status);

bool iequals(std::string_view a, std::string_view b);
std::string_view trim(std::string_view s);

} // namespace tefra::mcp::http
